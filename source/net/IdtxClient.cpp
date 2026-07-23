#include "IdtxClient.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/os.hpp>

#include <idtxflow/net/IdtxTokenHolder.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>
#include <idtxflow/utils/Logger.h>

#include "UsdTransformSync.h"

using namespace godot;

namespace { static constexpr const char* LOG_CATEGORY = "IdtxClient"; }

IdtxClient* IdtxClient::singleton_ = nullptr;

// Helper: convert a Godot Transform3D to a row-major idtxcore Mat4 (wire form).
// Convention (matches the project's UsdTypeConverter::toTransform and the
// UsdTransformSync author/read pair): Godot Basis row i -> wire matrix row i,
// translation in the last row (m12..m14). This is the exact inverse of
// mat4_to_transform() below, so a Godot->wire->Godot round-trip is identity.
static idtxflow::net::Mat4 transform_to_mat4(const Transform3D& t)
{
    idtxflow::net::Mat4 out;
    const Basis& b = t.basis;
    // 3x3 rotation/scale block, row-major: wire row i = Godot Basis row i.
    out.m[0]  = b.rows[0][0]; out.m[1]  = b.rows[0][1]; out.m[2]  = b.rows[0][2]; out.m[3]  = 0.0;
    out.m[4]  = b.rows[1][0]; out.m[5]  = b.rows[1][1]; out.m[6]  = b.rows[1][2]; out.m[7]  = 0.0;
    out.m[8]  = b.rows[2][0]; out.m[9]  = b.rows[2][1]; out.m[10] = b.rows[2][2]; out.m[11] = 0.0;
    // USD stores translation in the last row (row-major, row-vector convention).
    out.m[12] = t.origin.x;   out.m[13] = t.origin.y;   out.m[14] = t.origin.z;   out.m[15] = 1.0;
    return out;
}

// Helper: convert an inbound idtxcore Mat4 to a Godot Transform3D.
static Transform3D mat4_to_transform(const idtxflow::net::Mat4& mm)
{
    const auto& m = mm.m;
    // Godot Basis row i = wire matrix row i (inverse of transform_to_mat4).
    Basis basis;
    basis.rows[0] = Vector3((real_t)m[0], (real_t)m[1], (real_t)m[2]);
    basis.rows[1] = Vector3((real_t)m[4], (real_t)m[5], (real_t)m[6]);
    basis.rows[2] = Vector3((real_t)m[8], (real_t)m[9], (real_t)m[10]);
    Vector3 origin((real_t)m[12], (real_t)m[13], (real_t)m[14]);
    return Transform3D(basis, origin);
}

static Transform3D separate_to_transform(const idtxflow::net::SeparateXform& s)
{
    Transform3D t;
    t.origin = Vector3((real_t)s.translation[0], (real_t)s.translation[1], (real_t)s.translation[2]);
    Basis b = Basis::from_euler(Vector3(
        Math::deg_to_rad((real_t)s.rotation[0]),
        Math::deg_to_rad((real_t)s.rotation[1]),
        Math::deg_to_rad((real_t)s.rotation[2])));
    b.scale(Vector3((real_t)s.scale[0], (real_t)s.scale[1], (real_t)s.scale[2]));
    t.basis = b;
    return t;
}

IdtxClient::IdtxClient()
{
    rest_ = std::make_unique<idtxflow::net::IdtxRestClient>();
    // Ensure we receive _process. A Node only gets _process while inside the
    // SceneTree; this instance is created and add_child()-ed by the GDScript
    // autoload (addons/IDTXFlow/idtx_client_autoload.gd), so it IS in the tree
    // and _process ticks — driving IdtxSessionSocket::poll() to flush queued
    // outbound transform updates. The first-tick probe in _process() confirms
    // it is running.
    set_process(true);
}

IdtxClient::~IdtxClient()
{
    sync_.reset();
    if (socket_)
    {
        socket_->close();
        socket_.reset();
    }
    // If this instance was the registered global singleton, clear the pointer
    // so nothing dereferences a dangling instance after teardown.
    if (singleton_ == this)
    {
        if (Engine::get_singleton()->has_singleton("IdtxClient"))
        {
            Engine::get_singleton()->unregister_singleton("IdtxClient");
        }
        singleton_ = nullptr;
    }
}

void IdtxClient::set_as_singleton()
{
    // The GDScript autoload calls this after
    // add_child()-ing us into the SceneTree, so the globally reachable
    // `IdtxClient` is the same node that receives _process and drives
    // IdtxSessionSocket::poll(). Idempotent.
    Engine* engine = Engine::get_singleton();
    if (engine->has_singleton("IdtxClient"))
    {
        engine->unregister_singleton("IdtxClient");
    }
    singleton_ = this;
    engine->register_singleton("IdtxClient", this);
    IDTX_LOG(IDTX_INFO, "IdtxClient registered as engine singleton (in SceneTree).");
}

void IdtxClient::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("set_base_url", "url"), &IdtxClient::set_base_url);
    ClassDB::bind_method(D_METHOD("get_base_url"), &IdtxClient::get_base_url);

    ClassDB::bind_method(D_METHOD("is_authenticated"), &IdtxClient::is_authenticated);
    ClassDB::bind_method(D_METHOD("get_access_token"), &IdtxClient::get_access_token);
    ClassDB::bind_method(D_METHOD("clear_credentials"), &IdtxClient::clear_credentials);

    ClassDB::bind_method(D_METHOD("login", "username", "password"), &IdtxClient::login);
    ClassDB::bind_method(D_METHOD("list_files", "name_contains", "extension"),
                         &IdtxClient::list_files, DEFVAL(""), DEFVAL(""));
    ClassDB::bind_method(D_METHOD("create_session", "usd_file", "mode"),
                         &IdtxClient::create_session, DEFVAL("single_edit"));
    ClassDB::bind_method(D_METHOD("delete_session", "session_id"), &IdtxClient::delete_session);

    ClassDB::bind_method(D_METHOD("download_url", "usd_file"), &IdtxClient::download_url);
    ClassDB::bind_method(D_METHOD("ws_base_url"), &IdtxClient::ws_base_url);

    ClassDB::bind_method(D_METHOD("open_session_socket", "session_id", "ws_url"),
                         &IdtxClient::open_session_socket);
    ClassDB::bind_method(D_METHOD("close_session_socket"), &IdtxClient::close_session_socket);
    ClassDB::bind_method(D_METHOD("is_socket_open"), &IdtxClient::is_socket_open);
    ClassDB::bind_method(D_METHOD("send_transform", "prim_path", "xform"),
                         &IdtxClient::send_transform);
    ClassDB::bind_method(D_METHOD("attach_transform_sync", "stage_node", "remote"),
                         &IdtxClient::attach_transform_sync);
    ClassDB::bind_method(D_METHOD("detach_transform_sync"), &IdtxClient::detach_transform_sync);
    ClassDB::bind_method(D_METHOD("arm_transform_sync"), &IdtxClient::arm_transform_sync);
    ClassDB::bind_method(D_METHOD("notify_local_transform_changed", "node"),
                         &IdtxClient::notify_local_transform_changed);
    ClassDB::bind_method(D_METHOD("set_as_singleton"), &IdtxClient::set_as_singleton);

    // Internal deferred-emit trampolines (must be bound to be call_deferred-able).
    ClassDB::bind_method(D_METHOD("_emit_login_succeeded", "token", "expires_in"),
                         &IdtxClient::_emit_login_succeeded);
    ClassDB::bind_method(D_METHOD("_emit_login_failed", "http_code", "code", "message"),
                         &IdtxClient::_emit_login_failed);
    ClassDB::bind_method(D_METHOD("_emit_files_listed", "files"), &IdtxClient::_emit_files_listed);
    ClassDB::bind_method(D_METHOD("_emit_session_created", "session"),
                         &IdtxClient::_emit_session_created);
    ClassDB::bind_method(D_METHOD("_emit_request_failed", "op", "http_code", "code", "message"),
                         &IdtxClient::_emit_request_failed);
    ClassDB::bind_method(D_METHOD("_emit_socket_opened"), &IdtxClient::_emit_socket_opened);
    ClassDB::bind_method(D_METHOD("_emit_handshake", "session_id", "usd_path", "usd_uri"),
                         &IdtxClient::_emit_handshake);
    ClassDB::bind_method(D_METHOD("_emit_transform_broadcast", "prim_path", "xform", "from_client_id"),
                         &IdtxClient::_emit_transform_broadcast);
    ClassDB::bind_method(D_METHOD("_emit_socket_error", "code", "message"),
                         &IdtxClient::_emit_socket_error);
    ClassDB::bind_method(D_METHOD("_emit_socket_disconnected", "code", "reason"),
                         &IdtxClient::_emit_socket_disconnected);

    // Signals
    ADD_SIGNAL(MethodInfo("login_succeeded",
        PropertyInfo(Variant::STRING, "token"), PropertyInfo(Variant::INT, "expires_in")));
    ADD_SIGNAL(MethodInfo("login_failed",
        PropertyInfo(Variant::INT, "http_code"), PropertyInfo(Variant::STRING, "error_code"),
        PropertyInfo(Variant::STRING, "message")));
    ADD_SIGNAL(MethodInfo("files_listed", PropertyInfo(Variant::ARRAY, "files")));
    ADD_SIGNAL(MethodInfo("session_created", PropertyInfo(Variant::DICTIONARY, "session")));
    ADD_SIGNAL(MethodInfo("request_failed",
        PropertyInfo(Variant::STRING, "op"), PropertyInfo(Variant::INT, "http_code"),
        PropertyInfo(Variant::STRING, "error_code"), PropertyInfo(Variant::STRING, "message")));

    ADD_SIGNAL(MethodInfo("socket_opened"));
    ADD_SIGNAL(MethodInfo("handshake_received",
        PropertyInfo(Variant::STRING, "session_id"), PropertyInfo(Variant::STRING, "usd_path"),
        PropertyInfo(Variant::STRING, "usd_uri")));
    ADD_SIGNAL(MethodInfo("transform_broadcast_received",
        PropertyInfo(Variant::STRING, "prim_path"), PropertyInfo(Variant::TRANSFORM3D, "xform"),
        PropertyInfo(Variant::STRING, "from_client_id")));
    ADD_SIGNAL(MethodInfo("ack_received",
        PropertyInfo(Variant::BOOL, "ok"), PropertyInfo(Variant::STRING, "error")));
    ADD_SIGNAL(MethodInfo("socket_error",
        PropertyInfo(Variant::STRING, "code"), PropertyInfo(Variant::STRING, "message")));
    ADD_SIGNAL(MethodInfo("socket_disconnected",
        PropertyInfo(Variant::INT, "code"), PropertyInfo(Variant::STRING, "reason")));
}

void IdtxClient::_process(double /*delta*/)
{
    // One-time probe so we can confirm the singleton actually receives _process.
    static bool logged_first_tick = false;
    if (!logged_first_tick)
    {
        logged_first_tick = true;
        IDTX_LOG(IDTX_INFO, "_process is running (sync={}, socket={})",
            sync_ ? "yes" : "no", socket_ ? "yes" : "no");
    }
    if (socket_)
    {
        socket_->poll();
    }
    // Transform sync is event-driven (USD TfNotice + node
    // NOTIFICATION_TRANSFORM_CHANGED), so no per-frame poll is needed for it.
    // The socket poll above IS needed to flush queued outbound updates, which
    // works because the autoload places this node in the SceneTree so _process
    // ticks.
}

void IdtxClient::set_base_url(const String& url)
{
    rest_->set_base_url(url.utf8().get_data());
}

String IdtxClient::get_base_url() const
{
    return String(rest_->base_url().c_str());
}

bool IdtxClient::is_authenticated() const
{
    return idtxflow::net::IdtxTokenHolder::HasToken();
}

String IdtxClient::get_access_token() const
{
    return String(idtxflow::net::IdtxTokenHolder::Get().c_str());
}

void IdtxClient::clear_credentials()
{
    idtxflow::net::IdtxTokenHolder::Clear();
}

String IdtxClient::download_url(const String& usd_file) const
{
    return String(rest_->download_url(usd_file.utf8().get_data()).c_str());
}

String IdtxClient::ws_base_url() const
{
    return String(rest_->ws_base_url().c_str());
}

// ---------------------------------------------------------------------------
// REST operations. Engine callbacks fire on a background thread, so each one
// marshals onto the Godot main thread via call_deferred before emitting signals.
// ---------------------------------------------------------------------------

void IdtxClient::login(const String& username, const String& password)
{
    rest_->login(
        username.utf8().get_data(),
        password.utf8().get_data(),
        [this](const idtxflow::net::LoginResult& lr)
        {
            // Store the token so subsequent REST/WS/asset fetches are authenticated.
            idtxflow::net::IdtxTokenHolder::Set(lr.access_token, lr.token_type);
            Callable(this, "_emit_login_succeeded")
                .call_deferred(String(lr.access_token.c_str()), (int64_t)lr.expires_in);
        },
        [this](const idtxflow::net::RestError& e)
        {
            Callable(this, "_emit_login_failed")
                .call_deferred(e.http_code, String(e.error_code.c_str()), String(e.message.c_str()));
        });
}

void IdtxClient::list_files(const String& name_contains, const String& extension)
{
    rest_->list_files(
        name_contains.utf8().get_data(),
        extension.utf8().get_data(),
        [this](const std::vector<idtxflow::net::FileEntry>& files)
        {
            Array arr;
            for (const auto& f : files)
            {
                Dictionary d;
                d["filepath"]  = String(f.filepath.c_str());
                d["filename"]  = String(f.filename.c_str());
                d["directory"] = String(f.directory.c_str());
                d["size"]      = (int64_t)f.size;
                d["modified"]  = (int64_t)f.modified;
                arr.push_back(d);
            }
            Callable(this, "_emit_files_listed").call_deferred(arr);
        },
        [this](const idtxflow::net::RestError& e)
        {
            Callable(this, "_emit_request_failed")
                .call_deferred(String("list_files"), e.http_code,
                               String(e.error_code.c_str()), String(e.message.c_str()));
        });
}

void IdtxClient::create_session(const String& usd_file, const String& mode)
{
    rest_->create_session(
        usd_file.utf8().get_data(),
        mode.utf8().get_data(),
        [this](const idtxflow::net::SessionInfo& s)
        {
            Dictionary d;
            d["session_id"]   = String(s.session_id.c_str());
            d["usd_file"]     = String(s.usd_file.c_str());
            d["mode"]         = String(s.mode.c_str());
            d["client_count"] = (int64_t)s.client_count;
            d["created_at"]   = (int64_t)s.created_at;
            d["ws_url"]       = String(s.ws_url.c_str());
            d["protocol"]     = String(s.protocol.c_str());
            Callable(this, "_emit_session_created").call_deferred(d);
        },
        [this](const idtxflow::net::RestError& e)
        {
            Callable(this, "_emit_request_failed")
                .call_deferred(String("create_session"), e.http_code,
                               String(e.error_code.c_str()), String(e.message.c_str()));
        });
}

void IdtxClient::delete_session(const String& session_id)
{
    rest_->delete_session(
        session_id.utf8().get_data(),
        []() { /* success / already-gone: nothing to surface */ },
        [this](const idtxflow::net::RestError& e)
        {
            Callable(this, "_emit_request_failed")
                .call_deferred(String("delete_session"), e.http_code,
                               String(e.error_code.c_str()), String(e.message.c_str()));
        });
}

// ---------------------------------------------------------------------------
// WebSocket session
// ---------------------------------------------------------------------------

void IdtxClient::open_session_socket(const String& session_id, const String& ws_url)
{
    if (socket_)
    {
        socket_->close();
        socket_.reset();
    }

    socket_ = std::make_unique<idtxflow::net::IdtxSessionSocket>();
    socket_->set_session_id(session_id.utf8().get_data());

    socket_->on_opened([this]()
    {
        Callable(this, "_emit_socket_opened").call_deferred();
    });
    socket_->on_handshake([this](const std::string& sid, const std::string& usd_path, const std::string& usd_uri)
    {
        Callable(this, "_emit_handshake").call_deferred(
            String(sid.c_str()), String(usd_path.c_str()), String(usd_uri.c_str()));
    });
    socket_->on_broadcast([this](const idtxflow::net::TransformBroadcastMsg& b)
    {
        Transform3D xform = b.is_matrix ? mat4_to_transform(b.matrix)
                                        : separate_to_transform(b.separate);
        Callable(this, "_emit_transform_broadcast").call_deferred(
            String(b.prim_path.c_str()), xform, String(b.from_client_id.c_str()));
    });
    socket_->on_ack([this](bool ok, const std::string& error)
    {
        Callable(this, "_emit_socket_error").call_deferred(
            String(ok ? "ack_ok" : "ack_error"), String(error.c_str()));
    });
    socket_->on_error([this](const std::string& code, const std::string& message)
    {
        Callable(this, "_emit_socket_error").call_deferred(
            String(code.c_str()), String(message.c_str()));
    });
    socket_->on_disconnected([this](int code, const std::string& reason)
    {
        Callable(this, "_emit_socket_disconnected").call_deferred(code, String(reason.c_str()));
    });

    socket_->connect_to(ws_url.utf8().get_data(), idtxflow::net::IdtxTokenHolder::Get());
}

void IdtxClient::close_session_socket()
{
    if (socket_)
    {
        socket_->close();
        socket_.reset();
    }
}

bool IdtxClient::is_socket_open() const
{
    return socket_ && socket_->is_open();
}

void IdtxClient::send_transform(const String& prim_path, const Transform3D& xform)
{
    const bool open = socket_ && socket_->is_open();
    IDTX_LOG(IDTX_DEBUG, "send_transform prim='{}' socket_open={}",
        prim_path.utf8().get_data(), open ? "true" : "false");
    if (!open)
    {
        return;
    }
    socket_->send_transform_update_matrix(prim_path.utf8().get_data(), transform_to_mat4(xform));
}

// ---------------------------------------------------------------------------
// Deferred emit helpers (run on the Godot main thread)
// ---------------------------------------------------------------------------

void IdtxClient::_emit_login_succeeded(String token, int64_t expires_in)
{
    emit_signal("login_succeeded", token, (int64_t)expires_in);
}

void IdtxClient::_emit_login_failed(int http_code, String code, String message)
{
    emit_signal("login_failed", http_code, code, message);
}

void IdtxClient::_emit_files_listed(Array files)
{
    emit_signal("files_listed", files);
}

void IdtxClient::_emit_session_created(Dictionary session)
{
    emit_signal("session_created", session);
}

void IdtxClient::_emit_request_failed(String op, int http_code, String code, String message)
{
    // A 401 on any protected call means the token is dead — clear it so the UI
    // returns to login.
    if (http_code == 401)
    {
        idtxflow::net::IdtxTokenHolder::Clear();
    }
    emit_signal("request_failed", op, http_code, code, message);
}

void IdtxClient::_emit_socket_opened()
{
    emit_signal("socket_opened");
}

void IdtxClient::_emit_handshake(String session_id, String usd_path, String usd_uri)
{
    emit_signal("handshake_received", session_id, usd_path, usd_uri);
}

void IdtxClient::_emit_transform_broadcast(String prim_path, Transform3D xform, String from_client_id)
{
    // Apply the remote transform straight into the live USD stage + Godot node
    // (loopback-suppressed) when a sync is active — the §9.4 single change path.
    if (sync_)
    {
        sync_->apply_remote(std::string(prim_path.utf8().get_data()), xform);
    }
    emit_signal("transform_broadcast_received", prim_path, xform, from_client_id);
}

// ---------------------------------------------------------------------------
// Transform sync (§9.4)
// ---------------------------------------------------------------------------

void IdtxClient::attach_transform_sync(Node* stage_node, bool remote)
{
    detach_transform_sync();

    UsdStageNode3D* usd_stage = Object::cast_to<UsdStageNode3D>(stage_node);
    if (usd_stage == nullptr)
    {
        IDTX_LOG(IDTX_ERROR, "attach_transform_sync: node is not a UsdStageNode3D");
        return;
    }

    sync_ = std::make_unique<idtxflow::net::UsdTransformSync>(usd_stage, usd_stage->get_stage());
    sync_->set_remote(remote);
    sync_->set_stage_uri(std::string(usd_stage->get_stage_uri().utf8().get_data()));
    sync_->build_index();
    IDTX_LOG(IDTX_INFO, "attach_transform_sync: remote={}, is_processing={}",
        remote ? "true" : "false", is_processing() ? "true" : "false");
}

void IdtxClient::detach_transform_sync()
{
    sync_.reset();
}

void IdtxClient::arm_transform_sync()
{
    if (sync_)
    {
        sync_->arm();
        IDTX_LOG(IDTX_INFO, "transform sync armed (outbound broadcasting enabled)");
    }
}

void IdtxClient::notify_local_transform_changed(Node* node)
{
    if (!sync_ || node == nullptr)
    {
        return;
    }
    IUsdNode3D* usd = IUsdNode3D::from_node(node);
    Node3D* n3d = Object::cast_to<Node3D>(node);
    if (usd == nullptr || n3d == nullptr)
    {
        return;
    }
    const String prim_path = usd->get_prim_path();
    if (prim_path.is_empty())
    {
        return;
    }
    sync_->author_local(std::string(prim_path.utf8().get_data()), n3d->get_transform());
}

void IdtxClient::_emit_socket_error(String code, String message)
{
    // "ack_ok"/"ack_error" are surfaced via ack_received; everything else is socket_error.
    if (code == String("ack_ok") || code == String("ack_error"))
    {
        emit_signal("ack_received", code == String("ack_ok"), message);
        return;
    }
    emit_signal("socket_error", code, message);
}

void IdtxClient::_emit_socket_disconnected(int code, String reason)
{
    emit_signal("socket_disconnected", code, reason);
}
