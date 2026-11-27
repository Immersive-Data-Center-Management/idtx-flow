#include <idtxflow/stage_ops/PrimCreateSpecBuilder.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>

#include <idtxflow/resolver/UsdAssetPath.h>
#include <idtxflow/resolver/UrlCanonical.h>
#include <idtxflow/stage_ops/AttributeCodec.h>
#include <pxr/usd/sdf/attributeSpec.h>
#include <pxr/usd/sdf/listOp.h>
#include <pxr/usd/usdGeom/xformable.h>

namespace idtxflow::stage_ops
{
namespace
{
using namespace pxr;

PrimCreateSpecResult failure(const char* error, const std::string& message)
{
    PrimCreateSpecResult result;
    result.error = error;
    result.message = message;
    return result;
}

// Inspect identifier paths, never resolved cache filenames. URI paths remain
// escaped here; an application boundary may require an unescaped file spelling.
bool identifier_path(const std::string& identifier, std::string& origin, std::filesystem::path& path)
{
    const auto scheme = identifier.find("://");
    std::string part = identifier;
    if (scheme != std::string::npos)
    {
        if (identifier.find_first_of("?#") != std::string::npos) return false;
        const auto start = identifier.find_first_not_of('/', scheme + 3);
        if (start == std::string::npos) return false;
        origin = identifier.substr(0, scheme + 3);
        // Keep the authority, when present, as a path component. Resolver URIs
        // need not have one; a round-trip below verifies the resulting identity.
        part = "/" + identifier.substr(start);
        if (part.find('@') != std::string::npos) return false;
    }
    if (part.find_first_of("[]") != std::string::npos) return false;
    path = std::filesystem::u8path(part).lexically_normal();
    return scheme != std::string::npos ? path.has_root_directory() : path.is_absolute();
}

bool rebase_asset(const std::string& value, SdfLayerHandle layer, const UsdStageRefPtr& stage, std::string& out)
try
{
    if (value.empty())
    {
        out.clear(); // Empty asset-valued defaults are legitimate.
        return true;
    }
    const auto root = stage->GetRootLayer();
    if (layer == stage->GetSessionLayer()) layer = root;
    if (!layer || !root || root->IsAnonymous()) return false;
    const std::string anchored = resolver::AnchorAssetIdentifier(value, layer, stage->GetPathResolverContext());
    std::string root_origin, asset_origin;
    std::filesystem::path root_path, asset_path;
    const auto canonical = [](const std::string& s) {
        return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0 ? resolver::CanonicalUrl(s) : s;
    };
    if (!identifier_path(canonical(root->GetIdentifier()), root_origin, root_path) ||
        !identifier_path(canonical(anchored), asset_origin, asset_path) || root_origin != asset_origin)
        return false;
    const auto relative = asset_path.lexically_relative(root_path.parent_path());
    if (relative.empty() || relative.is_absolute()) return false;
    const auto bytes = relative.generic_u8string();
    std::string candidate(bytes.begin(), bytes.end());
    if (candidate.rfind("../", 0) != 0) candidate = "./" + candidate;
    // Lexical rebasing alone cannot prove resolver semantics. In particular, a
    // relative identifier must never cross a URI authority or become a cache path.
    std::string round_trip_origin;
    std::filesystem::path round_trip_path;
    const auto round_trip = resolver::AnchorAssetIdentifier(candidate, root, stage->GetPathResolverContext());
    if (!identifier_path(canonical(round_trip), round_trip_origin, round_trip_path) ||
        round_trip_origin != asset_origin || round_trip_path != asset_path)
        return false;
    out = std::move(candidate);
    return true;
}
catch (const std::filesystem::filesystem_error&)
{
    return false; // Invalid path encodings are a capture error, not an exception at the API boundary.
}

template <typename Arc>
void compose_arcs(const SdfPrimSpecHandleVector& specs, const TfToken& field, std::vector<Arc>& arcs,
                  std::map<Arc, SdfLayerHandle>& origins)
{
    // Compose the local layer stack weak-to-strong, retaining the layer of each
    // introduced item. Ordering/deletion operations do not change its provenance.
    for (auto it = specs.rbegin(); it != specs.rend(); ++it)
    {
        const VtValue value = (*it)->GetInfo(field);
        if (!value.IsHolding<SdfListOp<Arc>>()) continue;
        value.UncheckedGet<SdfListOp<Arc>>().ApplyOperations(
            &arcs, [&](SdfListOpType op, const Arc& arc) -> std::optional<Arc> {
                if (op != SdfListOpTypeDeleted && op != SdfListOpTypeOrdered) origins[arc] = (*it)->GetLayer();
                return arc;
            });
    }
}

bool local_layer(const SdfLayerHandleVector& layers, const SdfLayerHandle& layer)
{
    return std::find(layers.begin(), layers.end(), layer) != layers.end();
}

SdfPropertySpecHandle default_spec(const UsdAttribute& attribute, const SdfLayerHandleVector& layers, bool local_only)
{
    if (!attribute) return {};
    for (const auto& spec: attribute.GetPropertyStack(UsdTimeCode::Default()))
        if ((!local_only || local_layer(layers, spec->GetLayer())) && spec->HasInfo(SdfFieldKeys->Default)) return spec;
    return {};
}

bool dimension(const std::string& type, const std::string& name)
{
    if (type == "Cube") return name == "size";
    if (type == "Sphere") return name == "radius";
    if (type == "Plane") return name == "width" || name == "length" || name == "axis";
    if (type == "Cone" || type == "Cylinder" || type == "Capsule")
        return name == "radius" || name == "height" || name == "axis";
    return false;
}
} // namespace

PrimCreateSpecResult PrimCreateSpecBuilder::Build(const pxr::UsdStageRefPtr& stage, const pxr::SdfPath& path)
{
    if (!stage) return failure("stage_unavailable", "Load the stage before capturing a prim.");
    if (!path.IsAbsolutePath() || !path.IsPrimPath())
        return failure("invalid_source", "Select an absolute prim path, not the stage pseudo-root or a property.");
    const auto prim = stage->GetPrimAtPath(path);
    if (!prim || !prim.IsActive() || !prim.IsDefined())
        return failure("invalid_source", "The source must be an active, defined prim.");
    if (prim.IsInstanceProxy() || prim.IsInstance())
        return failure("unsupported_source", "Instance semantics cannot be reproduced by a single Create.");

    const auto layers = stage->GetLayerStack();
    SdfPrimSpecHandleVector local_specs;
    for (const auto& spec: prim.GetPrimStack())
    {
        if (!local_layer(layers, spec->GetLayer())) continue;
        if (spec->GetPath() != path)
            return failure("unsupported_source", "Variant or mapped composition cannot be reproduced by Create.");
        if (spec->GetInheritPathList().HasKeys() || spec->GetSpecializesList().HasKeys() ||
            !spec->GetVariantSelections().empty())
            return failure("unsupported_source", "Inherits, specializes and variant selections are not captured.");
        local_specs.push_back(spec);
    }
    std::vector<SdfReference> references;
    std::vector<SdfPayload> payloads;
    std::map<SdfReference, SdfLayerHandle> reference_origins;
    std::map<SdfPayload, SdfLayerHandle> payload_origins;
    compose_arcs(local_specs, SdfFieldKeys->References, references, reference_origins);
    compose_arcs(local_specs, SdfFieldKeys->Payload, payloads, payload_origins);
    if (references.size() + payloads.size() > 1)
        return failure("unsupported_source", "Create can reproduce only one external reference or payload.");

    PrimCreateSpecResult result;
    auto& spec = result.spec;
    const bool holder = !references.empty() || !payloads.empty();
    if (holder)
    {
        TfToken local_type, inherited_type;
        for (const auto& entry: prim.GetPrimStack())
        {
            auto& type = local_layer(layers, entry->GetLayer()) ? local_type : inherited_type;
            if (type.IsEmpty()) type = entry->GetTypeName();
        }
        if (!local_type.IsEmpty() && local_type != inherited_type)
            return failure("unsupported_source",
                           "Create cannot preserve a holder type override, or verify it on an unloaded asset.");
        spec.payload = !payloads.empty();
        const std::string asset = spec.payload ? payloads.front().GetAssetPath() : references.front().GetAssetPath();
        const SdfPath target = spec.payload ? payloads.front().GetPrimPath() : references.front().GetPrimPath();
        const SdfLayerOffset offset =
            spec.payload ? payloads.front().GetLayerOffset() : references.front().GetLayerOffset();
        if (asset.empty() || offset != SdfLayerOffset() ||
            (!target.IsEmpty() && (!target.IsAbsolutePath() || !target.IsPrimPath())) ||
            (!spec.payload && !references.front().GetCustomData().empty()))
            return failure("unsupported_source",
                           "Internal arcs, layer offsets and reference custom data are not captured.");
        const auto layer =
            spec.payload ? payload_origins.at(payloads.front()) : reference_origins.at(references.front());
        if (!rebase_asset(asset, layer, stage, spec.reference_path))
            return failure("unrepresentable_asset", "The asset arc cannot be expressed relative to this stage root.");
        if (!target.IsEmpty()) spec.reference_path += "<" + target.GetString() + ">";
        result.warnings.push_back("Only this holder's defaults are captured; descendant overrides are excluded and "
                                  "asset files remain shared.");
    }
    else
    {
        spec.type = prim.GetTypeName().GetString();
        static const std::set<std::string> supported{"Xform", "Cube", "Sphere", "Cone", "Cylinder", "Capsule", "Plane"};
        if (!supported.count(spec.type))
            return failure("unsupported_source", "The source type is not supported by Create.");
        if (!prim.GetAllChildren().empty()) result.warnings.push_back("Child prims are not copied.");
    }

    // An arc-only holder must retain inherited asset placement. A local order
    // opinion (including an explicitly empty order) is a placement override.
    const UsdGeomXformable xform(prim);
    const auto order = prim.GetAttribute(TfToken("xformOpOrder"));
    bool reset = false;
    const auto operations = xform.GetOrderedXformOps(&reset);
    const bool local_order = bool(default_spec(order, layers, true));
    const bool local_operation = std::any_of(operations.begin(), operations.end(), [&](const auto& operation) {
        return bool(default_spec(operation.GetAttr(), layers, true));
    });
    const bool placement = !holder || local_order || local_operation;
    if (placement && (xform || order))
    {
        for (const auto& operation: operations)
        {
            VtValue value;
            if (!operation.GetAttr().Get(&value, UsdTimeCode::Default()))
                return failure("unsupported_source", "An active transform operation has no default value.");
        }
        GfMatrix4d matrix(1.0);
        if (!xform.GetLocalTransformation(&matrix, &reset, UsdTimeCode::Default()) || reset)
            return failure("unsupported_source",
                           "The source transform cannot be reproduced by a parent-local Create matrix.");
        for (size_t i = 0; i < 16; ++i)
        {
            const double component = matrix[i / 4][i % 4];
            if (!std::isfinite(component))
                return failure("unsupported_source", "The source transform contains non-finite values.");
            spec.transform.m[i] = component;
        }
        if (matrix[0][3] != 0 || matrix[1][3] != 0 || matrix[2][3] != 0 || matrix[3][3] != 1)
            return failure("unsupported_source", "Projective transforms cannot be represented by the Create binding.");
        spec.has_transform = true;
    }

    for (const auto& attribute: prim.GetAttributes())
    {
        const std::string name = attribute.GetName().GetString();
        if (attribute.GetNumTimeSamples()) result.warnings.push_back("Animation samples excluded: " + name);
        if (name == "xformOpOrder" || name.rfind("xformOp:", 0) == 0) continue;
        if (attribute.HasAuthoredConnections()) result.warnings.push_back("Connections excluded: " + name);
        for (const auto& metadata: attribute.GetAllAuthoredMetadata())
            if (metadata.first != SdfFieldKeys->Default && metadata.first != SdfFieldKeys->TypeName &&
                metadata.first != SdfFieldKeys->Custom && metadata.first != SdfFieldKeys->Variability)
                result.warnings.push_back("Property metadata excluded: " + name + ":" + metadata.first.GetString());
        const auto source = default_spec(attribute, layers, holder);
        if (!source && (holder || !dimension(spec.type, name))) continue;
        VtValue value;
        if (source)
            value = source->GetInfo(SdfFieldKeys->Default);
        else
            attribute.Get(&value, UsdTimeCode::Default());
        net::model::PrimAttribute captured;
        captured.name = name;
        const auto type = attribute.GetTypeName();
        captured.type_name = type.GetScalarType().GetAsToken().GetString();
        if (!AttributeCodec::EncodeValue(type, value, captured.value))
        {
            if (dimension(spec.type, name))
                return failure("unsupported_source", "A required dimension has no representable default: " + name);
            result.warnings.push_back("Unsupported or blocked default excluded: " + name);
            continue;
        }
        if (captured.type_name == "asset")
        {
            for (auto& asset: captured.value.strings)
                if (!rebase_asset(asset, source ? source->GetLayer() : stage->GetRootLayer(), stage, asset))
                    return failure("unrepresentable_asset",
                                   "Asset default cannot be expressed relative to this stage root: " + name);
        }
        spec.attributes.push_back(std::move(captured));
    }
    for (const auto& relationship: prim.GetRelationships())
        if (relationship.IsAuthored())
            result.warnings.push_back("Relationship excluded: " + relationship.GetName().GetString());
    for (const auto& entry: prim.GetAllAuthoredMetadata())
    {
        const auto& key = entry.first;
        if (key != SdfFieldKeys->TypeName && key != SdfFieldKeys->Specifier && key != SdfFieldKeys->Active &&
            key != SdfFieldKeys->References && key != SdfFieldKeys->Payload)
            result.warnings.push_back("Prim metadata excluded: " + key.GetString());
    }
    std::sort(spec.attributes.begin(), spec.attributes.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    std::sort(result.warnings.begin(), result.warnings.end());
    result.warnings.erase(std::unique(result.warnings.begin(), result.warnings.end()), result.warnings.end());
    result.ok = true;
    return result;
}
} // namespace idtxflow::stage_ops
