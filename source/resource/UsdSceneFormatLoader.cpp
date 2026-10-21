#include "UsdSceneFormatLoader.h"

#include <godot_cpp/classes/packed_scene.hpp>

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
    return UsdStageNode3D::get_stage_dependencies(path);
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
