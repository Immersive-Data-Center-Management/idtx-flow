#include "GodotStageBridge.h"

#include <vector>

#include <godot_cpp/classes/node.hpp>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/weakPtr.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usdGeom/xformable.h>

#include <idtxflow_godot/nodes/IUsdNode3D.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>
#include <idtxflow/net/model/ConventionMath.h>

using namespace godot;

namespace idtxflow
{
namespace collab_godot
{
namespace
{
    // Godot Transform3D -> USD GfMatrix4d.
    //
    // The engine transform is first laid out as a wire matrix (row-major: Godot
    // Basis row i -> wire row i, translation in the last row), then converted to
    // the USD convention by the single-sourced transpose helper, and finally
    // copied into GfMatrix4d. This keeps the wire<->USD relationship in one place.
    pxr::GfMatrix4d transform_to_gfmatrix(const Transform3D& t)
    {
        const Basis& b = t.basis;
        const double basis_rows[9] = {
            b.rows[0][0], b.rows[0][1], b.rows[0][2],
            b.rows[1][0], b.rows[1][1], b.rows[1][2],
            b.rows[2][0], b.rows[2][1], b.rows[2][2],
        };
        const double origin[3] = {t.origin.x, t.origin.y, t.origin.z};
        const net::model::Mat4 usd =
            net::model::usd_from_wire(net::model::wire_from_basis_origin(basis_rows, origin));
        const auto& u = usd.m;
        pxr::GfMatrix4d m(1.0);
        m[0][0] = u[0];  m[0][1] = u[1];  m[0][2] = u[2];  m[0][3] = u[3];
        m[1][0] = u[4];  m[1][1] = u[5];  m[1][2] = u[6];  m[1][3] = u[7];
        m[2][0] = u[8];  m[2][1] = u[9];  m[2][2] = u[10]; m[2][3] = u[11];
        m[3][0] = u[12]; m[3][1] = u[13]; m[3][2] = u[14]; m[3][3] = u[15];
        return m;
    }

    // USD GfMatrix4d -> Godot Transform3D (Y-up). This mirrors the original
    // read path exactly: USD rows map directly onto the Godot Basis rows (the
    // engine consumes USD's stored orientation as-is on read).
    Transform3D gfmatrix_to_transform(const pxr::GfMatrix4d& m)
    {
        Basis basis(
            Vector3((real_t)m[0][0], (real_t)m[0][1], (real_t)m[0][2]),
            Vector3((real_t)m[1][0], (real_t)m[1][1], (real_t)m[1][2]),
            Vector3((real_t)m[2][0], (real_t)m[2][1], (real_t)m[2][2]));
        Vector3 origin((real_t)m[3][0], (real_t)m[3][1], (real_t)m[3][2]);
        return Transform3D(basis, origin);
    }

    // model::PrimEdit (row-major matrix, the wire convention) -> Godot Transform3D.
    // Basis row i comes from wire row i; the translation is read from the bottom
    // row (m30..m32), the cell the backend authors into USD.
    Transform3D primedit_to_transform(const net::model::PrimEdit& edit)
    {
        double basis_rows[9];
        double origin[3];
        net::model::basis_origin_from_wire(edit.matrix, basis_rows, origin);
        Basis basis;
        basis.rows[0] = Vector3((real_t)basis_rows[0], (real_t)basis_rows[1], (real_t)basis_rows[2]);
        basis.rows[1] = Vector3((real_t)basis_rows[3], (real_t)basis_rows[4], (real_t)basis_rows[5]);
        basis.rows[2] = Vector3((real_t)basis_rows[6], (real_t)basis_rows[7], (real_t)basis_rows[8]);
        return Transform3D(basis, Vector3((real_t)origin[0], (real_t)origin[1], (real_t)origin[2]));
    }

    // Godot Transform3D -> model::PrimEdit matrix (row-major), inverse of the above.
    void transform_to_primedit(const std::string& prim_path, const Transform3D& t,
                               net::model::PrimEdit& out)
    {
        out.kind = net::model::PrimEdit::Kind::Transform;
        out.prim_path = prim_path;
        out.is_matrix = true;
        const Basis& b = t.basis;
        const double basis_rows[9] = {
            b.rows[0][0], b.rows[0][1], b.rows[0][2],
            b.rows[1][0], b.rows[1][1], b.rows[1][2],
            b.rows[2][0], b.rows[2][1], b.rows[2][2],
        };
        const double origin[3] = {t.origin.x, t.origin.y, t.origin.z};
        out.matrix = net::model::wire_from_basis_origin(basis_rows, origin);
    }
} // namespace

GodotStageBridge::GodotStageBridge(UsdStageNode3D* stage_node, pxr::UsdStageRefPtr stage)
    : stage_node_(stage_node), stage_(std::move(stage))
{
}

GodotStageBridge::~GodotStageBridge()
{
    revoke_listener();
}

void GodotStageBridge::build_index()
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

            Tracked t;
            t.node = n3d;
            tracked_[std::string(prim_path.utf8().get_data())] = t;
        }
    }

    IDTX_LOG(IDTX_DEBUG, "Indexed {} transformable prim node(s)", tracked_.size());
    register_listener();
}

void GodotStageBridge::register_listener()
{
    if (listening_ || !stage_)
        return;
    notice_key_ = pxr::TfNotice::Register(
        pxr::TfCreateWeakPtr(this),
        &GodotStageBridge::_on_objects_changed,
        pxr::UsdStageWeakPtr(stage_));
    listening_ = true;
    IDTX_LOG(IDTX_INFO, "TfNotice listener registered on stage");
}

void GodotStageBridge::revoke_listener()
{
    if (!listening_)
        return;
    pxr::TfNotice::Revoke(notice_key_);
    listening_ = false;
}

void GodotStageBridge::_on_objects_changed(const pxr::UsdNotice::ObjectsChanged& notice,
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

    auto consider = [&](const pxr::SdfPath& path)
    {
        const pxr::SdfPath prim_path = path.IsPropertyPath() ? path.GetPrimPath() : path;
        const std::string key = prim_path.GetString();
        const bool is_tracked = tracked_.find(key) != tracked_.end();
        IDTX_LOG(IDTX_DEBUG, "[trace] C consider path='{}' key='{}' tracked={}",
                 path.GetString(), key, is_tracked);
        if (tracked_.find(key) == tracked_.end())
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

bool GodotStageBridge::read_prim(const std::string& prim_path, net::model::PrimEdit& out) const
{
    Transform3D xform;
    if (!read_prim_transform(prim_path, xform))
        return false;
    transform_to_primedit(prim_path, xform, out);
    return true;
}

void GodotStageBridge::author_local_edit(const net::model::PrimEdit& edit)
{
    // This IS the local change we want to broadcast, so do not suppress: authoring
    // trips the TfNotice listener, which reports it back through on_changed_.
    if (suppress_broadcast_)
        return;
    author_to_usd(edit.prim_path, primedit_to_transform(edit));
}

void GodotStageBridge::apply_remote_edit(const net::model::PrimEdit& edit)
{
    auto it = tracked_.find(edit.prim_path);

    // Suppress the TfNotice our authoring will trigger so the remote change is not
    // echoed straight back out.
    suppress_broadcast_ = true;

    const Transform3D xform = primedit_to_transform(edit);
    author_to_usd(edit.prim_path, xform);
    if (it != tracked_.end() && it->second.node)
    {
        it->second.node->set_block_signals(true);
        it->second.node->set_transform(xform);
        it->second.node->set_block_signals(false);
    }

    suppress_broadcast_ = false;
}

bool GodotStageBridge::author_to_usd(const std::string& prim_path, const Transform3D& xform)
{
    if (!stage_)
        return false;

    const pxr::SdfPath sdf_path(prim_path);
    pxr::UsdPrim prim = stage_->GetPrimAtPath(sdf_path);
    if (!prim)
        return false;

    pxr::UsdGeomXformable xformable(prim);
    if (!xformable)
        return false;

    // Author into the session layer.
    pxr::UsdEditContext edit_ctx(stage_, stage_->GetSessionLayer());

    bool reset_stack = false;
    std::vector<pxr::UsdGeomXformOp> ops = xformable.GetOrderedXformOps(&reset_stack);

    const pxr::GfMatrix4d m = transform_to_gfmatrix(xform);

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
    matrix_op.Set(m);
    return true;
}

bool GodotStageBridge::read_prim_transform(const std::string& prim_path, Transform3D& out) const
{
    if (!stage_)
        return false;
    pxr::UsdPrim prim = stage_->GetPrimAtPath(pxr::SdfPath(prim_path));
    if (!prim)
        return false;
    pxr::UsdGeomXformable xformable(prim);
    if (!xformable)
        return false;

    pxr::GfMatrix4d local(1.0);
    bool resets = false;
    if (!xformable.GetLocalTransformation(&local, &resets))
        return false;
    out = gfmatrix_to_transform(local);
    return true;
}

} // namespace collab_godot
} // namespace idtxflow
