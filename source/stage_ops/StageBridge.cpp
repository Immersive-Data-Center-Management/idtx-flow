#include "StageBridge.h"

#include <cmath>
#include <unordered_set>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/object.hpp>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/weakPtr.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/tokens.h>

#include <idtxflow_godot/nodes/IUsdNode3D.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>
#include <idtxflow/net/model/ConventionMath.h>

#include "TransformCodec.h"

namespace gxform = idtxflow::collab::xform;

using namespace godot;

namespace idtxflow
{
namespace collab
{
namespace
{
    // Godot Transform3D -> USD GfMatrix4d.
    //
    // The engine transform is first laid out as a wire matrix (row-major: Godot
    // Basis row i -> wire row i, translation in the last row) via the shared
    // transform codec, then converted to the USD convention by the single-sourced
    // transpose helper, and finally copied into GfMatrix4d
    pxr::GfMatrix4d transform_to_gfmatrix(const Transform3D& t)
    {
        net::model::PrimEdit e;
        gxform::transform_to_prim_edit(std::string(), t, e);
        const net::model::Mat4 usd = net::model::usd_from_wire(e.matrix);
        const auto& u = usd.m;
        pxr::GfMatrix4d m(1.0);
        m[0][0] = u[0];  m[0][1] = u[1];  m[0][2] = u[2];  m[0][3] = u[3];
        m[1][0] = u[4];  m[1][1] = u[5];  m[1][2] = u[6];  m[1][3] = u[7];
        m[2][0] = u[8];  m[2][1] = u[9];  m[2][2] = u[10]; m[2][3] = u[11];
        m[3][0] = u[12]; m[3][1] = u[13]; m[3][2] = u[14]; m[3][3] = u[15];
        return m;
    }

    // USD GfMatrix4d -> Godot Transform3D (Y-up).
    // 
    // USD rows map directly onto the Godot Basis rows (the engine consumes USD's stored orientation as-is on read).
    Transform3D gfmatrix_to_transform(const pxr::GfMatrix4d& m)
    {
        Basis basis(
            Vector3((real_t)m[0][0], (real_t)m[0][1], (real_t)m[0][2]),
            Vector3((real_t)m[1][0], (real_t)m[1][1], (real_t)m[1][2]),
            Vector3((real_t)m[2][0], (real_t)m[2][1], (real_t)m[2][2]));
        Vector3 origin((real_t)m[3][0], (real_t)m[3][1], (real_t)m[3][2]);
        return Transform3D(basis, origin);
    }
} // namespace

StageBridge::StageBridge(UsdStageNode3D* stage_node, pxr::UsdStageRefPtr stage,
                         EditTarget edit_target)
    : stage_node_(stage_node), stage_(std::move(stage)), edit_target_(edit_target)
{
}

StageBridge::~StageBridge()
{
    revoke_listener();
}

void StageBridge::build_index()
{
    tracked_.clear();
    if (!stage_node_)
    {
        return;
    }

    std::vector<Node*> stack;
    stack.push_back(stage_node_);
    while (!stack.empty())
    {
        Node* n = stack.back();
        stack.pop_back();

        for (int i = 0; i < n->get_child_count(); ++i)
        {
            Node* child = n->get_child(i);

            // A nested UsdStageNode3D is a referenced sub-stage with its own stage
            // and child-relative paths. Track the sub-stage node itself (it is a
            // reference prim in THIS stage, so moving it as a whole syncs), but do
            // not descend into its subtree.
            const bool is_sub_stage = (Object::cast_to<UsdStageNode3D>(child) != nullptr);
            if (!is_sub_stage)
                stack.push_back(child);

            IUsdNode3D* usd = IUsdNode3D::from_node(child);
            if (!usd)
                continue;
            Node3D* n3d = Object::cast_to<Node3D>(child);
            if (!n3d)
                continue;
            const String prim_path = usd->get_prim_path();
            if (prim_path.is_empty())
                continue;

            // Skip the stage's placement root (its defaultPrim, e.g. "/World"):
            // display-only transform, never synced.
            if (is_stage_root(std::string(prim_path.utf8().get_data())))
                continue;

            Tracked t;
            t.node_id = n3d->get_instance_id();
            tracked_[std::string(prim_path.utf8().get_data())] = t;
        }
    }

    IDTX_LOG(IDTX_DEBUG, "Indexed {} transformable prim node(s)", tracked_.size());
    register_listener();
}

void StageBridge::register_listener()
{
    if (listening_ || !stage_)
        return;
    notice_key_ = pxr::TfNotice::Register(
        pxr::TfCreateWeakPtr(this),
        &StageBridge::_on_objects_changed,
        pxr::UsdStageWeakPtr(stage_));
    listening_ = true;
    IDTX_LOG(IDTX_INFO, "TfNotice listener registered on stage");
}

void StageBridge::revoke_listener()
{
    if (!listening_)
        return;
    pxr::TfNotice::Revoke(notice_key_);
    listening_ = false;
}

void StageBridge::_on_objects_changed(const pxr::UsdNotice::ObjectsChanged& notice,
                                           const pxr::UsdStageWeakPtr& /*sender*/)
{
    // Ignore changes we are authoring ourselves (loopback suppression). The
    // engine owns the remote/armed gating; this bridge only reports genuine,
    // externally-triggered stage changes for tracked prims.
    IDTX_LOG(IDTX_DEBUG, "[trace] C _on_objects_changed fired: suppress={} has_on_changed={}",
             suppress_broadcast_, on_changed_ != nullptr);
    if (suppress_broadcast_)
        return;
    if (!on_changed_)
        return;
    // Ignore notices arriving while the stage is being released.
    if (!stage_ || !stage_->GetRootLayer())
        return;

    // One authored Set emits several paths within a single notice (the attribute
    // path and its prim path) and USD may follow with a second info-only notice;
    // without coalescing, the same prim would be reported — and broadcast —
    // multiple times for one edit. De-dupe by prim key for this dispatch.
    std::unordered_set<std::string> reported;
    auto consider = [&](const pxr::SdfPath& path)
    {
        const pxr::SdfPath prim_path = path.IsPropertyPath() ? path.GetPrimPath() : path;
        const std::string key = prim_path.GetString();
        const bool is_tracked = tracked_.find(key) != tracked_.end();
        IDTX_LOG(IDTX_DEBUG, "[trace] C consider path='{}' key='{}' tracked={}",
                 path.GetString(), key, is_tracked);
        if (tracked_.find(key) == tracked_.end())
            return;
        if (!reported.insert(key).second)
            return;

        net::model::PrimEdit edit;
        if (!read_prim(key, edit))
            return;
        on_changed_(edit);
    };

    for (const pxr::SdfPath& p : notice.GetResyncedPaths())
        consider(p);
    for (const pxr::SdfPath& p : notice.GetChangedInfoOnlyPaths())
        consider(p);
}

bool StageBridge::read_prim(const std::string& prim_path, net::model::PrimEdit& out) const
{
    Transform3D xform;
    if (!read_prim_transform(prim_path, xform))
        return false;
    gxform::transform_to_prim_edit(prim_path, xform, out);
    return true;
}

void StageBridge::author_local_edit(const net::model::PrimEdit& edit)
{
    // This IS the local change we want to broadcast, so do not suppress: authoring
    // trips the TfNotice listener, which reports it back through on_changed_.
    if (suppress_broadcast_)
        return;
    author_to_usd(edit.prim_path, gxform::prim_edit_to_transform(edit));
}

void StageBridge::apply_remote_edit(const net::model::PrimEdit& edit)
{
    auto it = tracked_.find(edit.prim_path);

    // Suppress the TfNotice our authoring will trigger so the remote change is not
    // echoed straight back out.
    suppress_broadcast_ = true;

    // The wire/USD value is raw (no spine-axis rotation). Re-apply the
    // load-time presentation rotation so both the USD author (which strips it back
    // out) and the live node carry the same node-space transform the importer
    // produced. No-op for prim types without a baked spine axis.
    const xform::SpineAxis axis = spine_axis_for(edit.prim_path);
    Transform3D node_xform = gxform::prim_edit_to_transform(edit);
    node_xform.basis = gxform::apply_spine_axis(node_xform.basis, axis);

    author_to_usd(edit.prim_path, node_xform);
    if (it != tracked_.end())
    {
        // Resolve the stored ObjectID to a live node. ObjectDB::get_instance()
        // returns null if the node has been freed. Prevents dangling pointer issues.
        Node3D* node = Object::cast_to<Node3D>(ObjectDB::get_instance(it->second.node_id));
        if (node)        
        {
            node->set_block_signals(true);
            node->set_transform(node_xform);
            node->set_block_signals(false);
        }
    }

    suppress_broadcast_ = false;
}

bool StageBridge::author_to_usd(const std::string& prim_path, const Transform3D& xform)
{
    // The stage may have been released (node left the tree) between an inbound
    // edit being queued and applied; bail rather than touch a dead stage.
    if (!stage_ || !stage_->GetRootLayer())
    {
        IDTX_LOG(IDTX_DEBUG, "author_to_usd bail: stage released");
        return false;
    }

    const pxr::SdfPath sdf_path(prim_path);
    pxr::UsdPrim prim = stage_->GetPrimAtPath(sdf_path);
    if (!prim)
    {
        IDTX_LOG(IDTX_DEBUG, "author_to_usd bail: prim not found '{}'", prim_path);
        return false;
    }

    // A prim that holds a reference/payload is authored as a typeless `def` whose
    // local transform (the arc's placement in the referencing layer) still lives
    // on this prim. Such a prim is not an xformable by schema type, yet carries a
    // valid xformOpOrder, so author onto it as well; only bail when there is no
    // transform to drive at all.
    pxr::UsdGeomXformable xformable(prim);
    if (!xformable && !prim.HasAttribute(pxr::UsdGeomTokens->xformOpOrder))
    {
        IDTX_LOG(IDTX_DEBUG, "author_to_usd bail: no transform to author '{}'", prim_path);
        return false;
    }

    // Non-destructive by default: overrides go to the session layer, leaving the
    // opened file untouched unless RootLayer was requested.
    pxr::SdfLayerHandle target_layer = (edit_target_ == EditTarget::RootLayer)
                                           ? stage_->GetRootLayer()
                                           : stage_->GetSessionLayer();
    pxr::UsdEditContext edit_ctx(stage_, target_layer);

    bool reset_stack = false;
    std::vector<pxr::UsdGeomXformOp> ops = xformable.GetOrderedXformOps(&reset_stack);

    // Strip the load-time spine-axis presentation rotation (Cone/Cylinder) so USD
    // stores the raw orientation. No-op for every other prim type.
    Transform3D raw = xform;
    raw.basis = gxform::strip_spine_axis(raw.basis, spine_axis_for(prim_path));
    const pxr::GfMatrix4d m = transform_to_gfmatrix(raw);

    pxr::UsdGeomXformOp matrix_op;
    for (const pxr::UsdGeomXformOp& op : ops)
    {
        if (op.GetOpType() == pxr::UsdGeomXformOp::TypeTransform)
        {
            matrix_op = op;
            break;
        }
    }
    if (!matrix_op)
    {
        xformable.ClearXformOpOrder();
        matrix_op = xformable.AddTransformOp();
    }
    else
    {
        // Skip authoring when the value is unchanged. Godot fires
        // NOTIFICATION_TRANSFORM_CHANGED on a prim when an ancestor moves (its
        // world transform changed) even though its local transform did not; and
        // conversion-time writes re-set the imported value. Re-authoring the same
        // value is a redundant USD write that still trips a TfNotice (and, in a
        // session, a broadcast), so bail when the stored matrix already matches.
        pxr::GfMatrix4d current(1.0);
        if (matrix_op.Get(&current))
        {
            bool unchanged = true;
            for (int i = 0; i < 4 && unchanged; ++i)
                for (int j = 0; j < 4; ++j)
                    if (std::abs(current[i][j] - m[i][j]) > 1e-9)
                    {
                        unchanged = false;
                        break;
                    }
            if (unchanged)
            {
                IDTX_LOG(IDTX_DEBUG, "unchanged, not re-authored '{}'", prim_path);
                return false;
            }
        }
    }
    matrix_op.Set(m);
    IDTX_LOG(IDTX_DEBUG, "authored xform on '{}' (target={})", prim_path,
             edit_target_ == EditTarget::RootLayer ? "root" : "session");
    return true;
}

bool StageBridge::is_stage_root(const std::string& prim_path) const
{
    if (!stage_)
        return false;
    const pxr::UsdPrim def = stage_->GetDefaultPrim();
    if (!def)
        return false;
    return def.GetPath().GetString() == prim_path;
}

bool StageBridge::read_prim_transform(const std::string& prim_path, Transform3D& out) const
{
    if (!stage_ || !stage_->GetRootLayer())
        return false;
    pxr::UsdPrim prim = stage_->GetPrimAtPath(pxr::SdfPath(prim_path));
    if (!prim)
        return false;
    pxr::UsdGeomXformable xformable(prim);
    // A typeless reference/payload holder carries a valid local transform via its
    // xformOpOrder even though it is not an xformable by schema type; read it so
    // the holder's placement can be reported. GetLocalTransformation works
    // regardless of the typed-schema check.
    if (!xformable && !prim.HasAttribute(pxr::UsdGeomTokens->xformOpOrder))
        return false;
    pxr::GfMatrix4d local(1.0);
    bool resets = false;
    if (!xformable.GetLocalTransformation(&local, &resets))
        return false;
    out = gfmatrix_to_transform(local);
    return true;
}

xform::SpineAxis StageBridge::spine_axis_for(const std::string& prim_path) const
{
    // Only Cone and Cylinder are imported via toTransform(matrix, axis), with a
    // baked spine-axis presentation rotation; every other prim type bakes none.
    // Keep this in sync with StageConverter's Cone/Cylinder branches and with
    // UsdGodotTypeConverter::toTransform.
    //
    // Why the bake exists: USD's Cone/Cylinder store their spine direction in an
    // `axis` attribute (x/y/z) with no matrix rotation, but Godot's CylinderMesh is
    // fixed to the Y axis, so the loader rotates the node basis to compensate. This
    // strip/re-apply is the exact inverse of that, so the two MUST be enabled as a
    // matched pair, gated on the same prim types.
    //
    // Capsule is intentionally not handled here: no UsdGeomCapsule converter
    // (StageConverter has no Cone/Cylinder-style branch for it) With no loader bake there is nothing
    // to invert, so adding a branch now would be dead code that becomes a latent +/-90 deg
    // bug the moment a real UsdGeomCapsule node exists without a matching bake.
    // TODO: add `IsA<pxr::UsdGeomCapsule>()` here only once StageConverter
    // converts Capsule via toTransform(matrix, axis) (Godot's CapsuleMesh is Y-fixed too,
    // so it will need the same rot_z(+90)/rot_x(+90) bake)
    if (!stage_)
        return xform::SpineAxis::None;
    pxr::UsdPrim prim = stage_->GetPrimAtPath(pxr::SdfPath(prim_path));
    if (!prim)
        return xform::SpineAxis::None;

    pxr::TfToken axis;
    if (prim.IsA<pxr::UsdGeomCone>())
        pxr::UsdGeomCone(prim).GetAxisAttr().Get(&axis);
    else if (prim.IsA<pxr::UsdGeomCylinder>())
        pxr::UsdGeomCylinder(prim).GetAxisAttr().Get(&axis);
    else
        return xform::SpineAxis::None;

    if (axis == pxr::UsdGeomTokens->x) return xform::SpineAxis::X;
    if (axis == pxr::UsdGeomTokens->z) return xform::SpineAxis::Z;
    if (axis == pxr::UsdGeomTokens->y) return xform::SpineAxis::Y;
    // GetAxisAttr().Get() returns the schema fallback (Z) when unauthored, so for a
    // Cone/Cylinder axis is always x/y/z; this is only reached if that ever changes.
    return xform::SpineAxis::None;
}

} // namespace collab
} // namespace idtxflow
