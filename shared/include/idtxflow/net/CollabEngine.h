#pragma once

/**
 * @file CollabEngine.h
 * @brief The engine-agnostic core of the collaboration client.
 *
 * It owns the REST and session-socket protocol objects, holds the domain state
 * (auth, remote/armed flags), and decides when a local edit is broadcast. It
 * talks to the outside world only through injected ports and reports results to
 * a single CollabObserver on the host's main thread. Constructors do nothing
 * heavy; real setup and teardown are the idempotent initialize()/shutdown().
 */

#include <memory>
#include <string>

#include <idtxflow/net/CollabObserver.h>
#include <idtxflow/net/model/Types.h>
#include <idtxflow/net/ports/IClock.h>
#include <idtxflow/net/ports/IFrameTicker.h>
#include <idtxflow/net/ports/IHttpTransport.h>
#include <idtxflow/net/ports/IMainThreadDispatcher.h>
#include <idtxflow/net/ports/IStageBridge.h>
#include <idtxflow/net/ports/ITokenProvider.h>
#include <idtxflow/net/ports/IWebSocketTransport.h>
#include <idtxflow/utils/Logger.h>

namespace idtxflow
{
namespace net
{
    class RestClient;
    class SessionSocket;

    /// The adapters the engine requires. `stage` is optional (set later, on stage
    /// load, via attach_stage); the rest are provided at initialize().
    struct CollabPorts
    {
        ports::IHttpTransport*        http = nullptr;
        ports::IWebSocketTransport*   ws = nullptr;
        ports::IMainThreadDispatcher* dispatcher = nullptr;
        ports::ITokenProvider*        token = nullptr;
        ports::IStageBridge*          stage = nullptr;
        ports::IClock*                clock = nullptr;
        ports::IFrameTicker*          ticker = nullptr;
    };

    class CollabEngine
    {
    public:
        CollabEngine() = default;
        ~CollabEngine();

        CollabEngine(const CollabEngine&) = delete;
        CollabEngine& operator=(const CollabEngine&) = delete;

        /// Wire up the ports and begin ticking. Idempotent.
        void initialize(const CollabPorts& ports, CollabObserver* observer);
        /// Tear down: stop the tick, close the socket, detach the stage. Idempotent.
        void shutdown();
        /// Whether initialize() has run and the engine is wired up.
        bool is_initialized() const { return initialized_; }

        // --- configuration / auth ---
        
        /// Point the client at a backend base URL (clears any cached thumbnails).
        void set_base_url(const std::string& url);
        /// The configured backend base URL, or "" if unset.
        std::string base_url() const;
        /// The WebSocket base derived from the HTTP base (http->ws, https->wss).
        std::string ws_base_url() const;
        /// Compose the full authenticated USD download URL for a file.
        std::string download_url(const std::string& usd_file) const;
        /// Whether a bearer token is currently held.
        bool is_authenticated() const;
        /// Drop the stored token and any cached credentials.
        void clear_credentials();

        // --- REST operations (results delivered to the observer) ---
        
        /// Authenticate and store the token; result via on_login_ok / on_request_failed.
        void login(const std::string& username, const std::string& password);
        /// Probe backend reachability; result via on_health / on_request_failed.
        void health();
        /// Fetch a file's thumbnail image; result via on_thumbnail / on_request_failed.
        void fetch_thumbnail(const std::string& usd_file);
        /// List server USD files (optional name/extension filters); result via on_files / on_request_failed.
        void list_files(const std::string& name_contains, const std::string& extension);
        /// Create a session for a file; result via on_session_created / on_request_failed.
        void create_session(const std::string& usd_file, const std::string& mode);
        /// List active sessions; result via on_sessions / on_request_failed.
        void list_sessions();
        /// Retrieve one session's details; result via on_session_details / on_request_failed.
        void get_session(const std::string& session_id);
        /// Commit a session's overrides to the USD file; result via on_session_committed / on_request_failed.
        void commit_session(const std::string& session_id);
        /// Probe whether a file exists (no download); result via on_download_exists / on_request_failed.
        void check_download_exists(const std::string& usd_file);
        /// Probe whether a thumbnail exists (no download); result via on_thumbnail_exists / on_request_failed.
        void check_thumbnail_exists(const std::string& usd_file);
        /// Delete a session; failures reported via on_request_failed (no success callback).
        void delete_session(const std::string& session_id);

        // --- high-level session flow ---
        //
        // Two entry points that share the same tail (enter_session): they differ
        // only in how the SessionInfo is obtained.
        //
        //   open_new_session      -> POST /sessions   (create a fresh session)
        //   open_existing_session -> GET  /sessions/<id> (look up a running one)
        //
        // Both then run enter_session: own the active session id/mode, compute the
        // authenticated stage download URL and the full socket URL, open the
        // session socket, and report on_session_ready so the host performs the
        // engine-specific stage load and attaches it back (attach_stage). The
        // resulting stage_url is a plain http(s):// URL; the authenticated fetch
        // happens later in the USD http asset resolver (JWT injected at fetch
        // time), not here.

        // Create a new session for a file, then enter it. A create failure is
        // reported through on_request_failed(Op::CreateSession, ...).
        void open_new_session(const std::string& usd_file, const std::string& mode);
        // Look up an existing session by id, then enter it (join). Unlike
        // open_new_session this emits no on_session_created (nothing was created).
        // A lookup failure is reported through on_request_failed(Op::GetSession, ...).
        void open_existing_session(const std::string& session_id);
        // Tear the active session down: detach the stage, close the socket, and
        // request deletion on the backend, then report on_session_closed. Safe to
        // call with no active session (still reports, with an empty id).
        void end_session();

        // --- session socket ---
        
        /// Open the collaboration WebSocket for a session; lifecycle via
        /// on_socket_opened / on_handshake / on_disconnected / on_socket_error.
        void open_session_socket(const std::string& session_id, const std::string& ws_url);
        /// Close the collaboration WebSocket (reports on_disconnected).
        void close_session_socket();
        /// Whether the session socket is currently open.
        bool is_socket_open() const;

        // --- transform sync (TfNotice-as-source) ---
        
        // Attach the live stage for a loaded session; `remote` enables broadcasting.
        void attach_stage(ports::IStageBridge* stage, bool remote);
        /// Detach the current stage (stops applying/broadcasting edits).
        void detach_stage();
        // Enable broadcasting once the stage has settled, so conversion-time writes
        // don't phantom-broadcast.
        void arm_sync();
        // Author a node's local edit into the stage (the free local save); the
        // stage's change report then drives the gated broadcast.
        void notify_local_edit(const model::PrimEdit& edit);

        /// Drain outbound coalescing. Driven by the frame ticker.
        void poll();

    private:
        IDTX_LOG_CATEGORY("CollabEngine")

        // Invoked by the stage bridge when the live stage changes; broadcasts the
        // edit only for an armed, remote session that is not currently applying a
        // remote edit (loopback suppression).
        void on_stage_changed(const model::PrimEdit& edit);

        // Shared tail of open_new_session / open_existing_session: own the session
        // identity, compute the stage download URL + full socket URL, open the
        // socket, and report on_session_ready. Does not create or look up the
        // session itself — the caller supplies the resolved SessionInfo.
        void enter_session(const model::SessionInfo& session);

        bool           initialized_ = false;
        CollabPorts    ports_;
        CollabObserver* observer_ = nullptr;

        std::unique_ptr<RestClient>    rest_;
        std::unique_ptr<SessionSocket> socket_;

        bool remote_ = false;
        bool armed_ = false;
        bool applying_remote_ = false;

        // Identity of the session the high-level flow (open_new_session /
        // open_existing_session -> enter_session) owns, so end_session can tear
        // down exactly what it entered without the host tracking the id.
        std::string active_session_id_;
        std::string active_mode_;

        // Frames remaining before outbound broadcasting auto-arms after a remote
        // stage attaches; -1 means disabled. Counted down by poll() (driven by the
        // frame ticker) so conversion-time transform writes right after load are
        // suppressed without relying on engine-side timing done in script.
        int arm_countdown_ = -1;
    };

} // namespace net
} // namespace idtxflow
