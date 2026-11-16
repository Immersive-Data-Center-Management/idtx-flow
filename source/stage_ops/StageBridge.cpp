#include "StageBridge.h"

#include <cmath>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/object.hpp>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/weakPtr.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/sdf/valueTypeName.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/tokens.h>

#include <idtxflow/resolver/UsdAssetPath.h>
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
    m[0][0] = u[0];
    m[0][1] = u[1];
    m[0][2] = u[2];
    m[0][3] = u[3];
    m[1][0] = u[4];
    m[1][1] = u[5];
    m[1][2] = u[6];
    m[1][3] = u[7];
    m[2][0] = u[8];
    m[2][1] = u[9];
    m[2][2] = u[10];
    m[2][3] = u[11];
    m[3][0] = u[12];
    m[3][1] = u[13];
    m[3][2] = u[14];
    m[3][3] = u[15];
    return m;
}

// USD GfMatrix4d -> Godot Transform3D (Y-up).
//
// USD rows map directly onto the Godot Basis rows (the engine consumes USD's stored orientation as-is on read).
Transform3D gfmatrix_to_transform(const pxr::GfMatrix4d& m)
{
    Basis basis(Vector3((real_t)m[0][0], (real_t)m[0][1], (real_t)m[0][2]),
                Vector3((real_t)m[1][0], (real_t)m[1][1], (real_t)m[1][2]),
                Vector3((real_t)m[2][0], (real_t)m[2][1], (real_t)m[2][2]));
    Vector3 origin((real_t)m[3][0], (real_t)m[3][1], (real_t)m[3][2]);
    return Transform3D(basis, origin);
}

// Readback uses the wire convention on both sides, without primitive presentation rotations.
bool same_matrix(const net::model::Mat4& left, const net::model::Mat4& right)
{
    for (size_t index = 0; index < left.m.size(); ++index)
        if (!(std::abs(left.m[index] - right.m[index]) <= 1e-9)) return false;
    return true;
}

} // namespace

StageBridge::StageBridge(UsdStageNode3D* stage_node, pxr::UsdStageRefPtr stage, EditTarget edit_target)
    : stage_node_(stage_node),
      stage_(std::move(stage)),
      edit_target_(edit_target)
{
}

StageBridge::~StageBridge()
{
    dispatch_state_->alive = false;
    ++dispatch_state_->sink_revision;
    revoke_listener();
}

void StageBridge::build_index()
{
    auto previous = std::move(tracked_);
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

            // A nested UsdStageNode3D is a reference/payload sub-stage with its own stage
            // and child-relative paths. Track the sub-stage node itself (it is an
            // asset holder in THIS stage, so moving it as a whole syncs), but do
            // not descend into its subtree.
            const bool is_sub_stage = (Object::cast_to<UsdStageNode3D>(child) != nullptr);
            if (!is_sub_stage) stack.push_back(child);

            IUsdNode3D* usd = IUsdNode3D::from_node(child);
            if (!usd) continue;
            Node3D* n3d = Object::cast_to<Node3D>(child);
            if (!n3d) continue;
            const String prim_path = usd->get_prim_path();
            if (prim_path.is_empty()) continue;

            // Skip the stage's placement root (its defaultPrim, e.g. "/World"):
            // excluded from the broadcast index; explicit local authoring has its own policy.
            if (is_stage_root(std::string(prim_path.utf8().get_data()))) continue;

            Tracked t;
            t.node_id = n3d->get_instance_id();
            const std::string key(prim_path.utf8().get_data());
            const auto old = previous.find(key);
            if (old != previous.end() && old->second.node_id == t.node_id && old->second.has_observation)
            {
                t.observed = old->second.observed;
                t.has_observation = old->second.has_observation;
            }
            else
            {
                net::model::PrimEdit initial;
                t.has_observation = read_prim(key, initial);
                if (t.has_observation) t.observed = initial.matrix;
            }
            tracked_[key] = t;
        }
    }

    IDTX_LOG(IDTX_DEBUG, "Indexed {} transformable prim node(s)", tracked_.size());
    register_listener();
}

void StageBridge::register_listener()
{
    if (listening_ || !stage_) return;
    notice_key_ = pxr::TfNotice::Register(pxr::TfCreateWeakPtr(this), &StageBridge::_on_objects_changed,
                                          pxr::UsdStageWeakPtr(stage_));
    listening_ = true;
    IDTX_LOG(IDTX_INFO, "TfNotice listener registered on stage");
}

void StageBridge::revoke_listener()
{
    if (!listening_) return;
    pxr::TfNotice::Revoke(notice_key_);
    listening_ = false;
}

void StageBridge::_on_objects_changed(const pxr::UsdNotice::ObjectsChanged& notice,
                                      const pxr::UsdStageWeakPtr& /*sender*/)
{
    // Ignore notices arriving while the stage is being released.
    if (!stage_ || !stage_->GetRootLayer()) return;
    // CollabEngine decides whether a change can be sent. Collect notices during
    // our own writes so Godot node updates and raw transform reads use the
    // completed USD state.
    const auto state = dispatch_state_;
    auto observe = [&](const pxr::SdfPath& path) {
        const pxr::SdfPath prim_path = path.IsPropertyPath() ? path.GetPrimPath() : path;
        if (prim_path.IsPrimPath()) pending_transforms_.insert(prim_path.GetString());
    };
    // Copy notice paths because the notice is temporary and can arrive
    // before a transform write has finished.
    for (const pxr::SdfPath& path : notice.GetResyncedPaths())
    {
        if (path.IsPrimPath()) pending_resyncs_.insert(path);
        observe(path);
    }
    for (const pxr::SdfPath& path : notice.GetChangedInfoOnlyPaths()) observe(path);
    if (state->authoring) return;

    std::vector<net::model::PrimEdit> edits;
    {
        AuthoringScope scope(state);
        edits = finish_changes();
        if (!state->alive) return;
    }
    dispatch_changes(edits);
}

void StageBridge::reconcile_structural(const std::set<pxr::SdfPath>& paths)
{
    if (!stage_node_ || !stage_) return;
    const auto state = dispatch_state_;

    // Build / hide / show Godot nodes to match structural stage changes (prim
    // creation, active=false soft-delete, active=true restore). Inbound create
    // and delete must still change the scene tree without being sent back out.
    // Only prim resync paths change the node tree; property paths trigger
    // transform checks instead.
    for (const pxr::SdfPath& prim_path : paths)
    {
        if (!prim_path.IsPrimPath()) continue;
        const std::string key = prim_path.GetString();
        if (is_stage_root(key)) continue;

        pxr::UsdPrim prim = stage_->GetPrimAtPath(prim_path);
        godot::Node3D* node = stage_node_->find_prim_node(String(key.c_str()));
        if (prim && prim.IsActive())
        {
            if (!node)
            {
                // Newly created prim (or a reactivated one whose node was freed):
                // convert its subtree and attach it under its parent node.
                stage_node_->convert_prim_subtree(String(key.c_str()));
            }
            else if (!node->is_visible())
            {
                // The hidden subtree is still present,
                // so just show it again — no re-traversal/conversion needed.
                node->set_visible(true);
            }
        }
        else if (prim && node)
        {
            // Soft-delete (active=false): hide the node and its subtree from
            // rendering while keeping it alive for a cheap restore.
            // Retained subtrees preserve instance identity across soft delete and restore.
            node->set_visible(false);
        }
        // A descendant pruned by an inactive ancestor needs no separate hide;
        // its retained ancestor already hides it from rendering.
        if (!state->alive) return;
        flush_transform_notifications(key);
        if (!state->alive) return;
    }
}

void StageBridge::flush_transform_notifications(const std::string& prim_path)
{
    if (!stage_node_) return;
    const auto state = dispatch_state_;
    std::vector<uint64_t> node_ids;
    const pxr::SdfPath root(prim_path);
    for (const auto& item : tracked_)
        if (pxr::SdfPath(item.first).HasPrefix(root)) node_ids.push_back(item.second.node_id);
    // A placement root is deliberately absent from the broadcast index.
    if (Node3D* node = stage_node_->find_prim_node(String(prim_path.c_str())))
        node_ids.push_back(node->get_instance_id());
    for (uint64_t id : node_ids)
    {
        Node3D* node = Object::cast_to<Node3D>(ObjectDB::get_instance(id));
        if (!node || !node->is_inside_tree()) continue;
        UsdStageNode3D* owner = nullptr;
        for (Node* parent = node->get_parent(); parent; parent = parent->get_parent())
        {
            owner = Object::cast_to<UsdStageNode3D>(parent);
            if (owner) break;
        }
        if (owner != stage_node_) continue;
        node->force_update_transform();
        if (!state->alive) return;
    }
}

std::vector<net::model::PrimEdit> StageBridge::finish_changes()
{
    const auto state = dispatch_state_;
    while (!pending_resyncs_.empty())
    {
        std::set<pxr::SdfPath> paths;
        paths.swap(pending_resyncs_);
        reconcile_structural(paths);
        if (!state->alive) return {};
    }

    // One authored Set can emit several paths within a single notice (the attribute
    // path and its prim path) and USD may follow with a second info-only notice.
    // Group those paths by prim and compare with its last recorded transform
    // so repeated notices for the same value do not produce repeated reports.
    std::set<std::string> paths;
    paths.swap(pending_transforms_);
    std::vector<net::model::PrimEdit> edits;
    for (const std::string& path : paths)
    {
        net::model::PrimEdit edit;
        if (!read_prim(path, edit)) continue;
        auto tracked = tracked_.find(path);
        if (tracked == tracked_.end()) continue;
        const bool changed = tracked->second.has_observation &&
                             !same_matrix(tracked->second.observed, edit.matrix);
        tracked->second.observed = edit.matrix;
        tracked->second.has_observation = true;
        if (changed) edits.push_back(std::move(edit));
    }
    return edits;
}

void StageBridge::dispatch_changes(const std::vector<net::model::PrimEdit>& edits)
{
    const auto state = dispatch_state_;
    const uint64_t revision = state->sink_revision;
    const OnChanged sink = on_changed_;
    if (!sink) return;
    // A callback can destroy this bridge or install a different callback.
    for (const auto& edit : edits)
    {
        if (!state->alive || state->sink_revision != revision) return;
        sink(edit);
    }
}

bool StageBridge::read_prim(const std::string& prim_path, net::model::PrimEdit& out) const
{
    Transform3D xform;
    if (!read_prim_transform(prim_path, xform)) return false;
    gxform::transform_to_prim_edit(prim_path, xform, out);
    return true;
}

void StageBridge::author_edit(const net::model::PrimEdit& edit, net::ports::EditOrigin origin)
{
    const auto state = dispatch_state_;
    // Node notifications can call back into authoring while an edit is still
    // running. Ignore those nested calls until the current edit finishes.
    if (state->authoring) return;
    // Keep our own USD and Godot updates from starting another edit. Both origins
    // collect notices until the write finishes. Local reports changed transforms;
    // External updates the recorded values without reporting them. The per-kind
    // helpers below do not control this guard.
    const bool external = (origin == net::ports::EditOrigin::External);
    std::vector<net::model::PrimEdit> edits;
    {
        AuthoringScope scope(state);
        switch (edit.kind)
        {
        case net::model::PrimEdit::Kind::Transform:  author_transform(edit, external); break;
        case net::model::PrimEdit::Kind::CreatePrim: author_create(edit);              break;
        case net::model::PrimEdit::Kind::SetActive:  author_set_active(edit);          break;
        }
        if (!state->alive) return;
        edits = finish_changes();
        if (!state->alive) return;
    }
    // Remember external values for later comparisons without reporting them as local changes.
    if (!external) dispatch_changes(edits);
}

void StageBridge::author_transform(const net::model::PrimEdit& edit, bool external)
{
    const auto state = dispatch_state_;
    // The wire/USD value is raw (no spine-axis rotation). For an external edit,
    // re-apply the load-time presentation rotation so both the USD author (which
    // strips it back out) and the live node carry the same node-space transform
    // the importer produced; a local edit already carries the node-space basis
    // the live node holds, so it is authored as-is. This spine-axis adjustment
    // requires a marked built-in visual.
    Transform3D node_xform = gxform::prim_edit_to_transform(edit);
    if (external)
    {
        const xform::SpineAxis axis = spine_axis_for(edit.prim_path);
        node_xform.basis = gxform::apply_spine_axis(node_xform.basis, axis);
    }

    author_to_usd(edit.prim_path, node_xform);
    if (!state->alive) return;

    // Mirror the authored transform onto the live node only for external edits;
    // a local edit originates from the node, which already holds this transform.
    if (external && stage_node_)
    {
        // USD authoring can rebuild the lookup, so find the current node again after the write.
        Node3D* node = stage_node_->find_prim_node(String(edit.prim_path.c_str()));
        if (node)
        {
            const uint64_t id = node->get_instance_id();
            const bool blocked = node->is_blocking_signals();
            node->set_block_signals(true);
            node->set_transform(node_xform);
            if (!state->alive) return;
            // Transform callbacks may free this node before its signal state is restored.
            // Resolve the captured ObjectID to a live node. ObjectDB::get_instance()
            // returns null if the node has been freed. Prevents dangling pointer issues.
            node = Object::cast_to<Node3D>(ObjectDB::get_instance(id));
            if (node) node->set_block_signals(blocked);
            // Deliver pending transform notifications while our own node updates
            // are still prevented from starting another edit.
            flush_transform_notifications(edit.prim_path);
        }
    }
}

void StageBridge::author_create(const net::model::PrimEdit& edit)
{
    // The stage may have been released between the inbound message being queued
    // and applied; bail rather than touch a dead stage.
    if (!stage_ || !stage_->GetRootLayer()) return;
    // A create is either a typed built-in prim or a typeless reference holder;
    // nothing to author otherwise.
    if (edit.prim_type.empty() && !edit.has_ref) return;

    const pxr::SdfPath path(edit.prim_path);
    if (!path.IsPrimPath()) return;

    // Reject an unusable asset anchor before a partial holder can enter composition.
    // Anonymous session layers have no asset directory; wire-relative arcs belong to the owning root.
    const std::string arc_asset = edit.has_ref
        ? idtxflow::resolver::AnchorAssetIdentifier(edit.reference_path, stage_->GetRootLayer(),
                                                    stage_->GetPathResolverContext())
        : std::string();
    if (edit.has_ref && arc_asset.empty())
    {
        IDTX_LOG(IDTX_WARN,
                 "author_create: cannot anchor asset '{}' for '{}'; open the owning stage from a file/URI or supply an absolute asset identifier",
                 edit.reference_path, edit.prim_path);
        return;
    }

    // Author into the same layer inbound transforms use (session by default), so
    // the local copy mirrors the server's state without touching the opened root
    // layer unless RootLayer was requested.
    pxr::SdfLayerHandle target_layer =
        (edit_target_ == EditTarget::RootLayer) ? stage_->GetRootLayer() : stage_->GetSessionLayer();

    // One SdfChangeBlock makes the def, its attributes, any reference/payload arc
    // and the local transform visible to USD composition together. Update Godot
    // nodes after the full authoring call finishes so conversion reads the
    // complete prim. Authoring at the Sdf level avoids the
    // Usd-DefinePrim-inside-a-change-block pattern USD advises against.
    pxr::SdfChangeBlock block;
    if (pxr::SdfPrimSpecHandle spec = pxr::SdfCreatePrimInLayer(target_layer, path))
    {
        spec->SetSpecifier(pxr::SdfSpecifierDef);

        // The local reference-holder spec is typeless and carries a reference or payload arc to
        // the target asset; a built-in prim carries its schema type and the shape
        // defaults the server sent.
        if (edit.has_ref)
        {
            if (edit.ref_kind == net::model::PrimEdit::RefKind::Payload)
                spec->GetPayloadList().GetExplicitItems().push_back(pxr::SdfPayload(arc_asset));
            else
                spec->GetReferenceList().GetExplicitItems().push_back(pxr::SdfReference(arc_asset));
        }
        else
        {
            spec->SetTypeName(pxr::TfToken(edit.prim_type));

            // Author the built-in shape defaults the server sent. Only the fields
            // that apply to the type are present (has-field semantics).
            auto author_double = [&](const pxr::TfToken& name, double value) {
                if (pxr::SdfAttributeSpecHandle attr =
                        pxr::SdfAttributeSpec::New(spec, name.GetString(), pxr::SdfValueTypeNames->Double))
                    attr->SetDefaultValue(pxr::VtValue(value));
            };
            if (edit.has_size) author_double(pxr::UsdGeomTokens->size, edit.size);
            if (edit.has_height) author_double(pxr::UsdGeomTokens->height, edit.height);
            if (edit.has_radius) author_double(pxr::UsdGeomTokens->radius, edit.radius);
            if (edit.has_width) author_double(pxr::UsdGeomTokens->width, edit.width);
            if (edit.has_length) author_double(pxr::UsdGeomTokens->length, edit.length);
            if (edit.has_axis)
            {
                // axis is a uniform token on the gprim schema.
                if (pxr::SdfAttributeSpecHandle attr =
                        pxr::SdfAttributeSpec::New(spec, pxr::UsdGeomTokens->axis.GetString(),
                                                   pxr::SdfValueTypeNames->Token, pxr::SdfVariabilityUniform))
                    attr->SetDefaultValue(pxr::VtValue(pxr::TfToken(edit.axis)));
            }
        }

        // Optional create-time LOCAL transform, authored verbatim as a single
        // matrix xformOp:transform. The server authored it with no spine-axis
        // presentation bake, so it is written as-is (no strip_spine_axis) —
        // unlike an inbound transform edit for a marked built-in visual, whose
        // raw value needs the bake re-applied. Resolved to a matrix here; a separate branch composes via
        // the shared transform codec.
        if (edit.has_create_transform)
        {
            const Transform3D node_xform =
                gxform::to_transform(edit.create_is_matrix, edit.create_matrix, edit.create_separate);
            const pxr::GfMatrix4d m = transform_to_gfmatrix(node_xform);
            if (pxr::SdfAttributeSpecHandle attr =
                    pxr::SdfAttributeSpec::New(spec, "xformOp:transform", pxr::SdfValueTypeNames->Matrix4d))
                attr->SetDefaultValue(pxr::VtValue(m));
            if (pxr::SdfAttributeSpecHandle order =
                    pxr::SdfAttributeSpec::New(spec, pxr::UsdGeomTokens->xformOpOrder.GetString(),
                                               pxr::SdfValueTypeNames->TokenArray, pxr::SdfVariabilityUniform))
                order->SetDefaultValue(pxr::VtValue(pxr::VtTokenArray{pxr::TfToken("xformOp:transform")}));
        }
    }
    else
    {
        IDTX_LOG(IDTX_WARN, "author_create: could not create prim spec for '{}'", edit.prim_path);
    }
    IDTX_LOG(IDTX_DEBUG, "authored create of {} '{}'", edit.has_ref ? "reference" : edit.prim_type.c_str(),
             edit.prim_path);
}

void StageBridge::author_set_active(const net::model::PrimEdit& edit)
{
    if (!stage_ || !stage_->GetRootLayer()) return;

    const pxr::SdfPath path(edit.prim_path);
    // GetPrimAtPath returns inactive prims too, so this also finds a prim that was
    // already soft-deleted (needed to restore it with active=true).
    pxr::UsdPrim prim = stage_->GetPrimAtPath(path);
    if (!prim)
    {
        IDTX_LOG(IDTX_DEBUG, "author_set_active: prim not found '{}'", edit.prim_path);
        return;
    }

    pxr::SdfLayerHandle target_layer =
        (edit_target_ == EditTarget::RootLayer) ? stage_->GetRootLayer() : stage_->GetSessionLayer();

    // The resulting resync notice drives the Godot hide/show through
    // reconcile_structural.
    pxr::UsdEditContext edit_ctx(stage_, target_layer);
    prim.SetActive(edit.active);
    IDTX_LOG(IDTX_DEBUG, "authored active={} on '{}'", edit.active, edit.prim_path);
}

bool StageBridge::author_to_usd(const std::string& prim_path, const Transform3D& xform)
{
    const auto state = dispatch_state_;
    const pxr::UsdStageRefPtr stage = stage_;
    const EditTarget edit_target = edit_target_;
    // The stage may have been released (node left the tree) between an inbound
    // edit being queued and applied; bail rather than touch a dead stage.
    if (!stage || !stage->GetRootLayer())
    {
        IDTX_LOG(IDTX_DEBUG, "author_to_usd bail: stage released");
        return false;
    }

    const pxr::SdfPath sdf_path(prim_path);
    pxr::UsdPrim prim = stage->GetPrimAtPath(sdf_path);
    if (!prim)
    {
        IDTX_LOG(IDTX_DEBUG, "author_to_usd bail: prim not found '{}'", prim_path);
        return false;
    }

    // A prim that holds a reference/payload can have a local typeless `def` whose
    // local transform (the arc's placement in the referencing layer) still lives
    // on this prim. When the composed prim is not xformable by schema type, it
    // may still carry a valid xformOpOrder, so author onto it as well; only bail
    // when there is no transform to drive at all.
    pxr::UsdGeomXformable xformable(prim);
    if (!xformable && !prim.HasAttribute(pxr::UsdGeomTokens->xformOpOrder))
    {
        IDTX_LOG(IDTX_DEBUG, "author_to_usd bail: no transform to author '{}'", prim_path);
        return false;
    }

    // Non-destructive by default: overrides go to the session layer, leaving the
    // opened root layer untouched unless RootLayer was requested.
    pxr::SdfLayerHandle target_layer =
        (edit_target == EditTarget::RootLayer) ? stage->GetRootLayer() : stage->GetSessionLayer();
    pxr::UsdEditContext edit_ctx(stage, target_layer);

    bool reset_stack = false;
    std::vector<pxr::UsdGeomXformOp> ops = xformable.GetOrderedXformOps(&reset_stack);

    // Strip the load-time spine-axis presentation rotation from marked built-in
    // Cone/Cylinder/Capsule visuals so USD stores the raw orientation. Unmarked
    // representations receive no built-in spine-axis adjustment.
    Transform3D raw = xform;
    raw.basis = gxform::strip_spine_axis(raw.basis, spine_axis_for(prim_path));
    const pxr::GfMatrix4d m = transform_to_gfmatrix(raw);

    pxr::UsdGeomXformOp matrix_op;
    for (const pxr::UsdGeomXformOp& op: ops)
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
        if (!state->alive) return false;
        matrix_op = xformable.AddTransformOp();
        if (!state->alive) return false;
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
             edit_target == EditTarget::RootLayer ? "root" : "session");
    return true;
}

bool StageBridge::is_stage_root(const std::string& prim_path) const
{
    if (!stage_) return false;
    const pxr::UsdPrim def = stage_->GetDefaultPrim();
    if (!def) return false;
    return def.GetPath().GetString() == prim_path;
}

bool StageBridge::read_prim_transform(const std::string& prim_path, Transform3D& out) const
{
    if (!stage_ || !stage_->GetRootLayer()) return false;
    pxr::UsdPrim prim = stage_->GetPrimAtPath(pxr::SdfPath(prim_path));
    if (!prim) return false;
    pxr::UsdGeomXformable xformable(prim);
    // A typeless reference/payload holder carries a valid local transform via its
    // xformOpOrder even though it is not an xformable by schema type; read it so
    // the holder's placement can be reported. GetLocalTransformation works
    // regardless of the typed-schema check.
    if (!xformable && !prim.HasAttribute(pxr::UsdGeomTokens->xformOpOrder)) return false;
    pxr::GfMatrix4d local(1.0);
    bool resets = false;
    if (!xformable.GetLocalTransformation(&local, &resets)) return false;
    out = gfmatrix_to_transform(local);
    return true;
}

xform::SpineAxis StageBridge::spine_axis_for(const std::string& prim_path) const
{
    // Built-in Cone, Cylinder and Capsule visuals are imported via toTransform(matrix, axis),
    // with a baked spine-axis presentation rotation. Keep this in sync with
    // StageConverter's Cone/Cylinder/Capsule branches and UsdGodotTypeConverter::toTransform.
    //
    // Why the bake exists: USD's Cone/Cylinder/Capsule store their axis direction in
    // an `axis` attribute (x/y/z) with no matrix rotation, but Godot's CylinderMesh
    // and CapsuleMesh have a fixed Y spine, so the loader rotates the node basis to
    // compensate. This strip/re-apply is the exact inverse of that, so the two MUST
    // be enabled as a matched pair, gated on the same marked visual representations.
    // Plane uses resource orientation and does not carry this node-axis presentation.
    //
    // Schema identity also covers colliders, payload carriers and custom representations.
    // Only a marked built-in visual permits inversion of a node-axis rotation.
    if (!stage_ || !stage_node_) return xform::SpineAxis::None;
    const Node3D* node = stage_node_->find_prim_node(String(prim_path.c_str()));
    if (!node) return xform::SpineAxis::None;
    const Variant presentation = node->get_meta(IUsdNode3D::BUILTIN_SPINE_AXIS_META, false);
    if (presentation.get_type() != Variant::BOOL || !static_cast<bool>(presentation))
        return xform::SpineAxis::None;

    pxr::UsdPrim prim = stage_->GetPrimAtPath(pxr::SdfPath(prim_path));
    if (!prim) return xform::SpineAxis::None;

    pxr::TfToken axis;
    if (prim.IsA<pxr::UsdGeomCone>())
        pxr::UsdGeomCone(prim).GetAxisAttr().Get(&axis);
    else if (prim.IsA<pxr::UsdGeomCylinder>())
        pxr::UsdGeomCylinder(prim).GetAxisAttr().Get(&axis);
    else if (prim.IsA<pxr::UsdGeomCapsule>())
        pxr::UsdGeomCapsule(prim).GetAxisAttr().Get(&axis);
    else
        return xform::SpineAxis::None;

    if (axis == pxr::UsdGeomTokens->x) return xform::SpineAxis::X;
    if (axis == pxr::UsdGeomTokens->z) return xform::SpineAxis::Z;
    if (axis == pxr::UsdGeomTokens->y) return xform::SpineAxis::Y;
    // GetAxisAttr().Get() returns the schema fallback (Z) when unauthored.
    // Invalid authored axis tokens cannot establish a supported presentation convention.
    return xform::SpineAxis::None;
}

} // namespace collab
} // namespace idtxflow
