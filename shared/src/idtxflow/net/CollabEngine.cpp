#include <idtxflow/net/CollabEngine.h>

#include <utility>

#include <idtxflow/net/protocol/RestClient.h>
#include <idtxflow/net/protocol/SessionSocket.h>
#include <idtxflow/net/wire/WireCodec.h>

namespace idtxflow
{
namespace net
{
namespace
{
    // Frames to wait after a remote stage attaches before auto-arming outbound
    // broadcasting, so USD conversion-time transform writes settle first.
    constexpr int kArmAfterTicks = 3;
}

CollabEngine::~CollabEngine()
{
    shutdown();
}

void CollabEngine::initialize(const CollabPorts& ports, CollabObserver* observer)
{
    if (initialized_)
    {
        return;
    }

    // Fail fast if the linked protobuf runtime disagrees with the generated
    // message headers, rather than corrupting memory in the codec later.
    wire::verify_protobuf_version();

    ports_ = ports;
    observer_ = observer;

    rest_ = std::make_unique<RestClient>(ports_.http, ports_.token, ports_.dispatcher);

    if (ports_.ticker)
    {
        ports_.ticker->set_tick([this] { poll(); });
    }

    initialized_ = true;
}

void CollabEngine::shutdown()
{
    if (!initialized_)
    {
        return;
    }

    // Stop the tick first, then flip the flag so no work posted afterwards runs
    // against a half-torn-down engine or a dead observer.
    if (ports_.ticker)
    {
        ports_.ticker->clear_tick();
    }
    initialized_ = false;

    // Tear down every live session (detach stages, close sockets).
    for (auto& [id, s] : sessions_)
    {
        teardown_session(s);
    }
    sessions_.clear();

    rest_.reset();
    observer_ = nullptr;
}

void CollabEngine::set_base_url(const std::string& url)
{
    if (rest_) rest_->set_base_url(url);
}

std::string CollabEngine::base_url() const
{
    return ports_.http ? ports_.http->base_url() : std::string();
}

std::string CollabEngine::ws_base_url() const
{
    return rest_ ? rest_->ws_base_url() : std::string();
}

std::string CollabEngine::download_url(const std::string& usd_file) const
{
    return rest_ ? rest_->download_url(usd_file) : std::string();
}

bool CollabEngine::is_authenticated() const
{
    return ports_.token && !ports_.token->get().empty();
}

void CollabEngine::clear_credentials()
{
    if (ports_.token) ports_.token->clear();
    // Logging out: cached thumbnails were fetched with the old identity, drop them.
    if (rest_) rest_->clear_thumbnail_cache();
}

void CollabEngine::login(const std::string& username, const std::string& password)
{
    if (!rest_) return;
    rest_->login(username, password,
        [this](const model::LoginResult& lr)
        {
            if (ports_.token) ports_.token->set(lr.access_token, lr.token_type);
            if (observer_) observer_->on_login_ok(lr);
        },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::Login, e); });
}

void CollabEngine::health()
{
    if (!rest_) return;
    rest_->health(
        [this](const model::HealthResult& hr) { if (observer_) observer_->on_health(hr); },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::Health, e); });
}

void CollabEngine::fetch_thumbnail(const std::string& usd_file)
{
    if (!rest_) return;
    rest_->fetch_thumbnail(usd_file,
        [this](const model::ThumbnailResult& tr) { if (observer_) observer_->on_thumbnail(tr); },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::FetchThumbnail, e); });
}

void CollabEngine::list_files(const std::string& name_contains, const std::string& extension)
{
    if (!rest_) return;
    rest_->list_files(name_contains, extension,
        [this](const std::vector<model::FileEntry>& files) { if (observer_) observer_->on_files(files); },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::ListFiles, e); });
}

void CollabEngine::create_session(const std::string& usd_file, const std::string& mode,
                                  bool auto_commit)
{
    if (!rest_) return;
    rest_->create_session(usd_file, mode, auto_commit,
        [this](const model::SessionInfo& si) { if (observer_) observer_->on_session_created(si); },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::CreateSession, e); });
}

void CollabEngine::list_sessions()
{
    if (!rest_) return;
    rest_->list_sessions(
        [this](const std::vector<model::SessionInfo>& sessions) { if (observer_) observer_->on_sessions(sessions); },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::ListSessions, e); });
}

void CollabEngine::get_session(const std::string& session_id)
{
    if (!rest_) return;
    rest_->get_session(session_id,
        [this](const model::SessionInfo& si) { if (observer_) observer_->on_session_details(si); },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::GetSession, e); });
}

void CollabEngine::commit_session(const std::string& session_id)
{
    if (!rest_) return;
    rest_->commit_session(session_id,
        [this](const model::CommitResult& cr) { if (observer_) observer_->on_session_committed(cr); },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::CommitSession, e); });
}

void CollabEngine::check_download_exists(const std::string& usd_file)
{
    if (!rest_) return;
    rest_->check_download_exists(usd_file,
        [this, usd_file](bool exists) { if (observer_) observer_->on_download_exists(usd_file, exists); },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::CheckDownload, e); });
}

void CollabEngine::check_thumbnail_exists(const std::string& usd_file)
{
    if (!rest_) return;
    rest_->check_thumbnail_exists(usd_file,
        [this, usd_file](bool exists) { if (observer_) observer_->on_thumbnail_exists(usd_file, exists); },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::CheckThumbnail, e); });
}

void CollabEngine::delete_session(const std::string& session_id)
{
    if (!rest_) return;
    rest_->delete_session(session_id,
        [this] { /* deletion has no distinct observer callback today */ },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::DeleteSession, e); });
}

CollabEngine::Session* CollabEngine::find_session(const std::string& id)
{
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : &it->second;
}

const CollabEngine::Session* CollabEngine::find_session(const std::string& id) const
{
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : &it->second;
}

std::vector<std::string> CollabEngine::active_session_ids() const
{
    std::vector<std::string> ids;
    ids.reserve(sessions_.size());
    for (const auto& [id, s] : sessions_) ids.push_back(id);
    return ids;
}

void CollabEngine::enter_session(const model::SessionInfo& si)
{
    if (!rest_) return;

    // Record the session so end_session can tear down exactly what we entered.
    Session& s = sessions_[si.session_id];
    s.id       = si.session_id;
    s.mode     = si.mode;
    s.usd_file = si.usd_file;

    // Compute the authenticated stage download URL and the full socket URL
    // (base + relative ws_url) here. The stage_url is a plain http(s):// URL;
    // the JWT is injected later by the USD http asset resolver at fetch time.
    const std::string stage_url = rest_->download_url(si.usd_file);
    const std::string ws_full =
        (!si.session_id.empty() && !si.ws_url.empty())
            ? rest_->ws_base_url() + si.ws_url
            : std::string();

    if (!ws_full.empty())
    {
        open_session_socket(s, ws_full);
    }

    // Hand control back to the host for the engine-specific stage load.
    if (observer_) observer_->on_session_ready(si, stage_url, ws_full);
}

void CollabEngine::open_new_session(const std::string& usd_file, const std::string& mode,
                                    bool auto_commit)
{
    if (!rest_) return;

    rest_->create_session(usd_file, mode, auto_commit,
        [this](const model::SessionInfo& si)
        {
            // Report the ordinary created callback first (unchanged observable
            // outcome), then enter the freshly created session.
            if (observer_) observer_->on_session_created(si);
            enter_session(si);
        },
        [this](const model::RestError& e)
        {
            if (observer_) observer_->on_request_failed(Op::CreateSession, e);
        });
}

void CollabEngine::open_existing_session(const std::string& session_id)
{
    if (!rest_) return;

    // Client-side duplicate-join guard: refuse to join a session we are already
    // in, before any round-trip. The server currently cannot detect a same-client rejoin
    // (it keys sockets by connection, not identity), so this is the only place
    // the check can live.
    if (find_session(session_id))
    {
        if (observer_)
        {
            model::RestError e;
            e.http_code  = 0;
            e.error_code = "already_joined";
            e.message    = "Already in session '" + session_id + "'.";
            observer_->on_request_failed(Op::GetSession, e);
        }
        return;
    }

    rest_->get_session(session_id,
        [this](const model::SessionInfo& si)
        {
            // Joining an existing session: no on_session_created (nothing was
            // created); just enter it.
            enter_session(si);
        },
        [this](const model::RestError& e)
        {
            if (observer_) observer_->on_request_failed(Op::GetSession, e);
        });
}

void CollabEngine::teardown_session(Session& s)
{
    // Detach the stage first (stops the change sink), then close+drop the socket
    // and its transport, so no further frames flush over a socket we are tearing down.
    if (s.stage)
    {
        s.stage->set_on_changed(nullptr);
        s.stage = nullptr;
    }
    if (s.socket)
    {
        s.socket->close();
        s.socket.reset();
    }
    s.ws.reset();
    s.remote = false;
    s.armed = false;
    s.applying_remote = false;
    s.arm_countdown = -1;
}

void CollabEngine::end_session(const std::string& session_id)
{
    auto it = sessions_.find(session_id);
    if (it != sessions_.end())
    {
        // We are not deleting the session on the backend, just leaving. The
        // server reaps sessions with no connected clients per its own rules.
        teardown_session(it->second);
        sessions_.erase(it);
    }
    if (observer_) observer_->on_session_closed(session_id);
}

void CollabEngine::end_all_sessions()
{
    // Snapshot ids first: end_session erases from the map as it goes.
    for (const std::string& id : active_session_ids())
    {
        end_session(id);
    }
}

void CollabEngine::open_session_socket(Session& s, const std::string& ws_url)
{
    if (!initialized_ || !ports_.ws_factory) return;

    const std::string session_id = s.id;

    // Mint a fresh WebSocket transport for this session, then wrap it in the
    // protocol socket. Each session owns its own transport so concurrent
    // sessions never share a connection.
    s.ws = ports_.ws_factory->make_websocket();
    s.socket = std::make_unique<SessionSocket>(s.ws.get(), ports_.clock);
    s.socket->set_session_id(session_id);

    // Socket callbacks arrive on the transport's network thread; marshal every
    // observer notification onto the host main thread through the dispatcher,
    // tagged with this session's id so the host can route it.
    s.socket->on_opened([this, session_id]
    {
        ports_.dispatcher->post([this, session_id]
        {
            // A (re)connect restarts the snapshot handshake: clear the latch and
            // re-gate arming so we never broadcast on a stale base.
            if (Session* sp = find_session(session_id))
            {
                sp->snapshot_complete = false;
                sp->armed = false;
                sp->settle_done = false;
            }
            if (observer_) observer_->on_socket_opened(session_id);
        });
    });
    s.socket->on_handshake([this](const std::string& sid, const std::string& path, const std::string& uri)
    {
        ports_.dispatcher->post([this, sid, path, uri]
        { if (observer_) observer_->on_handshake(sid, path, uri); });
    });
    s.socket->on_remote_edit([this, session_id](const model::PrimEdit& edit, const std::string& from,
                                                uint64_t server_seq)
    {
        ports_.dispatcher->post([this, session_id, edit, from, server_seq]
        {
            IDTX_LOG(IDTX_DEBUG, "[trace] H on_remote_edit session='{}' prim='{}' from='{}' server_seq={}",
                     session_id, edit.prim_path, from, server_seq);
            // Apply inbound to the stage with loopback suppression (so our author
            // isn't re-broadcast), then report. The broadcast's server_seq (incl. a
            // correction sent only to us) becomes our base, keeping updates non-stale.
            Session* sp = find_session(session_id);
            if (sp && sp->stage)
            {
                sp->applying_remote = true;
                sp->stage->apply_remote_edit(edit);
                sp->applying_remote = false;
                advance_server_seq(*sp, server_seq);
            }
            else if (sp)
            {
                // No stage yet (snapshot arrived before the stage attached):
                // buffer for replay in attach_stage rather than drop the edit.
                sp->pending_remote.push_back(edit);
                advance_server_seq(*sp, server_seq);
            }
            if (observer_) observer_->on_remote_edit(session_id, edit, from);
        });
    });
    s.socket->on_snapshot_complete([this, session_id](uint64_t server_seq)
    {
        ports_.dispatcher->post([this, session_id, server_seq]
        {
            // Latch so a late reader (is_snapshot_complete) sees it, then report.
            if (Session* sp = find_session(session_id))
            {
                sp->snapshot_complete = true;
                advance_server_seq(*sp, server_seq);
                try_arm(*sp);   // snapshot gate satisfied; arm if settle also done
            }
            if (observer_) observer_->on_snapshot_complete(session_id);
        });
    });
    s.socket->on_ack([this, session_id](bool ok, const std::string& error,
                                        uint64_t request_id, uint64_t server_seq)
    {
        ports_.dispatcher->post([this, session_id, ok, error, request_id, server_seq]
        {
            IDTX_LOG(IDTX_DEBUG, "[trace] H on_ack session='{}' ok={} error='{}' req_id={} server_seq={}",
                     session_id, ok, error, request_id, server_seq);
            // Adopt the ack's server_seq, then let the socket resolve the request
            // (clear on success, resend on rejection — see SessionSocket::handle_ack).
            if (Session* sp = find_session(session_id))
            {
                advance_server_seq(*sp, server_seq);
                if (sp->socket) sp->socket->handle_ack(ok, error, request_id);
            }
            if (observer_) observer_->on_ack(session_id, ok, error);
        });
    });
    s.socket->on_error([this, session_id](const std::string& code, const std::string& message)
    {
        IDTX_LOG(IDTX_ERROR, "session '{}' socket error code='{}' msg='{}'", session_id, code, message);
        ports_.dispatcher->post([this, session_id, code, message]
        { if (observer_) observer_->on_socket_error(session_id, code, message); });
    });
    s.socket->on_disconnected([this, session_id](model::CloseReason reason, int code, const std::string& text)
    {
        ports_.dispatcher->post([this, session_id, reason, code, text]
        { if (observer_) observer_->on_disconnected(session_id, reason, code, text); });
    });

    const std::string token = ports_.token ? ports_.token->get() : std::string();
    s.socket->connect(ws_url, token);
}

bool CollabEngine::is_socket_open(const std::string& session_id) const
{
    const Session* s = find_session(session_id);
    return s && s->socket && s->socket->is_open();
}

bool CollabEngine::is_snapshot_complete(const std::string& session_id) const
{
    const Session* s = find_session(session_id);
    return s && s->snapshot_complete;
}

void CollabEngine::attach_stage(const std::string& session_id, ports::IStageBridge* stage, bool remote)
{
    Session* s = find_session(session_id);
    if (!s) return;

    s->stage = stage;
    s->remote = remote;
    s->armed = false;
    s->applying_remote = false;
    // Gate outbound arming: start the settle countdown for remote sessions;
    // snapshot_complete is the other gate.
    s->arm_countdown = remote ? kArmAfterTicks : -1;
    s->settle_done = false;
    if (s->stage)
    {
        s->stage->set_on_changed([this, session_id](const model::PrimEdit& edit)
        { on_stage_changed(session_id, edit); });
        s->stage->build_index();

        // Replay any remote edits that arrived before the stage was attached (a
        // join snapshot delivered on socket open ahead of the stage load). Apply
        // with loopback suppression, then clear the buffer.
        if (!s->pending_remote.empty())
        {
            s->applying_remote = true;
            for (const auto& edit : s->pending_remote)
                s->stage->apply_remote_edit(edit);
            s->applying_remote = false;
            s->pending_remote.clear();
        }
    }
}

void CollabEngine::detach_stage(const std::string& session_id)
{
    Session* s = find_session(session_id);
    if (!s) return;

    if (s->stage)
    {
        s->stage->set_on_changed(nullptr);
    }
    s->stage = nullptr;
    s->remote = false;
    s->armed = false;
    s->applying_remote = false;
    s->arm_countdown = -1;
}

void CollabEngine::arm_sync(const std::string& session_id)
{
    Session* s = find_session(session_id);
    if (!s) return;
    // Explicit arm request (also reachable from the binding); cancel any pending
    // auto-arm countdown.
    s->armed = true;
    s->arm_countdown = -1;
}

void CollabEngine::notify_local_edit(const std::string& session_id, const model::PrimEdit& edit)
{
    Session* s = find_session(session_id);
    // Author into the live stage (the free local save). Authoring trips the
    // bridge's change report, which routes back through on_stage_changed for the
    // gated broadcast.
    IDTX_LOG(IDTX_DEBUG, "[trace] B notify_local_edit session='{}' prim='{}' has_stage={}",
             session_id, edit.prim_path, (s != nullptr && s->stage != nullptr));
    if (s && s->stage)
    {
        s->stage->author_local_edit(edit);
    }
}

void CollabEngine::advance_server_seq(Session& s, uint64_t server_seq)
{
    // 0 means "no state" (messages that don't reflect stage state, or Acks of
    // rejected updates) — ignore for ordering. Otherwise take the max so the
    // base never regresses, and keep the socket's outbound base in sync.
    if (server_seq == 0)
        return;
    if (server_seq > s.server_seq)
        s.server_seq = server_seq;
    if (s.socket)
        s.socket->set_base_server_seq(s.server_seq);
}

void CollabEngine::try_arm(Session& s)
{
    // Both gates must hold. NOTE: depends on the server always sending
    // SnapshotComplete; if it never arrives, the client never arms.
    if (!s.remote || s.armed || !s.settle_done || !s.snapshot_complete)
        return;
    s.armed = true;
    IDTX_LOG(IDTX_DEBUG, "[trace] E try_arm session='{}' ARMED (settle_done + snapshot_complete)", s.id);
}

void CollabEngine::on_stage_changed(const std::string& session_id, const model::PrimEdit& edit)
{
    Session* s = find_session(session_id);
    if (!s) return;

    // Broadcast only local edits of an armed, remote session — never while
    // applying a remote edit (that would echo it straight back).
    IDTX_LOG(IDTX_DEBUG, "[trace] D on_stage_changed session='{}' prim='{}' remote={} armed={} applying={} has_socket={}",
             session_id, edit.prim_path, s->remote, s->armed, s->applying_remote, s->socket != nullptr);
    if (!s->remote || !s->armed || s->applying_remote)
    {
        return;
    }
    if (s->socket)
    {
        s->socket->send_edit(edit);
    }
}

void CollabEngine::poll()
{
    // Advance each session's auto-arm countdown once per frame; arm when it
    // elapses so conversion-time writes right after stage load are not
    // broadcast. Then pump each session's socket.
    for (auto& [id, s] : sessions_)
    {
        if (s.arm_countdown > 0)
        {
            --s.arm_countdown;
            IDTX_LOG(IDTX_DEBUG, "[trace] E poll session='{}' settle countdown={} armed={}",
                     id, s.arm_countdown, s.armed);
            if (s.arm_countdown == 0)
            {
                // Settle elapsed; arm iff the snapshot gate is also satisfied.
                s.arm_countdown = -1;
                s.settle_done = true;
                try_arm(s);
            }
        }

        if (s.socket)
        {
            s.socket->poll();
        }
    }
}


} // namespace net
} // namespace idtxflow
