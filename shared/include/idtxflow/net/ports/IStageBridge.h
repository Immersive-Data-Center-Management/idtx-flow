#pragma once

/**
 * @file IStageBridge.h
 * @brief Port through which the core reads and writes prim transforms without
 *        knowing about USD. Authoring a local edit writes into the live stage
 *        (which is also the local save); applying a remote edit is suppressed
 *        from re-broadcast; and stage-originated changes are reported back so the
 *        core can gate and coalesce the outbound update.
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
struct IStageBridge
{
    virtual ~IStageBridge() = default;

    /// Build the prim-path lookup for the currently loaded stage.
    virtual void build_index() = 0;

    /// Read a prim's current transform; returns false if the prim is unknown.
    virtual bool read_prim(const std::string& prim_path, model::PrimEdit& out) const = 0;

    /// Write an edit into the live stage. This is the free local save; the
    /// resulting stage change is what feeds the change-report sink below.
    virtual void author_local_edit(const model::PrimEdit& edit) = 0;

    /// Apply an inbound edit, marking it so the resulting stage change is not
    /// reported back as a local change (loopback suppression).
    virtual void apply_remote_edit(const model::PrimEdit& edit) = 0;

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
