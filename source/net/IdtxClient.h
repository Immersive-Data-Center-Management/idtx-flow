#pragma once

/**
 * @file IdtxClient.h
 * @brief Godot binding shim over the engine-agnostic IDTX collaboration engine.
 *
 * This is the *only* engine-specific part of the collaboration client. It:
 *   - owns the engine-agnostic IdtxRestClient + IdtxSessionSocket (shared/),
 *   - converts C++ result structs <-> Godot Dictionary/Array/Transform3D,
 *   - marshals background-thread callbacks onto the Godot main thread
 *     (call_deferred) before emitting Godot signals,
 *   - drives IdtxSessionSocket::poll() from _process(),
 *   - is registered as the global engine singleton "IdtxClient" (see
 *     register_types.cpp), reachable from both editor tools and runtime scripts.
 *
 * GDScript usage:
 *   IdtxClient.set_base_url("http://localhost:8080")
 *   IdtxClient.login_succeeded.connect(_on_ok)
 *   IdtxClient.login(user, pass)
 */

#include <memory>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/string.hpp>

#include <idtxflow/net/IdtxRestClient.h>
#include <idtxflow/net/IdtxSessionSocket.h>

class UsdStageNode3D;
namespace idtxflow { namespace net { class UsdTransformSync; } }

class IdtxClient : public godot::Node
{
    GDCLASS(IdtxClient, godot::Node)

public:
    IdtxClient();
    ~IdtxClient() override;

    static IdtxClient* get_singleton() { return singleton_; }
    static void set_singleton(IdtxClient* s) { singleton_ = s; }

    // Register THIS instance as the global engine singleton "IdtxClient".
    // Called from the GDScript autoload wrapper (idtx_client_autoload.gd) after
    // the node has been add_child()-ed into the SceneTree, so the globally
    // reachable instance is the same one that receives _process and therefore
    // drives IdtxSessionSocket::poll(). Safe to call once; replaces any prior
    // registration.
    void set_as_singleton();

    // Godot lifecycle
    void _process(double delta) override;

    // --- Configuration ---
    void set_base_url(const godot::String& url);
    godot::String get_base_url() const;

    // --- Auth state ---
    bool is_authenticated() const;
    godot::String get_access_token() const;
    void clear_credentials();

    // --- REST operations (async; emit signals) ---
    void login(const godot::String& username, const godot::String& password);
    void list_files(const godot::String& name_contains = "", const godot::String& extension = "");
    void create_session(const godot::String& usd_file, const godot::String& mode = "single_edit");
    void delete_session(const godot::String& session_id);

    // --- URL helpers (sync) ---
    godot::String download_url(const godot::String& usd_file) const;
    godot::String ws_base_url() const;

    // --- WebSocket session ---
    void open_session_socket(const godot::String& session_id, const godot::String& ws_url);
    void close_session_socket();
    bool is_socket_open() const;

    // Outbound transform (matrix form); prim_path is the USD prim path.
    void send_transform(const godot::String& prim_path, const godot::Transform3D& xform);

    // --- Transform sync (§9.4) ---
    // Attach live-stage transform sync for a freshly loaded stage node. `remote`
    // enables outbound broadcasting (true for a server session). Called from the
    // wizard after `stage_loading_finished`. Passing a Node keeps the header light.
    void attach_transform_sync(godot::Node* stage_node, bool remote);
    // Detach/destroy the active transform sync (e.g. on session teardown).
    void detach_transform_sync();
    // Arm outbound broadcasting on the active sync — call one frame after the
    // stage has loaded so USD-conversion transform writes don't phantom-broadcast.
    void arm_transform_sync();
    // Called by a converted USD node from NOTIFICATION_TRANSFORM_CHANGED: authors
    // the node's transform into the live stage (which trips the sync's TfNotice
    // listener → conditional broadcast). No-op if no sync is active.
    void notify_local_transform_changed(godot::Node* node);

protected:
    static void _bind_methods();

private:
    static IdtxClient* singleton_;

    // Deferred emit helpers (called on the main thread via call_deferred).
    void _emit_login_succeeded(godot::String token, int64_t expires_in);
    void _emit_login_failed(int http_code, godot::String code, godot::String message);
    void _emit_files_listed(godot::Array files);
    void _emit_session_created(godot::Dictionary session);
    void _emit_request_failed(godot::String op, int http_code, godot::String code, godot::String message);
    void _emit_socket_opened();
    void _emit_handshake(godot::String session_id, godot::String usd_path, godot::String usd_uri);
    void _emit_transform_broadcast(godot::String prim_path, godot::Transform3D xform, godot::String from_client_id);
    void _emit_socket_error(godot::String code, godot::String message);
    void _emit_socket_disconnected(int code, godot::String reason);

    std::unique_ptr<idtxflow::net::IdtxRestClient>   rest_;
    std::unique_ptr<idtxflow::net::IdtxSessionSocket> socket_;
    std::unique_ptr<idtxflow::net::UsdTransformSync>  sync_;
};
