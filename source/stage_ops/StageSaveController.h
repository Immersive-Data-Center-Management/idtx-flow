#pragma once

/**
 * @file StageSaveController.h
 * @brief Godot-aware, node-agnostic orchestration for saving/exporting a live USD
 *        stage. Resolves res://-style URIs to OS paths, applies the target guards,
 *        and delegates the flatten/export to StagePersistence. Takes a stage, not a
 *        node, so any caller holding a UsdStageRefPtr can reuse it.
 *
 * Save-as writes to a new target; overwrite rewrites a source in place, with an
 * opt-in for remote stages (whose on-disk root is a disposable download cache).
 */

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include <pxr/pxr.h>
#include <pxr/usd/usd/stage.h>

namespace idtxflow
{
namespace stage_ops
{
    /// Stateless orchestration over StagePersistence. All methods are static; the
    /// stage and target are supplied by the caller.
    class StageSaveController
    {
    public:
        /// Save As... - flatten @p stage to the NEW target @p out_uri (res://,
        /// user://, or absolute). Rejects an empty/cache-dir target. Returns OK or
        /// a Godot Error.
        static godot::Error save_as(const pxr::UsdStageRefPtr& stage,
                                    const godot::String& out_uri);

        /// Overwrite the source with a flattened snapshot. @p source_uri is the
        /// stage's original URI. A local source is flattened over its file. A
        /// remote (http/https) source is REFUSED (ERR_UNAVAILABLE) unless
        /// @p allow_remote is set, in which case the flatten overwrites only the
        /// local download-cache copy (the stage's root real-path) - a transient
        /// write that a cache reload/eviction discards; use the server commit or
        /// save_as for durable output. Returns OK or a Godot Error.
        static godot::Error save_overwrite(const pxr::UsdStageRefPtr& stage,
                                           const godot::String& source_uri,
                                           bool allow_remote = false);
    };

} // namespace stage_ops
} // namespace idtxflow
