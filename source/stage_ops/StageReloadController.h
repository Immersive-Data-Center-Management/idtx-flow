#pragma once

/**
 * @file StageReloadController.h
 * @brief Rebuild a session's stage nodes after a file they use changed on the server, or after a reconnect.
 *
 * One controller serves every session of the client. The host (IdtxClient) only forwards: the engine's
 * on_layer_reloaded / on_reconnected, the per-frame tick and session unbinding. This controller selects the
 * affected stage nodes, releases and reloads them in the right order, and tracks their loads until each has ended,
 * then reports the session as complete so the host can call CollabEngine::reload_complete().
 *
 * Stage nodes report the end of a load with stage_loading_finished or stage_loading_cancelled. Godot delivers
 * those only to a Godot object, so the host passes two unbound Callables on itself and forwards their calls to
 * on_node_finished / on_node_cancelled (the same borrowing pattern StageActivityController uses with its stage
 * node). The controller binds the session id and node id to them.
 *
 * All operations run on the Godot main thread.
 */

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>

#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot
{
class Node;
}

namespace idtxflow::collab
{
class StageReloadController
{
  public:
    /// Reported once a session has no rebuild left to wait for.
    using CompleteFn = std::function<void(const std::string& session_id)>;

    /// @p finished / @p cancelled: unbound Callables the host routes to on_node_finished / on_node_cancelled.
    StageReloadController(godot::Callable finished, godot::Callable cancelled, CompleteFn on_complete);
    StageReloadController(const StageReloadController&) = delete;
    StageReloadController& operator=(const StageReloadController&) = delete;

    /// A file the session's stage uses changed: evict it from the HTTP cache and rebuild every stage node below
    /// @p session_root that composes it. @p session_root may be null (session not bound to a node).
    void reload_file(const std::string& session_id, godot::Node* session_root, const std::string& file_url);

    /// The session reconnected: any of its files may have changed meanwhile, so evict every remote file its stage
    /// nodes use and rebuild the session's root node. @p session_root may be null.
    void rebuild_session(const std::string& session_id, godot::Node* session_root);

    /// Forwarded stage_loading_finished of a node being rebuilt.
    void on_node_finished(bool success, const godot::String& session_id, int64_t node_id);
    /// Forwarded stage_loading_cancelled of a node being rebuilt.
    void on_node_cancelled(const godot::String& session_id, int64_t node_id);

    /// Per frame: complete every session left with no rebuild to wait for (the last one was cancelled).
    void poll();

    /// Stop rebuilding for @p session_id (its node was unbound). Returns whether a rebuild was pending, in which
    /// case the host still owes the engine its reload_complete().
    bool forget(const std::string& session_id);

  private:
    // Rebuild one stage node and count it until its load ends.
    void start_node_reload(const std::string& session_id, godot::Node* node);
    // Report @p session_id as complete if it has nothing left to wait for.
    void complete_if_idle(const std::string& session_id);

    // Per session: the stage nodes (by ObjectID) being rebuilt, and those to rebuild once more when they finish
    // because another file was reloaded meanwhile. Erased when a session has none left.
    struct ReloadState
    {
        std::set<uint64_t> loading;
        std::set<uint64_t> again;
    };
    std::map<std::string, ReloadState> reloads_;

    godot::Callable finished_;
    godot::Callable cancelled_;
    CompleteFn on_complete_;
};
} // namespace idtxflow::collab
