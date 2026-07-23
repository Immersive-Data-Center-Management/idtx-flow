#include "UsdTransformSync.h"

#include "IdtxClient.h"

#include <godot_cpp/classes/node.hpp>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/weakPtr.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usdGeom/xformable.h>

#include <idtxflow_godot/nodes/IUsdNode3D.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>
#include <idtxflow/net/IdtxSessionSocket.h>
#include <idtxflow/utils/Logger.h>

using namespace godot;

namespace idtxflow
{
namespace net
{
namespace
{
    static constexpr const char* LOG_CATEGORY = "UsdTransformSync";

    // Godot Transform3D -> USD GfMatrix4d.
    //
    // The rotation/scale block is TRANSPOSED on purpose: USD's GfMatrix4d uses
    // row-vector math (v' = v * M) while Godot's Basis is column-vector (v' = M * v),
    // so the USD matrix that reproduces a given Godot basis is its transpose —
    // i.e. USD row i = Godot Basis COLUMN i (b.rows[*][i]). The translation lives
    // in row 3 and is NOT transposed.
    //
    // This is the exact inverse of the author->read round-trip: authoring with
    // this function and reading back via read_prim_transform()
    // (GetLocalTransformation + gfmatrix_to_transform) must return the original
    // Transform3D.
    pxr::GfMatrix4d transform_to_gfmatrix(const Transform3D& t)
    {
        const Basis& b = t.basis;
        pxr::GfMatrix4d m(1.0);
        m[0][0] = b.rows[0][0]; m[0][1] = b.rows[1][0]; m[0][2] = b.rows[2][0]; m[0][3] = 0.0;
        m[1][0] = b.rows[0][1]; m[1][1] = b.rows[1][1]; m[1][2] = b.rows[2][1]; m[1][3] = 0.0;
        m[2][0] = b.rows[0][2]; m[2][1] = b.rows[1][2]; m[2][2] = b.rows[2][2]; m[2][3] = 0.0;
        m[3][0] = t.origin.x; m[3][1] = t.origin.y; m[3][2] = t.origin.z; m[3][3] = 1.0;
        return m;
    }

    // USD GfMatrix4d -> Godot Transform3D (matches UsdTypeConverter::toTransform, Y-up).
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

UsdTransformSync::UsdTransformSync(UsdStageNode3D* stage_node, pxr::UsdStageRefPtr stage)
    : stage_node_(stage_node), stage_(std::move(stage))
{
}

UsdTransformSync::~UsdTransformSync()
{
    revoke_listener();
}

void UsdTransformSync::build_index()
{
    tracked_.clear();
    if (!stage_node_)
        return;

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

void UsdTransformSync::register_listener()
{
    if (listening_ || !stage_)
        return;
    notice_key_ = pxr::TfNotice::Register(
        pxr::TfCreateWeakPtr(this),
        &UsdTransformSync::_on_objects_changed,
        pxr::UsdStageWeakPtr(stage_));
    listening_ = true;
    IDTX_LOG(IDTX_INFO, "TfNotice listener registered on stage");
}

void UsdTransformSync::revoke_listener()
{
    if (!listening_)
        return;
    pxr::TfNotice::Revoke(notice_key_);
    listening_ = false;
}

void UsdTransformSync::_on_objects_changed(const pxr::UsdNotice::ObjectsChanged& notice,
                                           const pxr::UsdStageWeakPtr& /*sender*/)
{
    // Ignore changes we are authoring ourselves (loopback suppression).
    if (suppress_broadcast_)
        return;
    if (!remote_)
        return;
    // Don't broadcast the transform writes that happen during initial USD
    // conversion/load — only once armed (after stage load settles).
    if (!armed_)
        return;

    IdtxClient* client = IdtxClient::get_singleton();
    if (!client)
        return;

    // Collect prim paths whose transform-relevant info changed.
    auto consider = [&](const pxr::SdfPath& path)
    {
        // Info/resync paths may be property paths (e.g. .xformOp:transform) or
        // prim paths; reduce to the owning prim path.
        const pxr::SdfPath prim_path = path.IsPropertyPath() ? path.GetPrimPath() : path;
        const std::string key = prim_path.GetString();
        auto it = tracked_.find(key);
        if (it == tracked_.end())
            return;

        Transform3D xform;
        if (!read_prim_transform(key, xform))
            return;

        // Broadcast over the socket (thread-safe / coalesced per prim).
        client->send_transform(String(key.c_str()), xform);
    };

    for (const pxr::SdfPath& p : notice.GetResyncedPaths())
        consider(p);
    for (const pxr::SdfPath& p : notice.GetChangedInfoOnlyPaths())
        consider(p);
}

void UsdTransformSync::author_local(const std::string& prim_path, const Transform3D& xform)
{
    // Authoring trips _on_objects_changed which does the conditional broadcast.
    // We do NOT suppress here (this IS the local change we want to broadcast).
    if (suppress_broadcast_)
        return;   // already inside an apply_remote / author cycle
    author_to_usd(prim_path, xform);
}

void UsdTransformSync::apply_remote(const std::string& prim_path, const Transform3D& xform)
{
    auto it = tracked_.find(prim_path);
    IDTX_LOG(IDTX_DEBUG, "apply_remote: prim='{}' tracked={}",
        prim_path, (it != tracked_.end()) ? "true" : "false");

    // Suppress the TfNotice + node-notification that our authoring will trigger,
    // so we don't echo the remote change back out.
    suppress_broadcast_ = true;

    author_to_usd(prim_path, xform);
    if (it != tracked_.end() && it->second.node)
    {
        it->second.node->set_block_signals(true);
        it->second.node->set_transform(xform);
        it->second.node->set_block_signals(false);
    }

    suppress_broadcast_ = false;
}

bool UsdTransformSync::author_to_usd(const std::string& prim_path, const Transform3D& xform)
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

    // Author into the session layer (matches the variant-selection pattern).
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

bool UsdTransformSync::read_prim_transform(const std::string& prim_path, Transform3D& out) const
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

} // namespace net
} // namespace idtxflow