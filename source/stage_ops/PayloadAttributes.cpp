#include "PayloadAttributes.h"
#include "PayloadPlacement.h"
#include <idtxflow/stage_ops/AttributeCodec.h>
#include <idtxflow/net/ports/IStageBridge.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <pxr/usd/pcp/primIndex.h>
#include <pxr/usd/pcp/layerStack.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/changeBlock.h>
#include <pxr/usd/sdf/primSpec.h>
#include <pxr/usd/usd/attribute.h>

namespace idtxflow::collab::payload
{
namespace
{
constexpr auto metadata = "USD_PAYLOAD_ATTRIBUTES";
bool ordinary(const std::string& name)
{
    return name != "xformOpOrder" && name.rfind("xformOp:", 0) != 0;
}
struct Current
{
    pxr::UsdStageRefPtr owner;
    pxr::SdfPath holder;
    pxr::SdfPath target;
    godot::Dictionary data;
};
Current current(UsdStageNode3D& carrier, const pxr::UsdStageRefPtr& loaded)
{
    Current result;
    if (!is_builtin_payload_carrier(carrier) || !carrier.has_meta(metadata)) return result;
    result.data = carrier.get_meta(metadata);
    if (godot::String(result.data.get("uri", "")) != carrier.get_stage_uri()) return result;
    for (auto* parent = carrier.get_parent(); parent; parent = parent->get_parent())
    {
        auto* owner = godot::Object::cast_to<UsdStageNode3D>(parent);
        if (!owner) continue;
        const auto stage = owner->get_stage();
        if (!stage ||
            godot::String(stage->GetRootLayer()->GetIdentifier().c_str()) != godot::String(result.data.get("root", "")))
            return result;
        // Resolve current ownership on every call. Serialized overlays can survive a PackedScene reload,
        // so a runtime ObjectID must not be persisted as their identity.
        if (carrier.get_stage_node() != owner || carrier.get_prim_path() != godot::String(result.data["holder"]))
            return result;
        result.owner = stage;
        break;
    }
    if (!result.owner) return result;
    result.holder = pxr::SdfPath(godot::String(result.data["holder"]).utf8().get_data());
    const godot::String target = result.data.get("target", "");
    if (!target.is_empty()) result.target = pxr::SdfPath(target.utf8().get_data());
    if (result.target.IsEmpty() && loaded && loaded->GetDefaultPrim())
        result.target = loaded->GetDefaultPrim().GetPath();
    return result;
}
std::vector<stage_ops::PreparedAttribute> collect(Current& state)
{
    std::vector<stage_ops::PreparedAttribute> attributes;
    if (!state.owner) return attributes;
    const auto prim = state.owner->GetPrimAtPath(state.holder);
    if (!prim) return attributes;
    godot::PackedStringArray names = state.data.get("names", godot::PackedStringArray());
    // Also includes Create defaults received before the carrier existed or before the worker started.
    const auto spec = state.owner->GetSessionLayer()->GetPrimAtPath(state.holder);
    if (spec)
        for (const auto& attribute: spec->GetAttributes())
        {
            const auto name = attribute->GetName();
            if (ordinary(name) && !names.has(name.c_str())) names.append(name.c_str());
        }
    state.data["names"] = names;
    for (int i = 0; i < names.size(); ++i)
    {
        const std::string name(names[i].utf8().get_data());
        if (!ordinary(name)) continue;
        const auto attribute = prim.GetAttribute(pxr::TfToken(name));
        stage_ops::PreparedAttribute item;
        if (!attribute || !attribute.Get(&item.value)) continue;
        item.name = name;
        item.type = attribute.GetTypeName();
        item.custom = attribute.IsCustom();
        item.variability = attribute.GetVariability();
        attributes.push_back(std::move(item));
    }
    return attributes;
}
void serialize(UsdStageNode3D& carrier, const Current& state,
               const std::vector<stage_ops::PreparedAttribute>& attributes)
{
    carrier.set_meta(metadata, state.data);
    if (!state.target.IsPrimPath() || attributes.empty()) return;
    auto layer = pxr::SdfLayer::CreateAnonymous("payload_attributes");
    const godot::String text = carrier.get_meta("USD_OVERRIDE_LAYER", "");
    if (!text.is_empty() && !layer->ImportFromString(text.utf8().get_data())) return;
    {
        const pxr::SdfChangeBlock block;
        if (!stage_ops::author_attributes(layer, state.target, attributes)) return;
    }
    std::string updated;
    layer->ExportToString(&updated);
    carrier.set_meta("USD_OVERRIDE_LAYER", updated.c_str());
}
} // namespace
void initialize_attributes(UsdStageNode3D& carrier, const pxr::UsdPrim& holder)
{
    godot::Dictionary data;
    data["root"] = holder.GetStage()->GetRootLayer()->GetIdentifier().c_str();
    data["holder"] = holder.GetPath().GetString().c_str();
    data["uri"] = carrier.get_stage_uri();
    // Match the converter's strongest payload arc, including composition through references.
    for (const auto& node: holder.GetPrimIndex().GetNodeRange())
        for (const auto& layer: node.GetLayerStack()->GetLayers())
            if (const auto spec = layer->GetPrimAtPath(node.GetPath()))
                for (const auto& arc: spec->GetPayloadList().GetAppliedItems())
                {
                    data["target"] = arc.GetPrimPath().GetString().c_str();
                    carrier.set_meta(metadata, data);
                    return;
                }
    carrier.set_meta(metadata, data);
}
void prepare_attributes(UsdStageNode3D& carrier, const pxr::UsdStageRefPtr& loaded)
{
    auto state = current(carrier, loaded);
    if (!state.owner || !loaded || !state.target.IsPrimPath()) return;
    const auto attributes = collect(state);
    serialize(carrier, state, attributes);
    const pxr::SdfChangeBlock block;
    stage_ops::author_attributes(loaded->GetSessionLayer(), state.target, attributes);
}
void update_attributes(UsdStageNode3D& carrier, const std::set<std::string>& changed)
{
    auto state = current(carrier, carrier.get_stage());
    if (!state.owner) return;
    godot::PackedStringArray names = state.data.get("names", godot::PackedStringArray());
    for (const auto& name: changed)
        if (ordinary(name) && !names.has(name.c_str())) names.append(name.c_str());
    state.data["names"] = names;
    const auto attributes = collect(state);
    serialize(carrier, state, attributes);
    if (!carrier.get_stage() || !state.target.IsPrimPath() || attributes.empty()) return;
    net::model::PrimEdit edit;
    edit.kind = net::model::PrimEdit::Kind::SetAttributes;
    edit.prim_path = state.target.GetString();
    for (const auto& item: attributes)
    {
        net::model::PrimAttribute attribute;
        attribute.name = item.name;
        attribute.type_name = item.type.GetScalarType().GetAsToken().GetString();
        if (stage_ops::AttributeCodec::EncodeValue(item.type, item.value, attribute.value))
            edit.attributes.push_back(std::move(attribute));
    }
    PresentationScope scope;
    // Last operation: nested refresh can invoke callbacks that free either stage.
    if (auto* bridge = carrier.get_or_create_bridge()) bridge->author_edit(edit, net::ports::EditOrigin::External);
}
void refresh_loaded_attributes(UsdStageNode3D& carrier)
{
    update_attributes(carrier, {});
}
} // namespace idtxflow::collab::payload
