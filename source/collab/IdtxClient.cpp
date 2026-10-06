#include "IdtxClient.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>

#include <idtxflow_godot/nodes/UsdStageNode3D.h>

#include "stage_ops/TransformCodec.h"
#include <idtxflow/net/CollabComposition.h>
#include <idtxflow/net/ports/IStageBridge.h>

using namespace godot;

namespace gxform = idtxflow::collab::xform;

IdtxClient* IdtxClient::singleton_ = nullptr;

IdtxClient::IdtxClient() = default;

IdtxClient::~IdtxClient()
{
    shutdown();
}

void IdtxClient::initialize(std::unique_ptr<idtxflow::net::ports::ITransportFactory> transport_factory)
{
    if (initialized_)
    {
        return;
    }

    if (!transport_factory)
    {
        IDTX_LOG(IDTX_ERROR, "initialize: null transport factory; client not initialized");
        return;
    }

    transport_factory_ = std::move(transport_factory);

    dispatcher_ = std::make_unique<idtxflow::collab::Dispatcher>(this, "_drain_dispatch");
    ticker_ = std::make_unique<idtxflow::collab::Ticker>(this, "_on_process_frame");

    // Engine-agnostic ports (HTTP transport + token + clock + ws_factory) are assembled by the
    // shared composition helper. Godot only provides the engine-specific ports below (dispatcher, ticker).
    // The engine mints a WebSocket per session via the factory, so the factory (transport_factory_)
    // must outlive the engine — it is held as a member.
    idtxflow::net::CollabPorts ports;
    transports_ = idtxflow::net::make_agnostic_ports(*transport_factory_, ports);
    ports.dispatcher = dispatcher_.get();
    ports.ticker = ticker_.get();

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
    bindings_.clear();
    transports_ = {};
    transport_factory_.reset();
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

void IdtxClient::health(const Callable& on_done)
{
    if (on_done.is_valid()) health_cbs_.push_back(on_done);
    engine_.health();
}

void IdtxClient::fetch_thumbnail(const String& usd_file, const Callable& on_done)
{
    if (on_done.is_valid()) thumbnail_cbs_.push_back(on_done);
    engine_.fetch_thumbnail(usd_file.utf8().get_data());
}

void IdtxClient::list_files(const String& name_contains, const String& extension, const Callable& on_done)
{
    if (on_done.is_valid()) list_cbs_.push_back(on_done);
    engine_.list_files(name_contains.utf8().get_data(), extension.utf8().get_data());
}

void IdtxClient::list_sessions(const Callable& on_done)
{
    if (on_done.is_valid()) bindings_cbs_.push_back(on_done);
    engine_.list_sessions();
}

void IdtxClient::get_session(const String& session_id, const Callable& on_done)
{
    if (on_done.is_valid()) session_details_cbs_.push_back(on_done);
    engine_.get_session(session_id.utf8().get_data());
}

void IdtxClient::commit_session(const String& session_id, const Callable& on_done)
{
    if (on_done.is_valid()) commit_cbs_.push_back(on_done);
    engine_.commit_session(session_id.utf8().get_data());
}

void IdtxClient::check_download_exists(const String& usd_file, const Callable& on_done)
{
    if (on_done.is_valid()) download_exists_cbs_.push_back(on_done);
    engine_.check_download_exists(usd_file.utf8().get_data());
}

void IdtxClient::check_thumbnail_exists(const String& usd_file, const Callable& on_done)
{
    if (on_done.is_valid()) thumbnail_exists_cbs_.push_back(on_done);
    engine_.check_thumbnail_exists(usd_file.utf8().get_data());
}

void IdtxClient::open_new_session(const String& usd_file, const String& mode, bool auto_commit, const Callable& on_done)
{
    if (on_done.is_valid()) create_cbs_.push_back(on_done);
    engine_.open_new_session(usd_file.utf8().get_data(), mode.utf8().get_data(), auto_commit);
}

void IdtxClient::open_existing_session(const String& session_id, const Callable& on_done)
{
    if (on_done.is_valid()) join_cbs_.push_back(on_done);
    engine_.open_existing_session(session_id.utf8().get_data());
}

void IdtxClient::end_session(const String& session_id)
{
    // Drop this session's binding (detaches its bridge), then leave the session in the engine.
    unbind_session(session_id);
    engine_.end_session(session_id.utf8().get_data());
}

bool IdtxClient::is_socket_open(const String& session_id) const
{
    return engine_.is_socket_open(session_id.utf8().get_data());
}

bool IdtxClient::is_session_synced(const String& session_id) const
{
    return engine_.is_snapshot_complete(session_id.utf8().get_data());
}

// ---------------------------------------------------------------------------
// Session sync binding and stage-bridge lifecycle (per session)
// ---------------------------------------------------------------------------

void IdtxClient::bind_session(const String& session_id, Node* stage_node, bool remote)
{
    const std::string sid = session_id.utf8().get_data();

    UsdStageNode3D* usd_stage = Object::cast_to<UsdStageNode3D>(stage_node);
    if (usd_stage == nullptr)
    {
        IDTX_LOG(IDTX_ERROR, "bind_session: node is not a UsdStageNode3D");
        return;
    }

    // Replace any prior binding for this session.
    unbind_session(session_id);

    // Record the session's sync state: which node it is bound to and the remote
    // flag. The bridge itself is built by attach_bridge (below / on reload).
    SessionBinding& binding = bindings_[sid];
    binding.node_id = usd_stage->get_instance_id();
    binding.remote = remote;

    // Follow the node's stage lifecycle: drop the bridge when the stage unloads,
    // rebuild it when the stage loads again. The node instance persists across the
    // cycle, so connect once and keep the connections; bind the session id so the
    // slots know which session they manage.
    const Callable unload_cb = callable_mp(this, &IdtxClient::_on_stage_unloading).bind(session_id);
    if (!usd_stage->is_connected("stage_unloading", unload_cb)) usd_stage->connect("stage_unloading", unload_cb);
    const Callable loaded_cb = callable_mp(this, &IdtxClient::_on_stage_loaded).bind(session_id);
    if (!usd_stage->is_connected("stage_loading_finished", loaded_cb))
        usd_stage->connect("stage_loading_finished", loaded_cb);

    attach_bridge(sid);
}

void IdtxClient::unbind_session(const String& session_id)
{
    const std::string sid = session_id.utf8().get_data();
    detach_bridge(sid);

    // Disconnect the node's lifecycle signals if it is still alive, so a fully
    // unbound node carries no stray connections back to us.
    auto it = bindings_.find(sid);
    if (it != bindings_.end())
    {
        Node* node = Object::cast_to<Node>(ObjectDB::get_instance(it->second.node_id));
        if (node != nullptr)
        {
            const Callable unload_cb = callable_mp(this, &IdtxClient::_on_stage_unloading).bind(session_id);
            if (node->is_connected("stage_unloading", unload_cb)) node->disconnect("stage_unloading", unload_cb);
            const Callable loaded_cb = callable_mp(this, &IdtxClient::_on_stage_loaded).bind(session_id);
            if (node->is_connected("stage_loading_finished", loaded_cb))
                node->disconnect("stage_loading_finished", loaded_cb);
        }
    }

    bindings_.erase(sid);
}

void IdtxClient::attach_bridge(const std::string& session_id)
{
    auto it = bindings_.find(session_id);
    if (it == bindings_.end() || it->second.attached) return;
    UsdStageNode3D* usd_stage = Object::cast_to<UsdStageNode3D>(ObjectDB::get_instance(it->second.node_id));
    if (usd_stage == nullptr || !usd_stage->get_stage()) return;

    // The node owns its authoring bridge; the engine borrows it (non-owning).
    idtxflow::net::ports::IStageBridge* bridge = usd_stage->get_or_create_bridge();
    if (bridge == nullptr) return;
    engine_.attach_stage(session_id, bridge, it->second.remote);
    it->second.attached = true;
    IDTX_LOG(IDTX_INFO, "attach_bridge: session='{}' remote={}", session_id, it->second.remote ? "true" : "false");
}

void IdtxClient::detach_bridge(const std::string& session_id)
{
    auto it = bindings_.find(session_id);
    if (it == bindings_.end()) return;
    engine_.detach_stage(session_id);
    it->second.attached = false;
}

void IdtxClient::_on_stage_unloading(const String& session_id)
{
    detach_bridge(session_id.utf8().get_data());
}

void IdtxClient::_on_stage_loaded(bool success, const String& session_id)
{
    if (!success) return;
    attach_bridge(session_id.utf8().get_data());
}

// ---------------------------------------------------------------------------
// CollabObserver (invoked on the main thread by the engine's dispatcher).
// ---------------------------------------------------------------------------

Dictionary IdtxClient::make_error_result(int http_code, const String& error_code, const String& message) const
{
    Dictionary d;
    d["ok"] = false;
    d["http_code"] = http_code;
    d["error_code"] = error_code;
    d["message"] = message;
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
    token["expires_in"] = (int64_t)r.expires_in;
    Dictionary ok;
    ok["ok"] = true;
    ok["result"] = token;
    resolve_next(login_cbs_, ok);
}

void IdtxClient::on_health(const idtxflow::net::model::HealthResult& r)
{
    Dictionary status;
    status["http_code"] = (int)r.http_code;
    Dictionary ok;
    ok["ok"] = true;
    ok["result"] = status;
    resolve_next(health_cbs_, ok);
}

void IdtxClient::on_thumbnail(const idtxflow::net::model::ThumbnailResult& r)
{
    PackedByteArray bytes;
    bytes.resize((int64_t)r.bytes.size());
    if (!r.bytes.empty())
    {
        memcpy(bytes.ptrw(), r.bytes.data(), r.bytes.size());
    }
    Dictionary img;
    img["bytes"] = bytes;
    img["content_type"] = String(r.content_type.c_str());
    Dictionary ok;
    ok["ok"] = true;
    ok["result"] = img;
    resolve_next(thumbnail_cbs_, ok);
}

void IdtxClient::on_files(const std::vector<idtxflow::net::model::FileEntry>& files)
{
    Array arr;
    for (const auto& f: files)
    {
        Dictionary d;
        d["filepath"] = String(f.filepath.c_str());
        d["filename"] = String(f.filename.c_str());
        d["directory"] = String(f.directory.c_str());
        d["size"] = (int64_t)f.size;
        d["modified"] = (int64_t)f.modified;
        d["modified_epoch"] = (int64_t)f.modified_epoch;
        arr.push_back(d);
    }

    Dictionary ok;
    ok["ok"] = true;
    ok["result"] = arr;
    resolve_next(list_cbs_, ok);
}

void IdtxClient::on_session_created(const idtxflow::net::model::SessionInfo&)
{
    // The create result is delivered to the open_new_session completion from
    // on_session_ready (which fires once the socket is also open); no separate
    // notification is emitted here.
}

namespace
{
// Build the { session_id, usd_file, mode, client_count, created_at, ws_url,
// protocol } Dictionary the binding surfaces for a session.
godot::Dictionary session_to_dict(const idtxflow::net::model::SessionInfo& s)
{
    godot::Dictionary d;
    d["session_id"] = godot::String(s.session_id.c_str());
    d["usd_file"] = godot::String(s.usd_file.c_str());
    d["mode"] = godot::String(s.mode.c_str());
    d["client_count"] = (int64_t)s.client_count;
    d["created_at"] = (int64_t)s.created_at;
    d["ws_url"] = godot::String(s.ws_url.c_str());
    d["protocol"] = godot::String(s.protocol.c_str());
    return d;
}
} // namespace

void IdtxClient::on_sessions(const std::vector<idtxflow::net::model::SessionInfo>& sessions)
{
    Array arr;
    for (const auto& s: sessions)
    {
        arr.push_back(session_to_dict(s));
    }
    Dictionary ok;
    ok["ok"] = true;
    ok["result"] = arr;
    resolve_next(bindings_cbs_, ok);
}

void IdtxClient::on_session_details(const idtxflow::net::model::SessionInfo& session)
{
    Dictionary ok;
    ok["ok"] = true;
    ok["result"] = session_to_dict(session);
    resolve_next(session_details_cbs_, ok);
}

void IdtxClient::on_session_committed(const idtxflow::net::model::CommitResult& result)
{
    Dictionary r;
    r["session_id"] = String(result.session_id.c_str());
    r["committed"] = result.committed;
    Dictionary ok;
    ok["ok"] = true;
    ok["result"] = r;
    resolve_next(commit_cbs_, ok);
}

void IdtxClient::on_download_exists(const std::string&, bool exists)
{
    Dictionary r;
    r["exists"] = exists;
    Dictionary ok;
    ok["ok"] = true;
    ok["result"] = r;
    resolve_next(download_exists_cbs_, ok);
}

void IdtxClient::on_thumbnail_exists(const std::string&, bool exists)
{
    Dictionary r;
    r["exists"] = exists;
    Dictionary ok;
    ok["ok"] = true;
    ok["result"] = r;
    resolve_next(thumbnail_exists_cbs_, ok);
}

void IdtxClient::on_session_ready(const idtxflow::net::model::SessionInfo& s, const std::string& stage_url,
                                  const std::string& ws_url)
{
    // The session enter step succeeded (session created-or-joined + socket opened). Report
    // it to whichever high-level completion is waiting, per request
    // The result carries everything the caller needs to load the
    // stage (session_id + resolved stage_url + ws_url), then call
    // bind_session(session_id, stage_node, true).
    Dictionary d;
    d["session_id"] = String(s.session_id.c_str());
    d["usd_file"] = String(s.usd_file.c_str());
    d["mode"] = String(s.mode.c_str());
    d["ws_url"] = String(ws_url.c_str());
    d["stage_url"] = String(stage_url.c_str());

    Dictionary ok;
    ok["ok"] = true;
    ok["result"] = d;
    // Resolve only the queue for the in-flight flow (open_new_session -> create_cbs_,
    // open_existing_session -> join_cbs_), so a concurrent create and join cannot cross-resolve.
    if (!create_cbs_.empty())
        resolve_next(create_cbs_, ok);
    else
        resolve_next(join_cbs_, ok);
}

void IdtxClient::on_session_closed(const std::string& session_id)
{
    emit_signal("session_closed", String(session_id.c_str()));
}

void IdtxClient::on_request_failed(idtxflow::net::Op op, const idtxflow::net::model::RestError& e)
{
    const Dictionary err = make_error_result(e.http_code, String(e.error_code.c_str()), String(e.message.c_str()));
    if (op == idtxflow::net::Op::Login)
        resolve_next(login_cbs_, err);
    else if (op == idtxflow::net::Op::Health)
        resolve_next(health_cbs_, err);
    else if (op == idtxflow::net::Op::FetchThumbnail)
        resolve_next(thumbnail_cbs_, err);
    else if (op == idtxflow::net::Op::ListFiles)
        resolve_next(list_cbs_, err);
    else if (op == idtxflow::net::Op::CreateSession)
        resolve_next(create_cbs_, err);
    else if (op == idtxflow::net::Op::ListSessions)
        resolve_next(bindings_cbs_, err);
    else if (op == idtxflow::net::Op::GetSession)
    {
        // Op::GetSession backs both the raw get_session() REST call and the
        // open_existing_session() join lookup. If a join is in flight (join_cbs_
        // non-empty), the failure belongs to it; otherwise it is a raw get_session.
        if (!join_cbs_.empty())
            resolve_next(join_cbs_, err);
        else
            resolve_next(session_details_cbs_, err);
    }
    else if (op == idtxflow::net::Op::CommitSession)
        resolve_next(commit_cbs_, err);
    else if (op == idtxflow::net::Op::CheckDownload)
        resolve_next(download_exists_cbs_, err);
    else if (op == idtxflow::net::Op::CheckThumbnail)
        resolve_next(thumbnail_exists_cbs_, err);
}

void IdtxClient::on_socket_opened(const std::string& session_id)
{
    emit_signal("socket_opened", String(session_id.c_str()));
}

void IdtxClient::on_handshake(const std::string& sid, const std::string& path, const std::string& uri)
{
    emit_signal("handshake_received", String(sid.c_str()), String(path.c_str()), String(uri.c_str()));
}

void IdtxClient::on_remote_edit(const std::string& session_id, const idtxflow::net::model::PrimEdit& edit,
                                const std::string& from)
{
    // The engine already applied the edit to the session's stage; surface it for any UI.
    emit_signal("transform_broadcast_received", String(session_id.c_str()), String(edit.prim_path.c_str()),
                gxform::prim_edit_to_transform(edit), String(from.c_str()));
}

void IdtxClient::on_snapshot_complete(const std::string& session_id)
{
    // The join snapshot's prim edits were already applied via on_remote_edit; this
    // marks the boundary at which the stage holds the full current server state.
    emit_signal("snapshot_complete", String(session_id.c_str()));
}

void IdtxClient::on_ack(const std::string& session_id, bool ok, const std::string& error)
{
    emit_signal("ack_received", String(session_id.c_str()), ok, String(error.c_str()));
}

void IdtxClient::on_socket_error(const std::string& session_id, const std::string& code, const std::string& message)
{
    emit_signal("socket_error", String(session_id.c_str()), String(code.c_str()), String(message.c_str()));
}

void IdtxClient::on_disconnected(const std::string& session_id, idtxflow::net::model::CloseReason reason, int code,
                                 const std::string& text)
{
    emit_signal("socket_disconnected", String(session_id.c_str()), (int)reason, code, String(text.c_str()));
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

    ClassDB::bind_method(D_METHOD("login", "username", "password", "on_done"), &IdtxClient::login, DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("health", "on_done"), &IdtxClient::health, DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("fetch_thumbnail", "usd_file", "on_done"), &IdtxClient::fetch_thumbnail,
                         DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("list_files", "name_contains", "extension", "on_done"), &IdtxClient::list_files,
                         DEFVAL(""), DEFVAL(""), DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("list_sessions", "on_done"), &IdtxClient::list_sessions, DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("get_session", "session_id", "on_done"), &IdtxClient::get_session,
                         DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("commit_session", "session_id", "on_done"), &IdtxClient::commit_session,
                         DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("check_download_exists", "usd_file", "on_done"), &IdtxClient::check_download_exists,
                         DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("check_thumbnail_exists", "usd_file", "on_done"), &IdtxClient::check_thumbnail_exists,
                         DEFVAL(Callable()));

    ClassDB::bind_method(D_METHOD("open_new_session", "usd_file", "mode", "auto_commit", "on_done"),
                         &IdtxClient::open_new_session, DEFVAL("single_edit"), DEFVAL(false), DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("open_existing_session", "session_id", "on_done"), &IdtxClient::open_existing_session,
                         DEFVAL(Callable()));
    ClassDB::bind_method(D_METHOD("end_session", "session_id"), &IdtxClient::end_session);

    ClassDB::bind_method(D_METHOD("download_url", "usd_file"), &IdtxClient::download_url);
    ClassDB::bind_method(D_METHOD("ws_base_url"), &IdtxClient::ws_base_url);

    ClassDB::bind_method(D_METHOD("is_socket_open", "session_id"), &IdtxClient::is_socket_open);
    ClassDB::bind_method(D_METHOD("is_session_synced", "session_id"), &IdtxClient::is_session_synced);

    ClassDB::bind_method(D_METHOD("bind_session", "session_id", "stage_node", "remote"), &IdtxClient::bind_session);
    ClassDB::bind_method(D_METHOD("unbind_session", "session_id"), &IdtxClient::unbind_session);

    // Internal trampolines: dispatcher drain, per-frame tick, and the deferred
    // ticker bootstrap (all call_deferred / signal targets).
    ClassDB::bind_method(D_METHOD("_drain_dispatch"), &IdtxClient::_drain_dispatch);
    ClassDB::bind_method(D_METHOD("_on_process_frame"), &IdtxClient::_on_process_frame);
    ClassDB::bind_method(D_METHOD("_bootstrap_ticker"), &IdtxClient::_bootstrap_ticker);

    ADD_SIGNAL(MethodInfo("session_closed", PropertyInfo(Variant::STRING, "session_id")));
    ADD_SIGNAL(MethodInfo("socket_opened", PropertyInfo(Variant::STRING, "session_id")));
    ADD_SIGNAL(MethodInfo("handshake_received", PropertyInfo(Variant::STRING, "session_id"),
                          PropertyInfo(Variant::STRING, "usd_path"), PropertyInfo(Variant::STRING, "usd_uri")));
    ADD_SIGNAL(MethodInfo("transform_broadcast_received", PropertyInfo(Variant::STRING, "session_id"),
                          PropertyInfo(Variant::STRING, "prim_path"), PropertyInfo(Variant::TRANSFORM3D, "xform"),
                          PropertyInfo(Variant::STRING, "from_client_id")));
    ADD_SIGNAL(MethodInfo("snapshot_complete", PropertyInfo(Variant::STRING, "session_id")));
    ADD_SIGNAL(MethodInfo("ack_received", PropertyInfo(Variant::STRING, "session_id"),
                          PropertyInfo(Variant::BOOL, "ok"), PropertyInfo(Variant::STRING, "error")));
    ADD_SIGNAL(MethodInfo("socket_error", PropertyInfo(Variant::STRING, "session_id"),
                          PropertyInfo(Variant::STRING, "code"), PropertyInfo(Variant::STRING, "message")));
    ADD_SIGNAL(MethodInfo("socket_disconnected", PropertyInfo(Variant::STRING, "session_id"),
                          PropertyInfo(Variant::INT, "reason"), PropertyInfo(Variant::INT, "code"),
                          PropertyInfo(Variant::STRING, "text")));
}
