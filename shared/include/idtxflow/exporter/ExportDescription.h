#pragma once

/**
 * @file ExportDescription.h
 * @brief Engine-agnostic "neutral" data structures that describe a scene to be exported to USD.
 *
 * These structs are the hand-off contract between the Godot-reading side (source/) and the
 * USD-authoring side (shared/). The Godot side fills them by walking the scene tree; the
 * shared UsdStageExporter consumes them to author USD prims. They must NOT reference any
 * godot:: type directly (the only exception is `Types::MeshData`, a copyable value type that
 * carries mesh arrays across the boundary — see the note in docs/EXPORTER_DESIGN.md §4).
 *
 * Keeping this data neutral is also what makes a future async exporter possible: the structs
 * contain no scene-graph pointers, so authoring could run on a worker thread.
 */

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec4f.h>

#include "../types/MaterialTypes.h"
#include "../types/TargetTypes.h"

namespace idtxflow
{
namespace exporter
{
    /**
     * A single texture referenced by a material channel, captured from the engine as raw image
     * bytes so the exporter can write a self-contained asset (image files next to the stage).
     */
    struct TextureExportRef
    {
        std::string sourceImagePath;      // original on-disk path, if known (may be empty)
        std::vector<uint8_t> imageBytes;  // encoded image bytes (e.g. PNG) to be written out
        std::string extension = ".png";   // file extension incl. leading dot
        pxr::GfVec2f uvScale = { 1, 1 };
        pxr::GfVec2f uvOffset = { 0, 0 };
    };

    /**
     * Neutral description of one material, resolved from an engine material into channel values.
     * Scalar/colour channels and texture channels are keyed by the shared MaterialChannel enum.
     */
    struct MaterialExportDescription
    {
        std::string name;  // sanitized material prim name
        // Constant values: colours use all four components, single floats use .x (e.g. metallic).
        std::map<types::MaterialChannel, pxr::GfVec4f> scalarChannels;
        std::map<types::MaterialChannel, TextureExportRef> textureChannels;
        float opacityThreshold = 0.0f;
    };

    /**
     * Neutral description of one node in the exported hierarchy. A tree of these mirrors the
     * exported scene sub-tree. `kind` selects which USD prim schema the exporter authors.
     */
    template<typename TargetEngine> requires idtxflow::types::ValidTargetEngine<TargetEngine>
    struct NodeExportDescription
    {
        using Types = idtxflow::types::TargetEngineTypes<TargetEngine>;

        enum class Kind { Xform, Mesh, Skeleton };

        std::string primName;                          // sanitized, valid USD identifier
        std::optional<std::string> originalPrimPath;   // present if the node came from a USD import
        std::optional<std::string> originalPrimType;   // present if the node came from a USD import

        pxr::GfMatrix4d localTransform = pxr::GfMatrix4d(1.0);  // node-local transform (identity default)

        Kind kind = Kind::Xform;

        // Only meaningful for Kind::Mesh. One MeshData per surface; materials are index-aligned.
        std::vector<typename Types::MeshData> surfaces;
        std::vector<std::optional<MaterialExportDescription>> surfaceMaterials;

        std::vector<NodeExportDescription<TargetEngine>> children;
    };

    /**
     * Output options for a single export run. The stage file format (ascii/binary/package) is
     * chosen by USD from the extension of `outputStagePath` (.usda / .usdc / .usdz).
     */
    struct StageExportOptions
    {
        std::string outputStagePath;   // absolute path; extension decides the format
        std::string textureOutputDir;  // absolute dir where texture files are written
        bool upAxisY = true;           // true => author Y-up, metersPerUnit=1 (no per-node fixups)
    };
}
}
