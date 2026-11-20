#include "PayloadPlacement.h"

#include <godot_cpp/core/object.hpp>

#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/xform.h>

#include <idtxflow_godot/nodes/UsdStageNode3D.h>
#include <idtxflow/utils/Logger.h>
#include "nodes/UsdXFormNode3D.h"

using namespace godot;
using namespace pxr;

namespace idtxflow::collab::payload
{
namespace
{
// Main-thread scene updates must not become user edits in any affected owning stage.
thread_local unsigned notification_depth = 0;

/// The nearest ancestor stage supplies the carrier's containing-stage USD opinion.
UsdStageNode3D* containing_stage(const Node* node)
{
    for (Node* parent = node->get_parent(); parent; parent = parent->get_parent())
        if (auto* stage = Object::cast_to<UsdStageNode3D>(parent)) return stage;
    return nullptr;
}

/// Fixed up-axis presentation is independent of a source prim's authored scale.
Basis up_presentation(const String& axis)
{
    if (axis == "Z") return Basis(Vector3(1, 0, 0), Math::deg_to_rad(-90.0));
    if (axis == "X") return Basis(Vector3(0, 0, 1), Math::deg_to_rad(90.0));
    return {};
}

/// USD basis rows act as Godot basis columns; translation has the same three values.
Transform3D payload_raw_transform(const GfMatrix4d& matrix)
{
    return Transform3D(Basis(Vector3(matrix[0][0], matrix[0][1], matrix[0][2]),
                             Vector3(matrix[1][0], matrix[1][1], matrix[1][2]),
                             Vector3(matrix[2][0], matrix[2][1], matrix[2][2])),
                       Vector3(matrix[3][0], matrix[3][1], matrix[3][2]));
}

/// Raw placement serialization must not include the carrier's visual presentation.
GfMatrix4d payload_usd_matrix(const Transform3D& transform)
{
    GfMatrix4d result(1.0);
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            result[row][column] = transform.basis.rows[column][row];
    result[3][0] = transform.origin.x;
    result[3][1] = transform.origin.y;
    result[3][2] = transform.origin.z;
    return result;
}

/// Consume deferred notifications while the coordinated presentation update is still guarded.
void flush_payload_notifications(Node* root)
{
    std::vector<uint64_t> identities;
    std::vector<Node*> pending{root};
    while (!pending.empty())
    {
        Node* node = pending.back();
        pending.pop_back();
        identities.push_back(node->get_instance_id());
        for (int index = 0; index < node->get_child_count(); ++index)
            pending.push_back(node->get_child(index));
    }
    // Notification callbacks may free nodes, so each captured ObjectID is resolved again before delivery.
    for (uint64_t identity: identities)
        if (auto* node = Object::cast_to<Node3D>(ObjectDB::get_instance(identity)); node && node->is_inside_tree())
            node->force_update_transform();
}
} // namespace

PresentationScope::PresentationScope()
{
    ++notification_depth;
}
PresentationScope::~PresentationScope()
{
    --notification_depth;
}

bool presentation_update_active()
{
    return notification_depth != 0;
}

/// Return carrier metadata with the supported version, or an empty dictionary when absent or incompatible.
/// The version check does not establish conversion readiness.
godot::Dictionary state(const godot::Node3D* node)
{
    if (!node || !node->has_meta(metadata_key)) return {};
    const godot::Variant value = node->get_meta(metadata_key);
    if (value.get_type() != godot::Variant::DICTIONARY) return {};
    const godot::Dictionary result = value;
    const godot::Variant version = result.get("version", 0);
    return version.get_type() == godot::Variant::INT && static_cast<int>(version) == representation_version
               ? result
               : godot::Dictionary();
}

/// Mark a built-in payload candidate with identity stage basis before source conversion.
void mark_carrier(godot::Node3D* node)
{
    godot::Dictionary data;
    data["version"] = representation_version;
    data["ready"] = false;
    data["stage_basis"] = godot::Basis();
    node->set_meta(metadata_key, data);
}

/// Capture the containing stage's fixed bake during conversion, before user placement is applied.
void record_stage_basis(godot::Node3D* node)
{
    godot::Dictionary data = state(node);
    if (data.is_empty()) return;
    data["stage_basis"] = node->get_basis();
    node->set_meta(metadata_key, data);
}

bool is_builtin_payload_carrier(const UsdStageNode3D& node)
{
    return !idtxflow::collab::payload::state(&node).is_empty();
}

bool get_payload_placement(const UsdStageNode3D& node, Transform3D& raw)
{
    const Dictionary data = idtxflow::collab::payload::state(&node);
    const Variant ready = data.get("ready", false);
    const Variant value = data.get("raw", Variant());
    if (ready.get_type() != Variant::BOOL || !static_cast<bool>(ready) || value.get_type() != Variant::TRANSFORM3D)
        return false;
    raw = value;
    return true;
}

Transform3D payload_to_display(const UsdStageNode3D& node, const Transform3D& raw)
{
    Transform3D ignored;
    if (!get_payload_placement(node, ignored)) return raw;
    const Dictionary data = idtxflow::collab::payload::state(&node);
    const Basis presentation = data["presentation"];
    const Basis stage_basis = data["stage_basis"];
    return Transform3D(stage_basis * presentation * raw.basis * presentation.inverse(), stage_basis.xform(raw.origin));
}

Transform3D payload_from_display(const UsdStageNode3D& node, const Transform3D& display)
{
    Transform3D ignored;
    if (!get_payload_placement(node, ignored)) return display;
    const Dictionary data = idtxflow::collab::payload::state(&node);
    const Basis presentation = data["presentation"];
    const Basis stage_inverse = static_cast<Basis>(data["stage_basis"]).inverse();
    return Transform3D(presentation.inverse() * stage_inverse * display.basis * presentation,
                       stage_inverse.xform(display.origin));
}

void apply_payload_presentation(UsdStageNode3D& node, const Transform3D& raw)
{
    const Dictionary data = idtxflow::collab::payload::state(&node);
    if (data.is_empty()) return;
    const bool ready = data.get("ready", false);
    const String root_path = data.get("root_path", "");
    const uint64_t identity = node.get_instance_id();
    idtxflow::collab::payload::PresentationScope scope;
    node.set_transform(payload_to_display(node, raw));
    if (ObjectDB::get_instance(identity) != &node) return;
    if (ready)
        if (Node3D* root = node.find_prim_node(root_path))
            root->set_transform(Transform3D(static_cast<Basis>(data["presentation"]), Vector3()));
}

void update_payload_placement(UsdStageNode3D& node, const Transform3D& raw, bool author_override)
{
    if (idtxflow::collab::payload::state(&node).is_empty()) return;
    // Scene callbacks may free this carrier while its presentation is being updated.
    const uint64_t identity = node.get_instance_id();
    idtxflow::collab::payload::PresentationScope scope;
    apply_payload_presentation(node, raw);
    if (ObjectDB::get_instance(identity) != &node) return;
    flush_payload_notifications(&node);
    if (ObjectDB::get_instance(identity) != &node) return;
    record_payload_placement(node, raw, author_override);
}

void record_payload_placement(UsdStageNode3D& node, const Transform3D& raw, bool author_override)
{
    // Local edits already have their display transform; flushing descendants here can invalidate SceneTree's next item.
    Dictionary data = idtxflow::collab::payload::state(&node);
    if (data.is_empty()) return;
    data["raw"] = raw;
    node.set_meta(idtxflow::collab::payload::metadata_key, data);
    const bool ready = data.get("ready", false);
    const String root_path = data.get("root_path", "");
    const auto stage = node.get_stage();
    if (!author_override || !ready || !stage || root_path.is_empty()) return;

    // The raw override remains reusable if this carrier later reconstructs its nested stage.
    const SdfLayerHandle layer = stage->GetSessionLayer();
    if (!layer)
    {
        IDTX_LOGF(IDTX_ERROR, "Payload placement has no session layer for '{}'", root_path.utf8().get_data());
        return;
    }
    const SdfChangeBlock block;
    const SdfPath path(root_path.utf8().get_data());
    const auto spec = SdfCreatePrimInLayer(layer, path);
    if (!spec)
    {
        IDTX_LOGF(IDTX_ERROR, "Cannot author payload placement prim '{}'", path.GetString());
        return;
    }
    // Creation and subsequent edits share the same attribute specs in the nested override.
    // Creating a duplicate spec does not return the existing attribute.
    auto matrix = layer->GetAttributeAtPath(path.AppendProperty(TfToken("xformOp:transform")));
    if (!matrix) matrix = SdfAttributeSpec::New(spec, "xformOp:transform", SdfValueTypeNames->Matrix4d);
    if (!matrix || matrix->GetTypeName() != SdfValueTypeNames->Matrix4d)
    {
        IDTX_LOGF(IDTX_ERROR, "Cannot author payload matrix attribute for '{}'", path.GetString());
        return;
    }
    auto order = layer->GetAttributeAtPath(path.AppendProperty(TfToken("xformOpOrder")));
    if (!order)
        order = SdfAttributeSpec::New(spec, "xformOpOrder", SdfValueTypeNames->TokenArray, SdfVariabilityUniform);
    if (!order || order->GetTypeName() != SdfValueTypeNames->TokenArray)
    {
        IDTX_LOGF(IDTX_ERROR, "Cannot author payload operation-order attribute for '{}'", path.GetString());
        return;
    }
    if (!matrix->SetDefaultValue(VtValue(payload_usd_matrix(raw))) ||
        !order->SetDefaultValue(VtValue(VtTokenArray{TfToken("xformOp:transform")})))
    {
        IDTX_LOGF(IDTX_ERROR, "Cannot update payload placement values for '{}'", path.GetString());
        return;
    }
    std::string content;
    if (!layer->ExportToString(&content))
    {
        IDTX_LOGF(IDTX_ERROR, "Cannot serialize payload placement override for '{}'", path.GetString());
        return;
    }
    node.set_meta("USD_OVERRIDE_LAYER", String(content.c_str()));
}

void finalize_payload_placement(UsdStageNode3D& node, const std::vector<Node3D*>& roots)
{
    Dictionary data = idtxflow::collab::payload::state(&node);
    const auto stage = node.get_stage();
    if (data.is_empty() || !stage) return;
    if (roots.size() != 1)
    {
        node.remove_meta(idtxflow::collab::payload::metadata_key);
        return;
    }
    const UsdPrim default_prim = stage->GetDefaultPrim();
    if (!default_prim || !default_prim.IsA<UsdGeomXform>())
    {
        node.remove_meta(idtxflow::collab::payload::metadata_key);
        return;
    }
    // Tree-entry observers can retire a converted root before finalization.
    auto* root = Object::cast_to<UsdXformNode3D>(node.find_prim_node(String(default_prim.GetPath().GetText())));
    if (!root || root->get_parent() != &node || root->get_class() != StringName("UsdXformNode3D") ||
        root->get_prim_path() != String(default_prim.GetPath().GetText()) || root->get_animation().is_valid())
    {
        // Unsupported root representations retain their ordinary converter behavior.
        node.remove_meta(idtxflow::collab::payload::metadata_key);
        return;
    }

    GfMatrix4d matrix(1.0);
    bool reset_stack = false;
    bool update_override = false;
    if (!UsdGeomXformable(default_prim).GetLocalTransformation(&matrix, &reset_stack)) return;
    UsdStageNode3D* owner = containing_stage(&node);
    if (owner && owner->get_stage())
    {
        const UsdPrim holder = owner->get_stage()->GetPrimAtPath(SdfPath(node.get_prim_path().utf8().get_data()));
        if (holder && holder.HasAttribute(UsdGeomTokens->xformOpOrder))
        {
            GfMatrix4d latest(1.0);
            if (UsdGeomXformable(holder).GetLocalTransformation(&latest, &reset_stack))
            {
                update_override = matrix != latest;
                matrix = latest;
            }
        }
    }
    const String source_up(UsdGeomGetStageUpAxis(stage).GetText());
    const String parent_up = node.get_meta("USD_PARENT_UP", "Y");
    const Basis presentation = up_presentation(parent_up).inverse() * up_presentation(source_up);
    data["ready"] = true;
    data["presentation"] = presentation;
    data["root_path"] = String(default_prim.GetPath().GetText());
    data["raw"] = payload_raw_transform(matrix);
    node.set_meta(idtxflow::collab::payload::metadata_key, data);
    // Presentation callbacks may retire the containing stage before its index is rebuilt.
    const uint64_t owner_id = owner ? owner->get_instance_id() : 0;
    update_payload_placement(node, payload_raw_transform(matrix), update_override);
    owner = owner_id ? Object::cast_to<UsdStageNode3D>(ObjectDB::get_instance(owner_id)) : nullptr;
    if (owner)
        if (auto* bridge = owner->get_or_create_bridge()) bridge->build_index();
}

} // namespace idtxflow::collab::payload
