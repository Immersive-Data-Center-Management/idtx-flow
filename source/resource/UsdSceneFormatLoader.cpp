#include "UsdSceneFormatLoader.h"

#include <godot_cpp/classes/packed_scene.hpp>

#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usdUtils/dependencies.h>

#include <idtxflow_godot/nodes/UsdStageNode3D.h>

using namespace godot;

static bool is_usd_file(const String& path)
{
    // shortcut: OpenUSD's own schema/plugin layers ship under addons/*/bin/, they are not user assets
    if (path.begins_with("res://addons/") && path.contains("/bin/")) return false;
    const String ext = path.get_extension().to_lower();
    return ext == "usd" || ext == "usda" || ext == "usdc" || ext == "usdz";
}

PackedStringArray UsdSceneFormatLoader::_get_recognized_extensions() const
{
    return PackedStringArray({"usd", "usda", "usdc", "usdz"});
}

bool UsdSceneFormatLoader::_handles_type(const StringName& type) const
{
    return type == StringName("PackedScene");
}

String UsdSceneFormatLoader::_get_resource_type(const String& path) const
{
    return is_usd_file(path) ? "PackedScene" : "";
}

PackedStringArray UsdSceneFormatLoader::_get_dependencies(const String& path, bool /*add_types*/) const
{
    // Sublayers, references, payloads and textures, so "export selected scenes" ships them too.
    // Our resolver keeps res:// identifiers, so the reported paths are already project paths.
    std::vector<pxr::SdfLayerRefPtr> layers;
    std::vector<std::string> assets;
    std::vector<std::string> unresolved;
    pxr::UsdUtilsComputeAllDependencies(pxr::SdfAssetPath(path.utf8().get_data()), &layers, &assets, &unresolved);

    PackedStringArray deps;
    auto add = [&](const std::string& p) {
        const String dep(p.c_str());
        // http/user:// dependencies are fetched at runtime and must not be exported
        if (dep.begins_with("res://") && dep != path && !deps.has(dep)) deps.push_back(dep);
    };
    for (const pxr::SdfLayerRefPtr& layer: layers)
    {
        add(layer->GetIdentifier());
    }
    for (const std::string& asset: assets)
    {
        add(asset);
    }
    return deps;
}

Variant UsdSceneFormatLoader::_load(const String& path, const String& /*original_path*/, bool /*use_sub_threads*/,
                                    int32_t /*cache_mode*/) const
{
    // Only store the uri: the stage opens once an instance enters the tree, not while loading the resource.
    UsdStageNode3D* root = memnew(UsdStageNode3D);
    root->set_name(path.get_file().get_basename());
    root->set_stage_uri(path);

    Ref<PackedScene> scene;
    scene.instantiate();
    const Error err = scene->pack(root);
    memdelete(root);
    if (err != OK) return Variant();
    return scene;
}
