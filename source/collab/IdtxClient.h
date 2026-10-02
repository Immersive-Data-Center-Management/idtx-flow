#pragma once

/**
 * @file IdtxClient.h
 * @brief Godot binding for the collaboration client.
 *
 * This is the only engine-specific part of the client. It is a Node that
 * implements CollabObserver, owns the engine-agnostic CollabEngine plus the
 * concrete Godot/IX/USD adapters, converts between engine model types and Godot
 * Variants, and emits signals. It contains no networking, protocol, or USD
 * logic — all of that lives behind the engine and its ports.
 *
 * Reachable as the global engine singleton "IdtxClient" from editor tools and
 * runtime scripts.
 */

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <idtxflow/net/CollabEngine.h>
#include <idtxflow/net/CollabComposition.h>
#include <idtxflow/net/CollabObserver.h>
#include <idtxflow/net/protocol/RestClient.h>
#include <idtxflow/net/protocol/SessionSocket.h>
#include <idtxflow/net/ports/ITransportFactory.h>

#include "Dispatcher.h"
#include "Ticker.h"

namespace idtxflow { namespace collab { class StageBridge; } }

class IdtxClient : public godot::Node, public idtxflow::net::CollabObserver
{
    GDCLASS(IdtxClient, godot::Node)

public:
    IdtxClient();
    ~IdtxClient() override;

    static IdtxClient* get_singleton() { return singleton_; }
    static void set_singleton(IdtxClient* s) { singleton_ = s; }

    /// Construct the adapters and start the engine. Idempotent. The transport factory
    /// (injected by the composition root) creates the concrete HTTP / WebSocket transports
    void initialize(std::unique_ptr<idtxflow::net::ports::ITransportFactory> transport_factory);
    /// Stop the engine, drop adapters, and detach the singleton. Idempotent.
    void shutdown();

    // --- Configuration ---
    
    void set_base_url(const godot::String& url);
    godot::String get_base_url() const;

    // --- Auth state ---
    
    bool is_authenticated() const;
    godot::String get_access_token() const;
    void clear_credentials();

    // --- REST operations (async) ---
    // Each request delivers its single result to the optional `on_done` Callable:
    // a Dictionary { "ok": true, "result": <payload> } on success, or
    // { "ok": false, "http_code": int, "error_code": String, "message": String }
    // on failure.
    
    // Authenticate a username/password and store the returned token for later
    // authenticated calls. result: { access_token: String, expires_in: int }
    void login(const godot::String& username, const godot::String& password,
               const godot::Callable& on_done = godot::Callable());
    // Probe backend reachability. result: { http_code: int } on a healthy
    // response; error dict otherwise.
    void health(const godot::Callable& on_done = godot::Callable());
    // Fetch a server thumbnail image for a USD file (cached in the core by
    // usd_file). result: { bytes: PackedByteArray, content_type: String };
    // error dict incl. 404 when not generated yet.
    void fetch_thumbnail(const godot::String& usd_file, const godot::Callable& on_done = godot::Callable());
    // List available server USD files, optionally filtered by name/extension.
    // result: Array of file dicts { filepath, filename, directory, size,
    // modified, modified_epoch }.
    void list_files(const godot::String& name_contains = "", const godot::String& extension = "",
                    const godot::Callable& on_done = godot::Callable());
    // List currently active collaboration sessions. result: Array of session
    // dicts { session_id, usd_file, mode, client_count, created_at, ws_url,
    // protocol }.
    void list_sessions(const godot::Callable& on_done = godot::Callable());
    // Retrieve details for one session. result: a single session dict (same
    // fields as list_sessions); error dict incl. 404 not_found.
    void get_session(const godot::String& session_id, const godot::Callable& on_done = godot::Callable());
    // Commit a session's overrides back into the original USD file. result:
    // { session_id: String, committed: bool }; error dict incl. 409
    // nothing_to_commit.
    void commit_session(const godot::String& session_id, const godot::Callable& on_done = godot::Callable());
    // Check whether a USD file exists on the backend (a lightweight probe, no
    // download). result: { exists: bool } (a definitive "no" resolves as
    // exists:false, not an error).
    void check_download_exists(const godot::String& usd_file, const godot::Callable& on_done = godot::Callable());
    // Check whether a thumbnail exists for a USD file (a lightweight probe, no
    // download). result: { exists: bool } (a definitive "no" resolves as
    // exists:false, not an error).
    void check_thumbnail_exists(const godot::String& usd_file, const godot::Callable& on_done = godot::Callable());

    // --- high-level session flow ---
    
    // Create a new collaboration session for a file, then enter it. `on_done`
    // (optional) reports the per-request completion once the session is created and
    // its socket is open: { "ok": true, "result": { session_id, usd_file, mode,
    // ws_url, stage_url } } on success, or the failure dict. The caller then loads
    // the stage from `stage_url` and calls bind_session. `end_session` emits
    // `session_closed`.
    void open_new_session(const godot::String& usd_file, const godot::String& mode = "single_edit",
                          bool auto_commit = false,
                          const godot::Callable& on_done = godot::Callable());
    // Join an existing collaboration session by id, then enter it. Same per-request
    // completion as open_new_session (`on_done` result with session_id + stage_url +
    // ws_url), but performs a session lookup instead of a create.
    void open_existing_session(const godot::String& session_id,
                               const godot::Callable& on_done = godot::Callable());
    void end_session(const godot::String& session_id);

    // --- URL helpers (sync) ---
    
    godot::String download_url(const godot::String& usd_file) const;
    godot::String ws_base_url() const;

    // --- WebSocket session ---
    
    // Whether the given session's socket is open (false for an unknown id).
    bool is_socket_open(const godot::String& session_id) const;

    // Whether the given session has completed its join snapshot.
    bool is_session_synced(const godot::String& session_id) const;

    // --- Session sync binding (per session) ---

    // Bind a live collaboration session to a stage node: the client owns the sync
    // from here — it attaches the bridge now (if the node's stage is loaded) or
    // when it next loads, and keeps it attached across the node's tree lifecycle
    // `remote` enables inbound apply + outbound broadcast (after arming).
    void bind_session(const godot::String& session_id, godot::Node* stage_node, bool remote);
    // Stop syncing a session: detach + drop the bridge and forget the binding.
    // Does not end the session or close its socket (see end_session).
    void unbind_session(const godot::String& session_id);
    // The prim node is the session-agnostic outbound origin (a gizmo edit). The
    // binding resolves which session's stage the node belongs to and routes the
    // edit there, so callers (the USD nodes) need not know the session id.
    void notify_local_transform_changed(godot::Node* node);

    // --- CollabObserver ---
    
    void on_login_ok(const idtxflow::net::model::LoginResult& result) override;
    void on_health(const idtxflow::net::model::HealthResult& result) override;
    void on_thumbnail(const idtxflow::net::model::ThumbnailResult& result) override;
    void on_files(const std::vector<idtxflow::net::model::FileEntry>& files) override;
    void on_session_created(const idtxflow::net::model::SessionInfo& session) override;
    void on_sessions(const std::vector<idtxflow::net::model::SessionInfo>& sessions) override;
    void on_session_details(const idtxflow::net::model::SessionInfo& session) override;
    void on_session_committed(const idtxflow::net::model::CommitResult& result) override;
    void on_download_exists(const std::string& usd_file, bool exists) override;
    void on_thumbnail_exists(const std::string& usd_file, bool exists) override;
    void on_request_failed(idtxflow::net::Op op, const idtxflow::net::model::RestError& error) override;
    void on_session_ready(const idtxflow::net::model::SessionInfo& session,
                          const std::string& stage_url, const std::string& ws_url) override;
    void on_session_closed(const std::string& session_id) override;
    void on_socket_opened(const std::string& session_id) override;
    void on_handshake(const std::string& session_id, const std::string& usd_path,
                      const std::string& usd_uri) override;
    void on_remote_edit(const std::string& session_id, const idtxflow::net::model::PrimEdit& edit,
                        const std::string& from_client_id) override;
    void on_snapshot_complete(const std::string& session_id) override;
    void on_ack(const std::string& session_id, bool ok, const std::string& error) override;
    void on_socket_error(const std::string& session_id,
                         const std::string& code, const std::string& message) override;
    void on_disconnected(const std::string& session_id,
                         idtxflow::net::model::CloseReason reason, int code,
                         const std::string& text) override;

protected:
    static void _bind_methods();

private:
    IDTX_LOG_CATEGORY("IdtxClient")

    // Bound methods: the dispatcher drain (call_deferred target), the per-frame
    // tick (process_frame signal target), and a deferred bootstrap that retries
    // the process_frame connection until the SceneTree exists.
    void _drain_dispatch();
    void _on_process_frame();
    void _bootstrap_ticker();

    // Tree-lifecycle slots for a session's stage node. The stage node rebuilds
    // its USD stage + child nodes on its own _exit_tree/_enter_tree cycle, so the
    // bridge is dropped when the stage unloads and rebuilt when it loads again,
    // otherwise it would outlive (or point past) the stage it wraps.
    void _on_stage_unloading(const godot::String& session_id);
    void _on_stage_loaded(bool success, const godot::String& session_id);
    // Build the StageBridge for a session's node and hand it to the engine. This
    // is the general "start the bridge" step; detach_bridge is its inverse.
    void attach_bridge(const std::string& session_id);
    void detach_bridge(const std::string& session_id);

    // Per-request completion queues (FIFO). A request enqueues its Callable (only
    // if valid) when issued; the matching observer result pops the front and
    // invokes it with the result Dictionary, skipping a target that has since
    // been freed. Each op has its own queue so concurrent requests of different
    // kinds never cross-talk.
    void resolve_next(std::vector<godot::Callable>& queue, const godot::Dictionary& result);
    godot::Dictionary make_error_result(int http_code, const godot::String& error_code,
                                        const godot::String& message) const;

    std::vector<godot::Callable> login_cbs_;
    std::vector<godot::Callable> health_cbs_;
    std::vector<godot::Callable> thumbnail_cbs_;
    std::vector<godot::Callable> list_cbs_;
    std::vector<godot::Callable> create_cbs_;
    std::vector<godot::Callable> join_cbs_;
    std::vector<godot::Callable> bindings_cbs_;
    std::vector<godot::Callable> session_details_cbs_;
    std::vector<godot::Callable> commit_cbs_;
    std::vector<godot::Callable> download_exists_cbs_;
    std::vector<godot::Callable> thumbnail_exists_cbs_;

    static IdtxClient* singleton_;

    bool initialized_ = false;

    std::unique_ptr<idtxflow::collab::Dispatcher>                 dispatcher_;
    std::unique_ptr<idtxflow::collab::Ticker>                     ticker_;
    std::unique_ptr<idtxflow::net::ports::ITransportFactory>      transport_factory_;
    idtxflow::net::AgnosticTransports                             transports_;

    // Per-session sync state, keyed by session id. One record owns the node it is
    // bound to (by ObjectID, stable across the node's _exit_tree/_enter_tree
    // cycle), the remote flag, and the live StageBridge (null while the node's stage is unloaded).
    struct SessionBinding
    {
        uint64_t node_id = 0;          // Godot ObjectID of the bound UsdStageNode3D
        bool     remote  = false;
        std::unique_ptr<idtxflow::collab::StageBridge> bridge;   // null while unloaded
    };
    std::map<std::string, SessionBinding> bindings_;

    idtxflow::net::CollabEngine engine_;
};
