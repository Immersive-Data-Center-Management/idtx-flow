#pragma once

/**
 * @file IStageBridge.h
 * @brief Port through which the core reads and writes prim transforms without
 *        knowing about USD. Authoring an edit writes into the live stage (which
 *        is also the local save); the edit's origin decides whether the
 *        resulting stage change is reported back as a local change (Local) or
 *        suppressed from re-broadcast (External). Stage-originated changes are
 *        reported back so the core can gate and coalesce the outbound update.
 */

#include <functional>
#include <string>

#include <idtxflow/net/model/Types.h>

namespace idtxflow
{
namespace net
{
namespace ports
{
/// Where an authored edit came from. Local edits are the user's own changes and
/// must be broadcast; External edits arrive from a peer and must not be echoed
/// back out. The origin is the single switch that drives loopback suppression.
enum class EditOrigin
{
    Local,
    External
};

struct IStageBridge
{
    virtual ~IStageBridge() = default;

    /// Build the prim-path lookup for the currently loaded stage.
    virtual void build_index() = 0;

    /// Read a prim's current transform; returns false if the prim is unknown.
    virtual bool read_prim(const std::string& prim_path, model::PrimEdit& out) const = 0;

    /// Write an edit into the live stage. This is the free local save. A Local
    /// edit lets the resulting stage change report back as a local change (to be
    /// broadcast); an External edit suppresses that report (loopback suppression).
    virtual void author_edit(const model::PrimEdit& edit, EditOrigin origin) = 0;

    /// Sink the bridge invokes when a stage-originated (non-remote) change
    /// fires, letting the core gate the broadcast and coalesce the update.
    using OnChanged = std::function<void(const model::PrimEdit&)>;
    virtual void set_on_changed(OnChanged sink) = 0;

    /// Whether a change-report sink is currently installed. Lets a sink-less
    /// local-authoring path stand down when another driver owns the stage.
    virtual bool has_on_changed() const = 0;

    /// Whether `prim_path` is the stage's placement root (its defaultPrim).
    /// The root carries display-only placement, never an authored transform,
    /// so it is excluded from authoring/sync.
    virtual bool is_stage_root(const std::string& prim_path) const = 0;
};

} // namespace ports
} // namespace net
} // namespace idtxflow
