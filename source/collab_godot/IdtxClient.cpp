#include "IdtxClient.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <idtxflow_godot/nodes/IUsdNode3D.h>
#include <idtxflow_godot/nodes/UsdStageNode3D.h>
#include <idtxflow/net/model/ConventionMath.h>

#include "GodotStageBridge.h"
#include "SystemClock.h"
#include <idtxflow/net/adapters/auth/StaticTokenProvider.h>

using namespace godot;

using idtxflow::net::model::Mat4;
using idtxflow::net::model::PrimEdit;

IdtxClient* IdtxClient::singleton_ = nullptr;

namespace
{
    // Godot Transform3D -> row-major wire matrix (PrimEdit). Godot Basis row i ->
    // wire row i; translation on the bottom row. Inverse of mat4_to_transform.
    PrimEdit transform_to_prim_edit(const std::string& prim_path, const Transform3D& t)
    {
        PrimEdit e;
        e.kind = PrimEdit::Kind::Transform;
        e.prim_path = prim_path;
        e.is_matrix = true;
        const Basis& b = t.basis;
        const double basis_rows[9] = {
            b.rows[0][0], b.rows[0][1], b.rows[0][2],
            b.rows[1][0], b.rows[1][1], b.rows[1][2],
            b.rows[2][0], b.rows[2][1], b.rows[2][2],
        };
        const double origin[3] = {t.origin.x, t.origin.y, t.origin.z};
        e.matrix = idtxflow::net::model::wire_from_basis_origin(basis_rows, origin);
        return e;
    }

    Transform3D mat4_to_transform(const Mat4& mm)
    {
        double basis_rows[9];
        double origin[3];
        idtxflow::net::model::basis_origin_from_wire(mm, basis_rows, origin);
        Basis basis;
        basis.rows[0] = Vector3((real_t)basis_rows[0], (real_t)basis_rows[1], (real_t)basis_rows[2]);
        basis.rows[1] = Vector3((real_t)basis_rows[3], (real_t)basis_rows[4], (real_t)basis_rows[5]);
        basis.rows[2] = Vector3((real_t)basis_rows[6], (real_t)basis_rows[7], (real_t)basis_rows[8]);
        return Transform3D(basis, Vector3((real_t)origin[0], (real_t)origin[1], (real_t)origin[2]));
    }

    Transform3D separate_to_transform(const idtxflow::net::model::SeparateXform& s)
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

    Transform3D prim_edit_to_transform(const PrimEdit& e)
    {
        return e.is_matrix ? mat4_to_transform(e.matrix) : separate_to_transform(e.separate);
    }
}

IdtxClient::IdtxClient() = default;

IdtxClient::~IdtxClient()
{
    shutdown();
}

void IdtxClient::initialize()
{
    if (initialized_)
    {
        return;
    }

    dispatcher_ = std::make_unique<idtxflow::collab_godot::GodotDispatcher>(this, "_drain_dispatch");
    ticker_     = std::make_unique<idtxflow::collab_godot::GodotTicker>(this, "_on_process_frame");
    http_       = std::make_unique<idtxflow::net::adapters::IxHttpTransport>();
    ws_         = std::make_unique<idtxflow::net::adapters::IxWebSocketTransport>();

    idtxflow::net::CollabPorts ports;
    ports.http       = http_.get();
    ports.ws         = ws_.get();
    ports.dispatcher = dispatcher_.get();
    ports.token      = &idtxflow::net::adapters::StaticTokenProvider::instance();
    ports.stage      = nullptr;   // attached on stage load
    ports.clock      = &idtxflow::collab_godot::SystemClock::instance();
    ports.ticker     = ticker_.get();

    engine_.initialize(ports, this);
    initialized_ = true;

    // At module-init the SceneTree does not exist yet, so the ticker's
    // process_frame connection cannot be made in engine_.initialize(). Kick off a
    // deferred bootstrap that retries the connection on the main loop (which runs
    // in the editor too) until it takes; call_deferred works on this out-of-tree
    // singleton, the same proven path the dispatcher uses.
    call_deferred("_bootstrap_ticker");
}

void IdtxClient::shutdown()
{
    if (!initialized_)
    {
        return;
    }
    initialized_ = false;

    // Teardown order matters: stop the dispatcher first so any in-flight socket
    // callback that tries to post an observer call is dropped rather than run
    // against a half-torn-down engine; then shut the engine (which stops the
    // tick, closes the socket, and detaches the stage), drop the adapters, and
    // finally unregister the singleton.
    if (dispatcher_) dispatcher_->shutdown();
    engine_.shutdown();
    detach_transform_sync();
    ws_.reset();
    http_.reset();
    ticker_.reset();
    dispatcher_.reset();

    if (singleton_ == this)
    {
        singleton_ = nullptr;
        if (Engine::get_singleton()->has_singleton("IdtxClient"))
        {
            Engine::get_singleton()->unregister_singleton("IdtxClient");
        }
    }
}

void IdtxClient::_drain_dispatch()
{
    if (dispatcher_) dispatcher_->drain();
}

void IdtxClient::_on_process_frame()
{
    // Per-frame tick target (SceneTree::process_frame). Runs CollabEngine::poll()
    // via the ticker: advances the auto-arm countdown and drains outbound edit
    // coalescing (SessionSocket::flush_pending).
    if (ticker_) ticker_->fire();
}

void IdtxClient::_bootstrap_ticker()
{
    // Retry the process_frame connection until the SceneTree exists. Re-arms
    // itself on the main loop (editor-safe) while still unconnected; becomes a
    // no-op once connected.
    if (!ticker_) return;
    ticker_->try_connect();
    if (!ticker_->is_connected())
    {
        call_deferred("_bootstrap_ticker");
    }
}

void IdtxClient::set_base_url(const String& url)
{
    engine_.set_base_url(url.utf8().get_data());
}

String IdtxClient::get_base_url() const
{
    return String(engine_.base_url().c_str());
}

bool IdtxClient::is_authenticated() const
{
    return engine_.is_authenticated();
}

String IdtxClient::get_access_token() const
{
    return String(idtxflow::net::adapters::StaticTokenProvider::instance().get().c_str());
}

void IdtxClient::clear_credentials()
{
    engine_.clear_credentials();
}

String IdtxClient::download_url(const String& usd_file) const
{
    return String(engine_.download_url(usd_file.utf8().get_data()).c_str());
}

String IdtxClient::ws_base_url() const
{
    return String(engine_.ws_base_url().c_str());
}


// ---------------------------------------------------------------------------
// REST operations (delegate to the engine; results arrive via the observer).
// ---------------------------------------------------------------------------

void IdtxClient::login(const String& username, const String& password, const Callable& on_done)
{
    if (on_done.is_valid()) login_cbs_.push_back(on_done);
    engine_.login(username.utf8().get_data(), password.utf8().get_data());
}

void IdtxClient::list_files(const String& name_contains, const String& extension, const Callable& on_done)
{
    if (on_done.is_valid()) list_cbs_.push_back(on_done);
    engine_.list_files(name_contains.utf8().get_data(), extension.utf8().get_data());
}

void IdtxClient::create_session(const String& usd_file, const String& mode)
{
    engine_.create_session(usd_file.utf8().get_data(), mode.utf8().get_data());
}

void IdtxClient::delete_session(const String& session_id)
{
    engine_.delete_session(session_id.utf8().get_data());
}

void IdtxClient::begin_server_import(const String& usd_file, const String& mode, const Callable& on_done)
{
    if (on_done.is_valid()) create_cbs_.push_back(on_done);
    engine_.begin_session(usd_file.utf8().get_data(), mode.utf8().get_data());
}

void IdtxClient::end_session()
{
    engine_.end_session();
}

void IdtxClient::open_session_socket(const String& session_id, const String& ws_url)
{
    engine_.open_session_socket(session_id.utf8().get_data(), ws_url.utf8().get_data());
}

void IdtxClient::close_session_socket()
{
    engine_.close_session_socket();
}

bool IdtxClient::is_socket_open() const
{
    return engine_.is_socket_open();
}

void IdtxClient::send_transform(const String& prim_path, const Transform3D& xform)
{
    engine_.notify_local_edit(transform_to_prim_edit(std::string(prim_path.utf8().get_data()), xform));
}

// ---------------------------------------------------------------------------
// Transform sync
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

    stage_ = std::make_unique<idtxflow::collab_godot::GodotStageBridge>(usd_stage, usd_stage->get_stage());
    engine_.attach_stage(stage_.get(), remote);
    IDTX_LOG(IDTX_INFO, "attach_transform_sync: remote={}", remote ? "true" : "false");
}

void IdtxClient::detach_transform_sync()
{
    engine_.detach_stage();
    stage_.reset();
}

void IdtxClient::arm_transform_sync()
{
    engine_.arm_sync();
}

void IdtxClient::notify_local_transform_changed(Node* node)
{
    if (node == nullptr)
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
        IDTX_LOG(IDTX_DEBUG, "[trace] A notify_local_transform_changed: EMPTY prim_path, ignored");
        return;
    }
    IDTX_LOG(IDTX_DEBUG, "[trace] A notify_local_transform_changed prim='{}'",
             std::string(prim_path.utf8().get_data()));
    engine_.notify_local_edit(
        transform_to_prim_edit(std::string(prim_path.utf8().get_data()), n3d->get_transform()));
}

// ---------------------------------------------------------------------------
// CollabObserver (invoked on the main thread by the engine's dispatcher).
// ---------------------------------------------------------------------------

Dictionary IdtxClient::make_error_result(int http_code, const String& error_code,
                                         const String& message) const
{
    Dictionary d;
    d["ok"]         = false;
    d["http_code"]  = http_code;
    d["error_code"] = error_code;
    d["message"]    = message;
    return d;
}

void IdtxClient::resolve_next(std::vector<Callable>& queue, const Dictionary& result)
{
    if (queue.empty()) return;
    const Callable cb = queue.front();
    queue.erase(queue.begin());
    if (cb.is_valid()) cb.call(result);
}

void IdtxClient::on_login_ok(const idtxflow::net::model::LoginResult& r)
{
    Dictionary token;
    token["access_token"] = String(r.access_token.c_str());
    token["expires_in"]   = (int64_t)r.expires_in;
    Dictionary ok;
    ok["ok"]     = true;
    ok["result"] = token;
    resolve_next(login_cbs_, ok);
}

void IdtxClient::on_files(const std::vector<idtxflow::net::model::FileEntry>& files)
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
        d["modified_epoch"] = (int64_t)f.modified_epoch;
        arr.push_back(d);
    }

    Dictionary ok;
    ok["ok"]     = true;
    ok["result"] = arr;
    resolve_next(list_cbs_, ok);
}

void IdtxClient::on_session_created(const idtxflow::net::model::SessionInfo&)
{
    // The create result is delivered to the begin_server_import completion from
    // on_session_ready (which fires once the socket is also open); no separate
    // notification is emitted here.
}

void IdtxClient::on_session_ready(const idtxflow::net::model::SessionInfo& s,
                                  const std::string& stage_url, const std::string& ws_url)
{
    // Hand control back to the host for the engine-specific stage load: the
    // caller loads a UsdStageNode3D from stage_url, then calls
    // attach_transform_sync(stage_node, true).
    Dictionary d;
    d["session_id"] = String(s.session_id.c_str());
    d["usd_file"]   = String(s.usd_file.c_str());
    d["mode"]       = String(s.mode.c_str());
    d["ws_url"]     = String(ws_url.c_str());
    emit_signal("session_ready", d, String(stage_url.c_str()));

    // The create step succeeded (session created + socket opened); report it to a
    // begin_server_import completion. The subsequent stage load/ready lifecycle
    // stays on the session_ready signal.
    Dictionary ok;
    ok["ok"]     = true;
    ok["result"] = d;
    resolve_next(create_cbs_, ok);
}

void IdtxClient::on_session_closed(const std::string& session_id)
{
    emit_signal("session_closed", String(session_id.c_str()));
}

void IdtxClient::on_request_failed(idtxflow::net::Op op, const idtxflow::net::model::RestError& e)
{
    const Dictionary err = make_error_result(e.http_code, String(e.error_code.c_str()),
                                             String(e.message.c_str()));
    if (op == idtxflow::net::Op::Login)              resolve_next(login_cbs_, err);
    else if (op == idtxflow::net::Op::ListFiles)     resolve_next(list_cbs_, err);
    else if (op == idtxflow::net::Op::CreateSession) resolve_next(create_cbs_, err);
}

void IdtxClient::on_socket_opened()
{
    emit_signal("socket_opened");
}

void IdtxClient::on_handshake(const std::string& sid, const std::string& path, const std::string& uri)
{
    emit_signal("handshake_received", String(sid.c_str()), String(path.c_str()), String(uri.c_str()));
}

void IdtxClient::on_remote_edit(const idtxflow::net::model::PrimEdit& edit, const std::string& from)
{
    // The engine already applied the edit to the stage; surface it for any UI.
    emit_signal("transform_broadcast_received",
                String(edit.prim_path.c_str()), prim_edit_to_transform(edit), String(from.c_str()));
}

void IdtxClient::on_ack(bool ok, const std::string& error)
{
    emit_signal("ack_received", ok, String(error.c_str()));
}

void IdtxClient::on_socket_error(const std::string& code, const std::string& message)
{
    emit_signal("socket_error", String(code.c_str()), String(message.c_str()));
}

void IdtxClient::on_disconnected(idtxflow::net::model::CloseReason reason, int code, const std::string& text)
{
    emit_signal("socket_disconnected", (int)reason, code, String(text.c_str()));
}


// ---------------------------------------------------------------------------

void IdtxClient::_bind_methods()
{
    using namespace godot;

    ClassDB::bind_method(D_METHOD("set_base_url", "url"), &IdtxClient::set_base_url);
    ClassDB::bind_method(D_METHOD("get_base_url"), &IdtxClient::get_base_url);
    ClassDB::bind_method(D_METHOD("is_authenticated"), &IdtxClient::is_authenticated);
    ClassDB::bind_method(D_METHOD("get_access_token"), &IdtxClient::get_access_token);
    ClassDB::bind_method(D_METHOD("clear_credentials"), &IdtxClient::clear_credentials);

    ClassDB::bind_method(D_METHOD("login", "username", "password", "on_done"), &IdtxClient::login,
                         DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("list_files", "name_contains", "extension", "on_done"),
                         &IdtxClient::list_files, DEFVAL(""), DEFVAL(""), DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("create_session", "usd_file", "mode"), &IdtxClient::create_session,
                         DEFVAL("single_edit"));
    ClassDB::bind_method(D_METHOD("delete_session", "session_id"), &IdtxClient::delete_session);

    ClassDB::bind_method(D_METHOD("begin_server_import", "usd_file", "mode", "on_done"),
                         &IdtxClient::begin_server_import, DEFVAL("single_edit"), DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("end_session"), &IdtxClient::end_session);

    ClassDB::bind_method(D_METHOD("download_url", "usd_file"), &IdtxClient::download_url);
    ClassDB::bind_method(D_METHOD("ws_base_url"), &IdtxClient::ws_base_url);

    ClassDB::bind_method(D_METHOD("open_session_socket", "session_id", "ws_url"),
                         &IdtxClient::open_session_socket);
    ClassDB::bind_method(D_METHOD("close_session_socket"), &IdtxClient::close_session_socket);
    ClassDB::bind_method(D_METHOD("is_socket_open"), &IdtxClient::is_socket_open);
    ClassDB::bind_method(D_METHOD("send_transform", "prim_path", "xform"), &IdtxClient::send_transform);

    ClassDB::bind_method(D_METHOD("attach_transform_sync", "stage_node", "remote"),
                         &IdtxClient::attach_transform_sync);
    ClassDB::bind_method(D_METHOD("detach_transform_sync"), &IdtxClient::detach_transform_sync);
    ClassDB::bind_method(D_METHOD("arm_transform_sync"), &IdtxClient::arm_transform_sync);
    ClassDB::bind_method(D_METHOD("notify_local_transform_changed", "node"),
                         &IdtxClient::notify_local_transform_changed);

    // Internal trampolines: dispatcher drain, per-frame tick, and the deferred
    // ticker bootstrap (all call_deferred / signal targets).
    ClassDB::bind_method(D_METHOD("_drain_dispatch"), &IdtxClient::_drain_dispatch);
    ClassDB::bind_method(D_METHOD("_on_process_frame"), &IdtxClient::_on_process_frame);
    ClassDB::bind_method(D_METHOD("_bootstrap_ticker"), &IdtxClient::_bootstrap_ticker);

    // Session lifecycle: `session_ready` fires once the session is created and its
    // socket is open, carrying the resolved stage download URL for the host's
    // engine-specific stage load; `session_closed` follows end_session().
    ADD_SIGNAL(MethodInfo("session_ready",
        PropertyInfo(Variant::DICTIONARY, "session"), PropertyInfo(Variant::STRING, "stage_url")));
    ADD_SIGNAL(MethodInfo("session_closed", PropertyInfo(Variant::STRING, "session_id")));

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
        PropertyInfo(Variant::INT, "reason"), PropertyInfo(Variant::INT, "code"),
        PropertyInfo(Variant::STRING, "text")));
}
