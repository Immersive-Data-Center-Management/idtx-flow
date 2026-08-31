#include <idtxflow/net/CollabEngine.h>

#include <utility>

#include <idtxflow/net/net/RestClient.h>
#include <idtxflow/net/net/SessionSocket.h>
#include <idtxflow/net/wire/Codec.h>

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

    close_session_socket();
    detach_stage();

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

void CollabEngine::list_files(const std::string& name_contains, const std::string& extension)
{
    if (!rest_) return;
    rest_->list_files(name_contains, extension,
        [this](const std::vector<model::FileEntry>& files) { if (observer_) observer_->on_files(files); },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::ListFiles, e); });
}

void CollabEngine::create_session(const std::string& usd_file, const std::string& mode)
{
    if (!rest_) return;
    rest_->create_session(usd_file, mode,
        [this](const model::SessionInfo& si) { if (observer_) observer_->on_session_created(si); },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::CreateSession, e); });
}

void CollabEngine::delete_session(const std::string& session_id)
{
    if (!rest_) return;
    rest_->delete_session(session_id,
        [this] { /* deletion has no distinct observer callback today */ },
        [this](const model::RestError& e) { if (observer_) observer_->on_request_failed(Op::DeleteSession, e); });
}

void CollabEngine::begin_session(const std::string& usd_file, const std::string& mode)
{
    if (!rest_) return;
    active_mode_ = mode;

    rest_->create_session(usd_file, mode,
        [this](const model::SessionInfo& si)
        {
            // Own the identity so end_session can tear down what we created.
            active_session_id_ = si.session_id;

            // Compute the authenticated stage download URL and the full socket
            // URL (base + relative ws_url) here, so the host is handed ready-to-
            // use values rather than composing them itself.
            const std::string stage_url = rest_->download_url(si.usd_file);
            const std::string ws_full =
                (!si.session_id.empty() && !si.ws_url.empty())
                    ? rest_->ws_base_url() + si.ws_url
                    : std::string();

            if (!ws_full.empty())
            {
                open_session_socket(si.session_id, ws_full);
            }

            // Report the ordinary created callback first (unchanged observable
            // outcome), then the high-level ready step that hands control back to
            // the host for the engine-specific stage load.
            if (observer_)
            {
                observer_->on_session_created(si);
                observer_->on_session_ready(si, stage_url, ws_full);
            }
        },
        [this](const model::RestError& e)
        {
            if (observer_) observer_->on_request_failed(Op::CreateSession, e);
        });
}

void CollabEngine::end_session()
{
    // Detach + close before the backend delete so no further frames flush over a
    // socket we are tearing down (matches the previous GDScript teardown order).
    detach_stage();
    close_session_socket();

    const std::string closed_id = active_session_id_;
    if (!active_session_id_.empty())
    {
        delete_session(active_session_id_);
    }
    active_session_id_.clear();
    active_mode_.clear();

    if (observer_) observer_->on_session_closed(closed_id);
}

void CollabEngine::open_session_socket(const std::string& session_id, const std::string& ws_url)
{
    if (!initialized_) return;

    socket_ = std::make_unique<SessionSocket>(ports_.ws, ports_.clock);
    socket_->set_session_id(session_id);

    // Socket callbacks arrive on the transport's network thread; marshal every
    // observer notification onto the host main thread through the dispatcher.
    socket_->on_opened([this]
    {
        ports_.dispatcher->post([this] { if (observer_) observer_->on_socket_opened(); });
    });
    socket_->on_handshake([this](const std::string& sid, const std::string& path, const std::string& uri)
    {
        ports_.dispatcher->post([this, sid, path, uri]
        { if (observer_) observer_->on_handshake(sid, path, uri); });
    });
    socket_->on_remote_edit([this](const model::PrimEdit& edit, const std::string& from)
    {
        ports_.dispatcher->post([this, edit, from]
        {
            // Apply the inbound edit to the stage with loopback suppression so the
            // resulting stage change is not re-broadcast, then report it.
            if (ports_.stage)
            {
                applying_remote_ = true;
                ports_.stage->apply_remote_edit(edit);
                applying_remote_ = false;
            }
            if (observer_) observer_->on_remote_edit(edit, from);
        });
    });
    socket_->on_ack([this](bool ok, const std::string& error)
    {
        ports_.dispatcher->post([this, ok, error] { if (observer_) observer_->on_ack(ok, error); });
    });
    socket_->on_error([this](const std::string& code, const std::string& message)
    {
        IDTX_LOG(IDTX_ERROR, "session socket error code='{}' msg='{}'", code, message);
        ports_.dispatcher->post([this, code, message]
        { if (observer_) observer_->on_socket_error(code, message); });
    });
    socket_->on_disconnected([this](model::CloseReason reason, int code, const std::string& text)
    {
        ports_.dispatcher->post([this, reason, code, text]
        { if (observer_) observer_->on_disconnected(reason, code, text); });
    });

    const std::string token = ports_.token ? ports_.token->get() : std::string();
    socket_->connect(ws_url, token);
}

void CollabEngine::close_session_socket()
{
    if (socket_)
    {
        socket_->close();
        socket_.reset();
    }
}

bool CollabEngine::is_socket_open() const
{
    return socket_ && socket_->is_open();
}

void CollabEngine::attach_stage(ports::IStageBridge* stage, bool remote)
{
    ports_.stage = stage;
    remote_ = remote;
    armed_ = false;
    applying_remote_ = false;
    // Auto-arm a few frames after a remote stage attaches, so the transform
    // writes performed during USD conversion/load settle first (they would
    // otherwise phantom-broadcast). Counted down in poll() via the frame ticker.
    arm_countdown_ = remote ? kArmAfterTicks : -1;
    if (ports_.stage)
    {
        ports_.stage->set_on_changed([this](const model::PrimEdit& edit) { on_stage_changed(edit); });
        ports_.stage->build_index();
    }
}

void CollabEngine::detach_stage()
{
    if (ports_.stage)
    {
        ports_.stage->set_on_changed(nullptr);
    }
    ports_.stage = nullptr;
    remote_ = false;
    armed_ = false;
    applying_remote_ = false;
    arm_countdown_ = -1;
}

void CollabEngine::arm_sync()
{
    // Explicit arm request (also reachable from the binding); cancel any pending
    // auto-arm countdown.
    armed_ = true;
    arm_countdown_ = -1;
}

void CollabEngine::notify_local_edit(const model::PrimEdit& edit)
{
    // Author into the live stage (the free local save). Authoring trips the
    // bridge's change report, which routes back through on_stage_changed for the
    // gated broadcast.
    IDTX_LOG(IDTX_DEBUG, "[trace] B notify_local_edit prim='{}' has_stage={}",
             edit.prim_path, ports_.stage != nullptr);
    if (ports_.stage)
    {
        ports_.stage->author_local_edit(edit);
    }
}

void CollabEngine::on_stage_changed(const model::PrimEdit& edit)
{
    // Broadcast only local edits of an armed, remote session — never while
    // applying a remote edit (that would echo it straight back).
    IDTX_LOG(IDTX_DEBUG, "[trace] D on_stage_changed prim='{}' remote={} armed={} applying={} has_socket={}",
             edit.prim_path, remote_, armed_, applying_remote_, socket_ != nullptr);
    if (!remote_ || !armed_ || applying_remote_)
    {
        return;
    }
    if (socket_)
    {
        socket_->send_edit(edit);
    }
}

void CollabEngine::poll()
{
    // Advance the auto-arm countdown once per frame; arm when it elapses so
    // conversion-time writes right after stage load are not broadcast.
    if (arm_countdown_ > 0)
    {
        --arm_countdown_;
        IDTX_LOG(IDTX_DEBUG, "[trace] E poll auto-arm countdown={} armed={}",
                 arm_countdown_, armed_);
        if (arm_countdown_ == 0)
        {
            armed_ = true;
            arm_countdown_ = -1;
            IDTX_LOG(IDTX_DEBUG, "[trace] E poll AUTO-ARMED (armed_=true)");
        }
    }

    if (socket_)
    {
        socket_->poll();
    }
}


} // namespace net
} // namespace idtxflow
