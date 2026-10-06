/**
 * @file StagePersistence.h
 * @brief Flatten a live USD stage (source layer stack + current session-layer
 *        edits) and export it to a new on-disk file. Pure OpenUSD + stdlib.
 *
 * ExportFlattened writes a new file whose content is the stage's root layer stack
 * with the session-layer edits merged on top (edits win), via
 * `UsdUtilsFlattenLayerStack` -> `SdfLayer::Export`. Composition arcs (references,
 * payloads, variants) are preserved as arcs - the output still points at external
 * sub-USDs, and a referenced sub-stage's inner content is not written. The source
 * is untouched; the target extension selects the encoding; the write is atomic.
 *
 * Takes a stage plus an absolute OS path, so it carries no host or URI-scheme
 * policy - callers resolve URIs and choose the target. It runs synchronously and
 * reads the live stage, so the caller must invoke it on the stage-owning thread.
 */
#pragma once

#include <string>

#include <pxr/pxr.h>
#include <pxr/usd/usd/stage.h>

#include <idtxflow/utils/Logger.h>

namespace idtxflow
{
namespace stage_ops
{

/// Outcome of a save/export: `ok` true on success, else `error` holds a
/// human-readable description.
struct SaveResult
{
    bool        ok = false;
    std::string error;
};

/// Flatten a live stage's composed layer stack (session edits over the source
/// root stack, arcs preserved) and export it to a new file.
class StagePersistence
{
    IDTX_LOG_CATEGORY("StagePersistence")

public:
    /**
     * @brief Export the flattened layer stack of @p stage to @p out_os_path.
     * @param stage        Live stage (session layer over the source root stack).
     * @param out_os_path  Absolute OS filesystem path of the new file to write.
     *                     The extension selects the encoding (.usda/.usdc/.usdz).
     * @return SaveResult{true,""} on success; {false,<reason>} on any failure.
     *         The target is only replaced atomically once the write fully
     *         succeeds, so a failed export never leaves a truncated file.
     */
    static SaveResult ExportFlattened(const pxr::UsdStageRefPtr& stage,
                                      const std::string& out_os_path);

    /**
     * @brief Pure, side-effect-free guard: reject targets that must never be
     *        written (empty path, or anything under a download/`.scn` cache).
     * @param out_os_path  Candidate OS path.
     * @param cache_dir_os Absolute OS path of the forbidden cache directory.
     *                     Empty disables the cache check.
     * @param out_error    Set to the reason when the target is rejected.
     * @return true when the target is acceptable to write.
     */
    static bool IsValidTarget(const std::string& out_os_path,
                              const std::string& cache_dir_os,
                              std::string& out_error);

    /**
     * @brief Pure, side-effect-free scheme check: is @p source_uri a local,
     *        writable source rather than a remote (http/https) one whose on-disk
     *        root is a disposable download cache?
     * @param source_uri A stage source URI (e.g. a local path or http(s)://).
     * @param out_error  Set to the reason when the source is not locally writable.
     * @return true for a local source; false for an http(s):// source.
     */
    static bool IsLocalWritableSource(const std::string& source_uri,
                                      std::string& out_error);

private:
    static SaveResult ExportFlat(const pxr::UsdStageRefPtr& stage,
                                 const std::string& out_os_path);

    static SaveResult ExportUsdz(const pxr::UsdStageRefPtr& stage,
                                 const std::string& out_os_path);
};

} // namespace stage_ops
} // namespace idtxflow
