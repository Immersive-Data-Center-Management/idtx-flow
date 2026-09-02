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

#include <memory>
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
#include <idtxflow/net/CollabObserver.h>
#include <idtxflow/net/net/RestClient.h>
#include <idtxflow/net/net/SessionSocket.h>
#include <idtxflow/net/adapters/transport/ix/IxHttpTransport.h>
#include <idtxflow/net/adapters/transport/ix/IxWebSocketTransport.h>

#include "GodotDispatcher.h"
#include "GodotTicker.h"

namespace idtxflow { namespace collab_godot { class GodotStageBridge; } }

class IdtxClient : public godot::Node, public idtxflow::net::CollabObserver
{
    GDCLASS(IdtxClient, godot::Node)

public:
    IdtxClient();
    ~IdtxClient() override;

    static IdtxClient* get_singleton() { return singleton_; }
    static void set_singleton(IdtxClient* s) { singleton_ = s; }

    /// Construct the adapters and start the engine. Idempotent.
    void initialize();
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
    // on failure. `result` is the token dict for login and the files Array for
    // list_files. The matching broadcast signals still fire and are deprecated.
    void login(const godot::String& username, const godot::String& password,
               const godot::Callable& on_done = godot::Callable());
    // Reachability probe: GET /health (unauthenticated). `on_done` receives
    // { "ok": true } on a healthy response, or the error dict on failure.
    void health(const godot::Callable& on_done = godot::Callable());
    // Fetch a server thumbnail image (authenticated, cached in the core by
    // usd_file). `on_done` receives { ok:true, result:{ bytes:PackedByteArray,
    // content_type:String } } or the error dict (incl. 404 when not generated).
    void fetch_thumbnail(const godot::String& usd_file, const godot::Callable& on_done = godot::Callable());
    void list_files(const godot::String& name_contains = "", const godot::String& extension = "",
                    const godot::Callable& on_done = godot::Callable());
    void create_session(const godot::String& usd_file, const godot::String& mode = "single_edit");
    void delete_session(const godot::String& session_id);

    // --- high-level session flow ---
    // Run the whole server-import lifecycle in the core: create the session,
    // open its socket, and report readiness (emits `session_ready` with the
    // resolved stage_url so the caller performs the engine-specific stage load
    // and then calls attach_transform_sync). `end_session` tears the active
    // session down (detach, close, delete) and emits `session_closed`.
    // `on_done` (optional) reports only the create result: { "ok": true } once
    // the session is created and its socket opened, or the failure Dictionary if
    // creation fails. The ready/loaded lifecycle stays on `session_ready`.
    void begin_server_import(const godot::String& usd_file, const godot::String& mode = "single_edit",
                             const godot::Callable& on_done = godot::Callable());
    void end_session();

    // --- URL helpers (sync) ---
    godot::String download_url(const godot::String& usd_file) const;
    godot::String ws_base_url() const;

    // --- WebSocket session ---
    void open_session_socket(const godot::String& session_id, const godot::String& ws_url);
    void close_session_socket();
    bool is_socket_open() const;

    // Outbound transform (matrix form); prim_path is the USD prim path.
    void send_transform(const godot::String& prim_path, const godot::Transform3D& xform);

    // --- Transform sync ---
    void attach_transform_sync(godot::Node* stage_node, bool remote);
    void detach_transform_sync();
    void arm_transform_sync();
    void notify_local_transform_changed(godot::Node* node);

    // CollabObserver
    void on_login_ok(const idtxflow::net::model::LoginResult& result) override;
    void on_health(const idtxflow::net::model::HealthResult& result) override;
    void on_thumbnail(const idtxflow::net::model::ThumbnailResult& result) override;
    void on_files(const std::vector<idtxflow::net::model::FileEntry>& files) override;
    void on_session_created(const idtxflow::net::model::SessionInfo& session) override;
    void on_request_failed(idtxflow::net::Op op, const idtxflow::net::model::RestError& error) override;
    void on_session_ready(const idtxflow::net::model::SessionInfo& session,
                          const std::string& stage_url, const std::string& ws_url) override;
    void on_session_closed(const std::string& session_id) override;
    void on_socket_opened() override;
    void on_handshake(const std::string& session_id, const std::string& usd_path,
                      const std::string& usd_uri) override;
    void on_remote_edit(const idtxflow::net::model::PrimEdit& edit,
                        const std::string& from_client_id) override;
    void on_ack(bool ok, const std::string& error) override;
    void on_socket_error(const std::string& code, const std::string& message) override;
    void on_disconnected(idtxflow::net::model::CloseReason reason, int code,
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

    static IdtxClient* singleton_;

    bool initialized_ = false;

    std::unique_ptr<idtxflow::collab_godot::GodotDispatcher>       dispatcher_;
    std::unique_ptr<idtxflow::collab_godot::GodotTicker>           ticker_;
    std::unique_ptr<idtxflow::net::adapters::IxHttpTransport>      http_;
    std::unique_ptr<idtxflow::net::adapters::IxWebSocketTransport> ws_;
    std::unique_ptr<idtxflow::collab_godot::GodotStageBridge>      stage_;

    idtxflow::net::CollabEngine engine_;
};
