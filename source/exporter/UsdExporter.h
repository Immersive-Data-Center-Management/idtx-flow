#pragma once

/**
 * @file UsdExporter.h
 * @brief Godot-callable entry point for exporting a scene sub-tree to a USD file.
 *
 * Registered as a Godot class exposing a single static method, so it can be invoked from GDScript
 * (and the editor plugin) as `UsdExporter.export_node(root, uri, options)` without instantiation.
 * It wires together the two exporter halves: SceneTreeExtractor (reads the Godot tree) and
 * UsdStageExporter (authors and writes the USD stage).
 */

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

class UsdExporter : public godot::Object
{
    GDCLASS(UsdExporter, Object)

public:
    /**
     * Export `root` (and all its Node3D children) to a USD file at `uri`.
     * @param root    The sub-tree root to export.
     * @param uri     Absolute output file path; the extension (.usda/.usdc/.usdz) selects the format.
     * @param options Optional keys: "texture_dir" (String), "up_axis_y" (bool, default true).
     * @return true on success.
     */
    static bool export_node(godot::Node3D* root, const godot::String& uri, const godot::Dictionary& options);

protected:
    static void _bind_methods();
};
