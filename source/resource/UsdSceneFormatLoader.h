#pragma once

#include <godot_cpp/classes/resource_format_loader.hpp>

/**
 * Makes *.usd[a|c|z] files known to Godot as PackedScene resources: they show up in the FileSystem dock,
 * can be dragged into a scene like *.glb files, and are exported together with the scenes that instance them.
 * The loaded scene is a single UsdStageNode3D pointing at the file; the file itself stays untouched, so
 * OpenUSD opens the original bytes at runtime.
 */
class UsdSceneFormatLoader : public godot::ResourceFormatLoader
{
    GDCLASS(UsdSceneFormatLoader, godot::ResourceFormatLoader)

  protected:
    static void _bind_methods()
    {
    }

  public:
    godot::PackedStringArray _get_recognized_extensions() const override;
    bool _handles_type(const godot::StringName& type) const override;
    godot::String _get_resource_type(const godot::String& path) const override;
    godot::PackedStringArray _get_dependencies(const godot::String& path, bool add_types) const override;
    godot::Variant _load(const godot::String& path, const godot::String& original_path, bool use_sub_threads,
                         int32_t cache_mode) const override;
};
