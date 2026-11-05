#pragma once

#include <godot_cpp/classes/resource_format_loader.hpp>

/**
 * Makes *.usd[a|c|z] files droppable into a scene like *.glb files. The loaded scene is only a
 * transport: a single UsdStageNode3D pointing at the file, which UsdStageNode3D turns into a plain
 * local node when it enters the edited scene (see UsdStageNode3D::_enter_tree). So a dropped stage is
 * stored exactly like a hand-added one, and exports follow stage_uri (usd_dependency_export_plugin.gd).
 */
class UsdSceneFormatLoader : public godot::ResourceFormatLoader
{
    GDCLASS(UsdSceneFormatLoader, godot::ResourceFormatLoader)

  protected:
    static void _bind_methods()
    {
    }

  public:
    /// @return the file extensions this loader handles: usd, usda, usdc, usdz.
    godot::PackedStringArray _get_recognized_extensions() const override;

    /// @return true for "PackedScene", the only resource type this loader produces.
    bool _handles_type(const godot::StringName& type) const override;

    /// @return "PackedScene" for user *.usd* files, "" for OpenUSD's own layers below addons/*/bin/,
    ///         so those do not show up as droppable scenes.
    godot::String _get_resource_type(const godot::String& path) const override;

    /// @return the res:// sublayers, references, payloads and textures of the stage at @p path
    ///         (see UsdStageNode3D::get_stage_dependencies). http(s):// and user:// entries are skipped.
    godot::PackedStringArray _get_dependencies(const godot::String& path, bool add_types) const override;

    /// Builds a PackedScene with a single UsdStageNode3D whose stage_uri is @p path. The stage itself is
    /// not opened here; that happens when an instance enters the tree.
    godot::Variant _load(const godot::String& path, const godot::String& original_path, bool use_sub_threads,
                         int32_t cache_mode) const override;
};
