/**
 * @file UsdExporter.cpp
 * @brief Implementation of the Godot-callable USD export entry point.
 *
 * Translates the Godot call arguments into neutral StageExportOptions, runs the scene-tree
 * extraction, and hands the result to the engine-agnostic UsdStageExporter which writes the file.
 */

#include "UsdExporter.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <idtxflow/exporter/ExportDescription.h>
#include <idtxflow/exporter/UsdStageExporter.h>
#include <idtxflow_godot/types/GodotTypes.h>

#include "SceneTreeExtractor.h"

using namespace godot;

void UsdExporter::_bind_methods()
{
    // Static so GDScript can call `UsdExporter.export_node(...)` directly on the class.
    ClassDB::bind_static_method(
        "UsdExporter",
        D_METHOD("export_node", "root", "uri", "options"),
        &UsdExporter::export_node);
}

bool UsdExporter::export_node(Node3D* root, const String& uri, const Dictionary& options)
{
    if (root == nullptr)
    {
        UtilityFunctions::push_error("UsdExporter.export_node: root node is null.");
        return false;
    }
    if (uri.is_empty())
    {
        UtilityFunctions::push_error("UsdExporter.export_node: output uri is empty.");
        return false;
    }

    idtxflow::exporter::StageExportOptions export_options;
    export_options.outputStagePath = uri.utf8().get_data();

    const String texture_dir = String(options.get("texture_dir", uri.get_base_dir()));
    export_options.textureOutputDir = texture_dir.utf8().get_data();
    export_options.upAxisY = bool(options.get("up_axis_y", true));

    // Read the Godot scene sub-tree into neutral descriptions (Godot side).
    idtxflow_godot::SceneTreeExtractor::NodeDesc root_desc =
        idtxflow_godot::SceneTreeExtractor::Extract(root);

    // Author and write the USD stage (engine-agnostic side).
    idtxflow::exporter::UsdStageExporter<idtxflow::types::TargetEngineGodot> exporter;
    return exporter.Export(root_desc, export_options);
}
