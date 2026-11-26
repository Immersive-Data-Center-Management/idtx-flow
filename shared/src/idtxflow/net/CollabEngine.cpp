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
// Frames to wait before re-sending a SnapshotRequest the server rejected
// because the session's command queue was full (about half a second at 60 fps).
constexpr int kSnapshotRetryTicks = 30;
} // namespace

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
    for (auto& [id, s]: sessions_)
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
    rest_->login(
        username, password,
        [this](const model::LoginResult& lr) {
            if (ports_.token) ports_.token->set(lr.access_token, lr.token_type);
            if (observer_) observer_->on_login_ok(lr);
        },
        [this](const model::RestError& e) {
            if (observer_) observer_->on_request_failed(Op::Login, e);
        });
}

void CollabEngine::health()
{
    if (!rest_) return;
    rest_->health(
        [this](const model::HealthResult& hr) {
            if (observer_) observer_->on_health(hr);
        },
        [this](const model::RestError& e) {
            if (observer_) observer_->on_request_failed(Op::Health, e);
        });
}

void CollabEngine::fetch_thumbnail(const std::string& usd_file)
{
    if (!rest_) return;
    rest_->fetch_thumbnail(
        usd_file,
        [this](const model::ThumbnailResult& tr) {
            if (observer_) observer_->on_thumbnail(tr);
        },
        [this](const model::RestError& e) {
            if (observer_) observer_->on_request_failed(Op::FetchThumbnail, e);
        });
}

void CollabEngine::list_files(const std::string& name_contains, const std::string& extension)
{
    if (!rest_) return;
    rest_->list_files(
        name_contains, extension,
        [this](const std::vector<model::FileEntry>& files) {
            if (observer_) observer_->on_files(files);
        },
        [this](const model::RestError& e) {
            if (observer_) observer_->on_request_failed(Op::ListFiles, e);
        });
}

void CollabEngine::create_session(const std::string& usd_file, const std::string& mode, bool auto_commit)
{
    if (!rest_) return;
    rest_->create_session(
        usd_file, mode, auto_commit,
        [this](const model::SessionInfo& si) {
            if (observer_) observer_->on_session_created(si);
        },
        [this](const model::RestError& e) {
            if (observer_) observer_->on_request_failed(Op::CreateSession, e);
        });
}

void CollabEngine::list_sessions()
{
    if (!rest_) return;
    rest_->list_sessions(
        [this](const std::vector<model::SessionInfo>& sessions) {
            if (observer_) observer_->on_sessions(sessions);
        },
        [this](const model::RestError& e) {
            if (observer_) observer_->on_request_failed(Op::ListSessions, e);
        });
}

void CollabEngine::get_session(const std::string& session_id)
{
    if (!rest_) return;
    rest_->get_session(
        session_id,
        [this](const model::SessionInfo& si) {
            if (observer_) observer_->on_session_details(si);
        },
        [this](const model::RestError& e) {
            if (observer_) observer_->on_request_failed(Op::GetSession, e);
        });
}

void CollabEngine::commit_session(const std::string& session_id)
{
    if (!rest_) return;
    rest_->commit_session(
        session_id,
        [this](const model::CommitResult& cr) {
            if (observer_) observer_->on_session_committed(cr);
        },
        [this](const model::RestError& e) {
            if (observer_) observer_->on_request_failed(Op::CommitSession, e);
        });
}

void CollabEngine::check_download_exists(const std::string& usd_file)
{
    if (!rest_) return;
    rest_->check_download_exists(
        usd_file,
        [this, usd_file](bool exists) {
            if (observer_) observer_->on_download_exists(usd_file, exists);
        },
        [this](const model::RestError& e) {
            if (observer_) observer_->on_request_failed(Op::CheckDownload, e);
        });
}

void CollabEngine::check_thumbnail_exists(const std::string& usd_file)
{
    if (!rest_) return;
    rest_->check_thumbnail_exists(
        usd_file,
        [this, usd_file](bool exists) {
            if (observer_) observer_->on_thumbnail_exists(usd_file, exists);
        },
        [this](const model::RestError& e) {
            if (observer_) observer_->on_request_failed(Op::CheckThumbnail, e);
        });
}

void CollabEngine::delete_session(const std::string& session_id)
{
    if (!rest_) return;
    rest_->delete_session(
        session_id, [this] { /* deletion has no distinct observer callback today */ },
        [this](const model::RestError& e) {
            if (observer_) observer_->on_request_failed(Op::DeleteSession, e);
        });
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
    for (const auto& [id, s]: sessions_)
        ids.push_back(id);
    return ids;
}

void CollabEngine::enter_session(const model::SessionInfo& si)
{
    if (!rest_) return;

    // Record the session so end_session can tear down exactly what we entered.
    Session& s = sessions_[si.session_id];
    s.id = si.session_id;
    s.mode = si.mode;
    s.usd_file = si.usd_file;

    // Compute the authenticated stage download URL and the full socket URL
    // (base + relative ws_url) here. The stage_url is a plain http(s):// URL;
    // the JWT is injected later by the USD http asset resolver at fetch time.
    const std::string stage_url = rest_->download_url(si.usd_file);
    const std::string ws_full =
        (!si.session_id.empty() && !si.ws_url.empty()) ? rest_->ws_base_url() + si.ws_url : std::string();

    if (!ws_full.empty())
    {
        open_session_socket(s, ws_full);
    }

    // Hand control back to the host for the engine-specific stage load.
    if (observer_) observer_->on_session_ready(si, stage_url, ws_full);
}

void CollabEngine::open_new_session(const std::string& usd_file, const std::string& mode, bool auto_commit)
{
    if (!rest_) return;

    rest_->create_session(
        usd_file, mode, auto_commit,
        [this](const model::SessionInfo& si) {
            // Report the ordinary created callback first (unchanged observable
            // outcome), then enter the freshly created session.
            if (observer_) observer_->on_session_created(si);
            enter_session(si);
        },
        [this](const model::RestError& e) {
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
            e.http_code = 0;
            e.error_code = "already_joined";
            e.message = "Already in session '" + session_id + "'.";
            observer_->on_request_failed(Op::GetSession, e);
        }
        return;
    }

    rest_->get_session(
        session_id,
        [this](const model::SessionInfo& si) {
            // Joining an existing session: no on_session_created (nothing was
            // created); just enter it.
            enter_session(si);
        },
        [this](const model::RestError& e) {
            if (observer_) observer_->on_request_failed(Op::GetSession, e);
        });
}

void CollabEngine::teardown_session(Session& s)
{
    // Closing a transport can notify synchronously, so retire deliveries before teardown.
    if (s.callback_lifetime) s.callback_lifetime->active = false;
    s.callback_lifetime.reset();
    // Detach the stage (stops the change sink), then close+drop the socket
    // and its transport, so no further updates are sent over a socket we are tearing down.
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
    for (const std::string& id: active_session_ids())
    {
        end_session(id);
    }
}

void CollabEngine::open_session_socket(Session& s, const std::string& ws_url)
{
    if (!initialized_ || !ports_.ws_factory || !ports_.dispatcher) return;

    const std::string session_id = s.id;
    if (s.callback_lifetime) s.callback_lifetime->active = false;
    s.callback_lifetime.reset();
    if (s.socket)
    {
        s.socket->close();
        s.socket.reset();
    }
    s.ws.reset();
    // A fresh transport's first open is a join (the host loads the stage for it);
    // only the transport re-opening after a drop is a reconnect.
    s.opened_before = false;
    s.has_baseline = false;
    s.known_creates.clear();
    s.window_creates.clear();
    s.window_acks.clear();
    s.callback_lifetime = std::make_shared<SessionCallbackLifetime>();
    const std::weak_ptr<SessionCallbackLifetime> weak_lifetime = s.callback_lifetime;
    ports::IMainThreadDispatcher* dispatcher = ports_.dispatcher;

    // Mint a fresh WebSocket transport for this session, then wrap it in the
    // protocol socket. Each session owns its own transport so concurrent
    // sessions never share a connection.
    s.ws = ports_.ws_factory->make_websocket();
    s.socket = std::make_unique<SessionSocket>(s.ws.get(), ports_.clock);
    s.socket->set_session_id(session_id);

    // Socket callbacks may arrive on the transport's network thread; marshal every
    // observer notification onto the host main thread through the dispatcher,
    // tagged with this session's id so the host can route it.
    // Transport callbacks only enqueue work.
    // Validate the captured lifetime before accessing the engine, since the same
    // session ID can belong to a replacement membership and the engine may be gone.
    const auto post = [this, dispatcher, session_id, weak_lifetime](auto action) {
        dispatcher->post([this, session_id, weak_lifetime, action = std::move(action)] {
            const auto lifetime = weak_lifetime.lock();
            if (!lifetime || !lifetime->active) return;
            Session* session = find_session(session_id);
            if (!session || session->callback_lifetime != lifetime) return;
            action(*session, lifetime);
        });
    };

    s.socket->on_opened([this, post, session_id] {
        post([this, session_id](Session& session, const auto&) {
            // A (re)connect restarts the snapshot handshake: clear the latch and
            // disarm so we never broadcast on a stale base.
            const bool reconnect = session.opened_before;
            session.opened_before = true;
            session.snapshot_complete = false;
            session.armed = false;
            // A request sent on the old connection is never answered; the join
            // snapshot of the new one (or, after a reconnect, the request sent once
            // the stage is rebuilt) replaces it, and so does no retry.
            session.snapshot_request_id = 0;
            session.snapshot_retry_countdown = -1;
            if (reconnect)
            {
                // The join snapshot only replays onto a freshly loaded stage: it
                // re-creates prims and replays deactivations, but cannot undo state
                // the kept stage still holds (a restore made meanwhile stays
                // missing). A LayerReloaded sent while disconnected was lost as
                // well. So rebuild like after a reload: ignore the join snapshot,
                // let the host rebuild from re-fetched files, and request the
                // session's state once it calls reload_complete(). The rebuilt
                // stage's attach restarts the settle countdown.
                session.reloading = true;
                session.pending_remote.clear(); // superseded by the requested snapshot
            }
            if (observer_) observer_->on_socket_opened(session_id);
            if (reconnect && observer_) observer_->on_reconnected(session_id);
        });
    });
    s.socket->on_handshake([this, post](const std::string& sid, const std::string& path, const std::string& uri) {
        post([this, sid, path, uri](Session&, const auto&) {
            if (observer_) observer_->on_handshake(sid, path, uri);
        });
    });
    s.socket->on_remote_edit(
        [this, post, session_id](const model::PrimEdit& edit, const std::string& from, uint64_t server_seq) {
            post([this, session_id, edit, from, server_seq](Session& session, const auto& lifetime) {
                IDTX_LOG(IDTX_DEBUG, "Received edit session='{}' prim='{}' from='{}' server_seq={}", session_id,
                         edit.prim_path, from, server_seq);
                // Apply inbound to the stage with loopback suppression (so our author
                // isn't re-broadcast), then report. The broadcast's server_seq (incl. a
                // correction sent only to us) becomes our base, keeping updates non-stale.
                if (edit.kind == model::PrimEdit::Kind::CreatePrim)
                {
                    // A create the session cannot report as live now (rebuilding, or
                    // waiting for a snapshot) is new if this membership never saw the
                    // prim: report it when the session is live again. A snapshot
                    // replays known prims too; those are not new.
                    const bool unseen = session.known_creates.insert(edit.prim_path).second;
                    if (unseen && session.has_baseline && !session.snapshot_complete)
                        session.window_creates.push_back({edit.prim_path, server_seq});
                }
                if (session.reloading)
                {
                    // The stage is being rebuilt from a re-fetched file; the snapshot
                    // requested afterwards supersedes this frame, so it is neither
                    // applied, buffered nor counted towards the base.
                    IDTX_LOG(IDTX_DEBUG, "ignoring remote edit while reloading session='{}' prim='{}'", session_id,
                             edit.prim_path);
                    return;
                }
                // A create after the snapshot is a new prim; one inside the snapshot is replayed history.
                const bool live_create = edit.kind == model::PrimEdit::Kind::CreatePrim && session.snapshot_complete;
                if (session.stage)
                {
                    session.applying_remote = true;
                    session.stage->author_edit(edit, ports::EditOrigin::External);
                    // A bridge call may retire the engine or replace this membership with the same ID.
                    if (!lifetime->active) return;
                    Session* current = find_session(session_id);
                    if (!current || current->callback_lifetime != lifetime) return;
                    current->applying_remote = false;
                    advance_server_seq(*current, server_seq);
                }
                else
                {
                    // No stage yet (snapshot arrived before the stage attached):
                    // buffer for replay in attach_stage rather than drop the edit.
                    // This buffer also retains live updates received before attachment.
                    session.pending_remote.push_back(edit);
                    advance_server_seq(session, server_seq);
                    if (live_create) session.creates_before_attach.push_back({edit.prim_path, server_seq});
                }
                if (observer_) observer_->on_remote_edit(session_id, edit, from);
                if (!lifetime->active) return;
                Session* current = find_session(session_id);
                if (!current || current->callback_lifetime != lifetime) return;
                // Reported once applied, so the prim's node exists for the listener.
                if (live_create && current->stage) on_live_create(*current, edit.prim_path, server_seq);
            });
        });
    s.socket->on_snapshot_complete([this, post, session_id](uint64_t server_seq, uint64_t request_id) {
        post([this, session_id, server_seq, request_id](Session& session, const auto&) {
            // Only the snapshot the session is waiting for ends the wait: none while
            // reloading (its frames were ignored, a request follows), and after a
            // request only the SnapshotComplete that echoes its id.
            if (session.reloading || (session.snapshot_request_id != 0 && request_id != session.snapshot_request_id))
            {
                IDTX_LOG(IDTX_DEBUG, "ignoring snapshot complete session='{}' req_id={} awaited={} reloading={}",
                         session_id, request_id, session.snapshot_request_id, session.reloading);
                return;
            }
            // Latch so a late reader (is_snapshot_complete) sees it, then report.
            session.snapshot_request_id = 0;
            session.snapshot_complete = true;
            session.has_baseline = true;
            advance_server_seq(session, server_seq);
            try_arm(session); // snapshot gate satisfied; arm if settle also done
            // Prims created while the session was not live: their nodes exist now
            // that the snapshot is applied. An Ack seen meanwhile names this
            // client's own.
            std::vector<std::pair<std::string, uint64_t>> created;
            for (const auto& c: session.window_creates)
            {
                const auto acked = session.window_acks.find(c.server_seq);
                created.emplace_back(c.prim_path, acked != session.window_acks.end() ? acked->second : 0);
            }
            session.window_creates.clear();
            session.window_acks.clear();
            if (observer_) observer_->on_snapshot_complete(session_id);
            report_creates(session_id, std::move(created));
        });
    });
    s.socket->on_layer_reloaded(
        [this, post, session_id](const std::string& usd_path, const std::string& usd_uri, uint64_t server_seq) {
            post([this, session_id, usd_path, usd_uri, server_seq](Session& session, const auto&) {
                IDTX_LOG(IDTX_INFO, "layer reloaded session='{}' usd_path='{}' server_seq={}", session_id, usd_path,
                         server_seq);
                // Stop sending and stop applying until the host has rebuilt and the
                // requested snapshot has arrived. The reload's server_seq is not adopted:
                // the client has not applied that state yet; the snapshot carries it.
                session.reloading = true;
                session.snapshot_complete = false;
                session.armed = false;
                session.snapshot_request_id = 0;
                session.snapshot_retry_countdown = -1; // reload_complete sends a new request
                session.pending_remote.clear();        // superseded by the snapshot as well
                const std::string file_url = usd_uri.empty() ? download_url(usd_path) : base_url() + usd_uri;
                if (observer_) observer_->on_layer_reloaded(session_id, usd_path, file_url);
            });
        });
    s.socket->on_ack([this, post, session_id](bool ok, const std::string& error, uint64_t request_id,
                                              uint64_t server_seq, model::RequestKind request_kind) {
        post([this, session_id, ok, error, request_id, server_seq, request_kind](Session& session,
                                                                                 const auto& lifetime) {
            IDTX_LOG(IDTX_DEBUG, "Acknowledgement received session='{}' ok={} error='{}' req_id={} server_seq={}",
                     session_id, ok, error, request_id, server_seq);
            // The ack also carries server progress: adopt its server_seq so this author's base
            // advances past its own applied edit (otherwise the next local edit on
            // the same prim would fail the server's stale check), log it, and
            // notify. The server resolves ordering, so a rejected update is not resent;
            // the one exception is the SnapshotRequest sent after a reload (below).
            advance_server_seq(session, server_seq);
            if (!ok && request_id != 0 && request_id == session.snapshot_request_id)
            {
                // The server refused the SnapshotRequest sent after a reload, so
                // no SnapshotComplete follows and the session would never re-arm.
                // A full queue is transient: ask again shortly. Any other reason
                // ("session_closing") means the session is going away.
                session.snapshot_request_id = 0;
                if (error == "queue_full")
                {
                    IDTX_LOG(IDTX_WARN, "snapshot request rejected (queue_full) session='{}', retrying", session_id);
                    session.snapshot_retry_countdown = kSnapshotRetryTicks;
                }
                else
                {
                    IDTX_LOG(IDTX_WARN, "snapshot request rejected session='{}' error='{}'", session_id, error);
                }
            }
            // Notify before naming a held create: a listener matching prim_created to
            // its request has seen the Ack by then.
            if (observer_) observer_->on_ack(session_id, request_id, request_kind, ok, error);
            if (request_kind != model::RequestKind::CreatePrim || !lifetime->active) return;
            Session* current = find_session(session_id);
            if (current && current->callback_lifetime == lifetime) on_create_ack(*current, request_id, ok, server_seq);
        });
    });
    s.socket->on_error([this, post, session_id](const std::string& code, const std::string& message) {
        post([this, session_id, code, message](Session&, const auto&) {
            IDTX_LOG(IDTX_ERROR, "session '{}' socket error code='{}' msg='{}'", session_id, code, message);
            if (observer_) observer_->on_socket_error(session_id, code, message);
        });
    });
    s.socket->on_disconnected([this, post, session_id](model::CloseReason reason, int code, const std::string& text) {
        post([this, session_id, reason, code, text](Session& session, const auto&) {
            // Disarm and drop the snapshot latch so a transport-level drop cannot
            // leave the session armed; a reconnect then rebuilds the stage and
            // re-gates on the snapshot it requests. The socket drops its last-sent cache
            // as it goes down, so later edits are not deduplicated against
            // pre-disconnect values. That snapshot is authoritative.
            session.snapshot_complete = false;
            session.armed = false;
            // No Ack for an outstanding create arrives on a new connection: report
            // the held creates without a request id.
            session.outstanding_creates.clear();
            std::vector<std::pair<std::string, uint64_t>> held;
            for (const auto& c: session.held_creates)
                held.emplace_back(c.prim_path, 0);
            session.held_creates.clear();
            if (observer_) observer_->on_disconnected(session_id, reason, code, text);
            report_creates(session_id, std::move(held));
        });
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
        s->stage->set_on_changed(
            [this, session_id](const model::PrimEdit& edit) { on_stage_changed(session_id, edit); });
        s->stage->build_index();

        // Replay any remote edits that arrived before the stage was attached (a
        // join snapshot delivered on socket open ahead of the stage load). Apply
        // with loopback suppression, then clear the buffer.
        if (!s->pending_remote.empty())
        {
            s->applying_remote = true;
            for (const auto& edit: s->pending_remote)
                s->stage->author_edit(edit, ports::EditOrigin::External);
            s->applying_remote = false;
            s->pending_remote.clear();
        }

        // Live creates that arrived before the stage: their nodes exist now.
        std::vector<Session::LiveCreate> buffered = std::move(s->creates_before_attach);
        s->creates_before_attach.clear();
        for (const auto& c: buffered)
        {
            Session* current = find_session(session_id);
            if (!current || !current->stage) return;
            on_live_create(*current, c.prim_path, c.server_seq);
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

void CollabEngine::reload_complete(const std::string& session_id)
{
    Session* s = find_session(session_id);
    if (!s || !s->reloading) return;

    s->reloading = false;
    // The rebuilt stage no longer holds the session's edits: ask for them. If the
    // socket is closed nothing is sent; the reconnect rebuilds and asks again.
    s->snapshot_request_id = s->socket ? s->socket->send_snapshot_request() : 0;
    IDTX_LOG(IDTX_INFO, "reload complete session='{}' snapshot req_id={}", session_id, s->snapshot_request_id);
}

void CollabEngine::on_live_create(Session& s, const std::string& prim_path, uint64_t server_seq)
{
    // Without an outstanding request of ours, it is another client's create.
    if (s.outstanding_creates.empty())
    {
        report_creates(s.id, {{prim_path, 0}});
        return;
    }
    s.held_creates.push_back({prim_path, server_seq});
}

void CollabEngine::on_create_ack(Session& s, uint64_t request_id, bool ok, uint64_t server_seq)
{
    s.outstanding_creates.erase(request_id);
    // Not live: the create's broadcast was recorded for the window (if new);
    // remember which request it answers.
    if (ok && !s.snapshot_complete) s.window_acks[server_seq] = request_id;

    // The server sends a create's broadcast and then its Ack, both stamped with the
    // creation's server_seq. Only an accepted create has a broadcast: a rejected
    // one's Ack carries the current server_seq, which may be another client's create.
    std::vector<std::pair<std::string, uint64_t>> report;
    std::vector<Session::LiveCreate> keep;
    for (const auto& c: s.held_creates)
    {
        if (ok && c.server_seq == server_seq)
            report.emplace_back(c.prim_path, request_id);
        else if (c.server_seq <= server_seq || s.outstanding_creates.empty())
            report.emplace_back(c.prim_path, 0); // arrived before this Ack: not ours
        else
            keep.push_back(c);
    }
    s.held_creates = std::move(keep);
    report_creates(s.id, std::move(report));
}

void CollabEngine::report_creates(const std::string& session_id, std::vector<std::pair<std::string, uint64_t>> creates)
{
    if (creates.empty() || !observer_) return;
    Session* s = find_session(session_id);
    if (!s || !s->callback_lifetime) return;
    const std::shared_ptr<SessionCallbackLifetime> lifetime = s->callback_lifetime;
    for (const auto& [prim_path, request_id]: creates)
    {
        // A listener may end this membership or retire the engine.
        if (!lifetime->active) return;
        Session* current = find_session(session_id);
        if (!current || current->callback_lifetime != lifetime) return;
        observer_->on_prim_created(session_id, prim_path, request_id);
    }
}

void CollabEngine::advance_server_seq(Session& s, uint64_t server_seq)
{
    // 0 means "no state" (messages that don't reflect stage state, or Acks of
    // rejected updates) — ignore for ordering. Otherwise take the max so the
    // base never regresses, and keep the socket's outbound base in sync.
    if (server_seq == 0) return;
    if (server_seq > s.server_seq) s.server_seq = server_seq;
    if (s.socket) s.socket->set_base_server_seq(s.server_seq);
}

void CollabEngine::try_arm(Session& s)
{
    // Both gates must hold. NOTE: depends on the server always sending
    // SnapshotComplete; if it never arrives, the client never arms.
    if (!s.remote || s.armed || !s.settle_done || !s.snapshot_complete) return;
    s.armed = true;
    IDTX_LOG(IDTX_DEBUG, "Transform synchronization enabled session='{}'", s.id);
}

uint64_t CollabEngine::request_create_prim(const std::string& session_id, const std::string& parent_path,
                                           const std::string& prim_type, bool is_reference, bool is_payload,
                                           const std::string& reference_path, bool has_transform,
                                           const model::Mat4& transform)
{
    Session* s = find_session(session_id);
    if (!s) return 0;

    // Same preconditions as an outbound transform: a remote, snapshot-complete
    // session with an open socket. The protocol forbids sending before the
    // snapshot, and a request on a non-remote/unsynced/closed session is dropped.
    if (!s->remote || !s->snapshot_complete || !s->socket || !s->socket->is_open())
    {
        IDTX_LOG(IDTX_DEBUG, "request_create_prim dropped (remote={} snapshot={} has_socket={})", s->remote,
                 s->snapshot_complete, s->socket != nullptr);
        return 0;
    }

    // Build the request edit from the decomposed intent: a reference holder is
    // typeless and carries its arc kind + target; every other create carries a
    // built-in schema type. parent_path is the path the new prim is created
    // under — the server assigns the child name.
    model::PrimEdit edit;
    edit.kind = model::PrimEdit::Kind::CreatePrim;
    edit.prim_path = parent_path;
    if (is_reference)
    {
        edit.has_ref = true;
        edit.reference_path = reference_path;
        edit.ref_kind = is_payload ? model::PrimEdit::RefKind::Payload : model::PrimEdit::RefKind::Reference;
    }
    else
    {
        edit.prim_type = prim_type;
    }
    if (has_transform)
    {
        edit.has_create_transform = true;
        edit.create_is_matrix = true;
        edit.create_matrix = transform;
    }

    // The prim is materialized later by the server's broadcast echo, not here.
    const uint64_t request_id = s->socket->send_create_prim(edit);
    if (request_id != 0) s->outstanding_creates.insert(request_id);
    return request_id;
}

uint64_t CollabEngine::request_set_prim_active(const std::string& session_id, const std::string& prim_path, bool active)
{
    Session* s = find_session(session_id);
    if (!s) return 0;

    if (!s->remote || !s->snapshot_complete || !s->socket || !s->socket->is_open())
    {
        IDTX_LOG(IDTX_DEBUG, "request_set_prim_active dropped (remote={} snapshot={} has_socket={})", s->remote,
                 s->snapshot_complete, s->socket != nullptr);
        return 0;
    }

    model::PrimEdit edit;
    edit.kind = model::PrimEdit::Kind::SetActive;
    edit.prim_path = prim_path;
    edit.active = active;
    return s->socket->send_set_prim_active(edit);
}

void CollabEngine::on_stage_changed(const std::string& session_id, const model::PrimEdit& edit)
{
    Session* s = find_session(session_id);
    if (!s) return;

    // Broadcast only local edits of an armed, remote session — never while
    // applying a remote edit (that would echo it straight back). Also require a
    // completed snapshot and an open socket: the protocol forbids sending before
    // the snapshot, and an edit authored while disconnected is dropped (the join
    // snapshot wins on reconnect).
    if (!s->remote || !s->armed || s->applying_remote)
    {
        return;
    }
    if (!s->snapshot_complete || !s->socket || !s->socket->is_open())
    {
        return;
    }
    s->socket->send(edit);
}

void CollabEngine::poll()
{
    // Advance each session's auto-arm settle countdown once per frame; arm when it
    // elapses so USD conversion-time writes right after stage load are not
    // broadcast. Outbound edits are sent immediately in on_stage_changed, so there
    // is no per-frame socket flush here.
    for (auto& [id, s]: sessions_)
    {
        if (s.arm_countdown > 0)
        {
            --s.arm_countdown;
            if (s.arm_countdown == 0)
            {
                // Settle elapsed; arm iff the snapshot gate is also satisfied.
                s.arm_countdown = -1;
                s.settle_done = true;
                try_arm(s);
            }
        }
        if (s.snapshot_retry_countdown > 0 && --s.snapshot_retry_countdown == 0)
        {
            s.snapshot_retry_countdown = -1;
            // Not while reloading or after a snapshot arrived; a closed socket
            // sends nothing, and its reconnect rebuilds the stage and asks again.
            if (!s.reloading && !s.snapshot_complete && s.socket)
            {
                s.snapshot_request_id = s.socket->send_snapshot_request();
                IDTX_LOG(IDTX_INFO, "retry snapshot request session='{}' req_id={}", id, s.snapshot_request_id);
            }
        }
    }
}

} // namespace net
} // namespace idtxflow
