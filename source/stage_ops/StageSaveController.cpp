#include "StageSaveController.h"

#include <string>

#include <pxr/usd/sdf/layer.h>

#include <idtxflow/stage_ops/StagePersistence.h>
#include <idtxflow/utils/Logger.h>

using namespace godot;

namespace
{
    IDTX_LOG_CATEGORY("StageSaveController")

    /// Flatten `stage` to the absolute OS path `out_os`, applying the shared
    /// empty/cache-dir target guard first (bypassed when `allow_cache` is set, as
    /// the deliberate remote-overwrite path writes into the cache). Maps the
    /// result to a Godot Error and logs.
    godot::Error flatten_to(const pxr::UsdStageRefPtr& stage,
                            const std::string& out_os,
                            const std::string& cache_os,
                            bool allow_cache)
    {
        std::string err;
        const std::string guard_cache = allow_cache ? std::string() : cache_os;
        if (!idtxflow::stage_ops::StagePersistence::IsValidTarget(out_os, guard_cache, err))
        {
            IDTX_LOG(IDTX_ERROR, "save: invalid target: {}", err);
            return ERR_INVALID_PARAMETER;
        }

        const idtxflow::stage_ops::SaveResult res =
            idtxflow::stage_ops::StagePersistence::ExportFlattened(stage, out_os);
        if (!res.ok)
        {
            IDTX_LOG(IDTX_ERROR, "save: export failed: {}", res.error);
            return FAILED;
        }
        IDTX_LOG(IDTX_INFO, "save: wrote '{}'", out_os);
        return OK;
    }
}

namespace idtxflow
{
namespace stage_ops
{
    Error StageSaveController::save_as(const pxr::UsdStageRefPtr& stage, const String& out_uri)
    {
        if (!stage)
        {
            IDTX_LOG(IDTX_ERROR, "save_as: no live stage to save");
            return ERR_UNCONFIGURED;
        }
        ProjectSettings* ps = ProjectSettings::get_singleton();
        const String out_os   = ps->globalize_path(out_uri);
        const String cache_os = ps->globalize_path("user://usd_cache");
        return flatten_to(stage, out_os.utf8().get_data(), cache_os.utf8().get_data(), false);
    }

    Error StageSaveController::save_overwrite(const pxr::UsdStageRefPtr& stage,
                                              const String& source_uri, bool allow_remote)
    {
        if (!stage)
        {
            IDTX_LOG(IDTX_ERROR, "save_overwrite: no live stage to save");
            return ERR_UNCONFIGURED;
        }

        ProjectSettings* ps = ProjectSettings::get_singleton();
        const String cache_os = ps->globalize_path("user://usd_cache");

        std::string scheme_err;
        const bool local =
            StagePersistence::IsLocalWritableSource(source_uri.utf8().get_data(), scheme_err);

        if (local)
        {
            // Local source: the root layer IS the source; flatten over its file.
            const String out_os = ps->globalize_path(source_uri);
            return flatten_to(stage, out_os.utf8().get_data(), cache_os.utf8().get_data(), false);
        }

        // Remote source: refused unless the caller opts in.
        if (!allow_remote)
        {
            IDTX_LOG(IDTX_WARN,
                     "save_overwrite refused for remote source ({}); use save_as or commit the "
                     "session for durable output", scheme_err);
            return ERR_UNAVAILABLE;
        }

        // Opt-in remote overwrite: the only thing the client can overwrite is the
        // local download-cache copy (the stage's resolved root real-path). This is
        // transient - a cache reload/eviction discards it. Bypass the cache-dir
        // guard for exactly this path.
        if (!stage->GetRootLayer())
        {
            IDTX_LOG(IDTX_ERROR, "save_overwrite: stage has no root layer");
            return ERR_UNCONFIGURED;
        }
        const std::string root_os = stage->GetRootLayer()->GetRealPath();
        if (root_os.empty())
        {
            IDTX_LOG(IDTX_ERROR, "save_overwrite: root layer has no on-disk path");
            return ERR_UNAVAILABLE;
        }
        IDTX_LOG(IDTX_WARN,
                 "save_overwrite: writing the TRANSIENT download-cache copy '{}' for a remote "
                 "stage; this is discarded on cache reload/eviction - use the server commit or "
                 "save_as for durable output", root_os);
        return flatten_to(stage, root_os, cache_os.utf8().get_data(), /*allow_cache=*/true);
    }

} // namespace stage_ops
} // namespace idtxflow
