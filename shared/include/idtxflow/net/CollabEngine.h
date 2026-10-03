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

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <idtxflow/net/CollabObserver.h>
#include <idtxflow/net/model/Types.h>
#include <idtxflow/net/ports/IClock.h>
#include <idtxflow/net/ports/IFrameTicker.h>
#include <idtxflow/net/ports/IHttpTransport.h>
#include <idtxflow/net/ports/IMainThreadDispatcher.h>
#include <idtxflow/net/ports/IStageBridge.h>
#include <idtxflow/net/ports/ITokenProvider.h>
#include <idtxflow/net/ports/ITransportFactory.h>
#include <idtxflow/net/ports/IWebSocketTransport.h>
#include <idtxflow/utils/Logger.h>

namespace idtxflow
{
namespace net
{
    class RestClient;
    class SessionSocket;

    /// The adapters the engine requires. `ws_factory` mints a fresh WebSocket
    /// transport per session (so N concurrent sessions get N sockets); the HTTP
    /// transport is shared across all REST calls. Stages are attached per
    /// session later (attach_stage), so none is provided here.
    struct CollabPorts
    {
        ports::IHttpTransport*        http = nullptr;
        ports::ITransportFactory*     ws_factory = nullptr;
        ports::IMainThreadDispatcher* dispatcher = nullptr;
        ports::ITokenProvider*        token = nullptr;
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
        void create_session(const std::string& usd_file, const std::string& mode,
                            bool auto_commit = false);
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
        // The engine can hold multiple concurrent sessions, each keyed by its
        // session id and owning its own WebSocket + attached stage + sync state.
        // Two entry points share the same tail (enter_session); they differ only
        // in how the SessionInfo is obtained:
        //
        //   open_new_session      -> POST /sessions   (create a fresh session)
        //   open_existing_session -> GET  /sessions/<id> (look up a running one)
        //
        // Both then run enter_session: record the session (id, mode, usd_file),
        // mint a WebSocket for it, compute the authenticated stage download URL
        // and the full socket URL, open the socket, and report on_session_ready
        // so the host performs the engine-specific stage load and attaches it
        // back (attach_stage). The stage_url is a plain http(s):// URL; the JWT is
        // injected later in the USD http asset resolver at fetch time, not here.

        // Create a new session for a file, then enter it. A create failure is
        // reported through on_request_failed(Op::CreateSession, ...). auto_commit
        // (create-only) asks the server to merge the session's overrides back into
        // the original USD file when the session is torn down.
        void open_new_session(const std::string& usd_file, const std::string& mode,
                              bool auto_commit = false);
        // Look up an existing session by id, then enter it (join). Unlike
        // open_new_session this emits no on_session_created (nothing was created).
        //
        // Client-side duplicate-join guard: if this engine is already in the
        // requested session, the call fails fast with
        // on_request_failed(Op::GetSession, "already_joined") and no network
        // round-trip. This guard must live here because the server keys sockets
        // by connection, not by client identity, so it currently cannot detect (and would
        // silently accept, or mislabel as single_edit_busy) a same-client rejoin.
        //
        // A lookup failure is reported through on_request_failed(Op::GetSession, ...).
        void open_existing_session(const std::string& session_id);
        // Tear one session down: detach its stage, close+drop its socket, and
        // forget it, then report on_session_closed(session_id). We do not delete
        // the session on the backend — leaving simply disconnects; the server
        // reaps idle sessions per its own rules. A no-op (still reports) when the
        // id is unknown.
        void end_session(const std::string& session_id);
        // Tear down every active session (used on shutdown). Reports on_session_closed for each.
        void end_all_sessions();
        // The ids of all sessions currently held by the engine.
        std::vector<std::string> active_session_ids() const;

        // --- session socket ---
        
        /// Whether the given session's socket is currently open (false if the id is unknown).
        bool is_socket_open(const std::string& session_id) const;

        /// Whether the given session has received its join snapshot's terminal
        /// SnapshotComplete since the current socket opened (false if unknown or
        /// still catching up). Latched state, so a late reader never misses it;
        /// reset to false on each (re)connect so a reconnect re-enters "syncing".
        bool is_snapshot_complete(const std::string& session_id) const;

        // --- transform sync (TfNotice-as-source), per session ---
        
        // Attach the live stage for a loaded session; `remote` enables broadcasting
        // on that session's socket. No-op if the id is unknown.
        void attach_stage(const std::string& session_id, ports::IStageBridge* stage, bool remote);
        /// Detach a session's stage (stops applying/broadcasting its edits).
        void detach_stage(const std::string& session_id);
        // Enable broadcasting for a session once its stage has settled, so
        // conversion-time writes don't phantom-broadcast.
        void arm_sync(const std::string& session_id);
        // Author a node's local edit into a session's stage (the free local save);
        // the stage's change report then drives that session's gated broadcast.
        void notify_local_edit(const std::string& session_id, const model::PrimEdit& edit);

        /// Drain outbound coalescing. Driven by the frame ticker.
        void poll();

    private:
        IDTX_LOG_CATEGORY("CollabEngine")

        // One live session the engine holds: its identity, its own WebSocket
        // transport + protocol socket, the attached stage (if any), and the
        // per-session transform-sync state. Everything a session needs is here so
        // sessions are fully isolated from one another.
        struct Session
        {
            std::string id;
            std::string mode;
            std::string usd_file;

            std::unique_ptr<ports::IWebSocketTransport> ws;     // owned per session
            std::unique_ptr<SessionSocket>              socket;

            ports::IStageBridge* stage = nullptr;   // attached on stage load
            bool remote = false;
            bool armed = false;
            bool applying_remote = false;
            // Set on SnapshotComplete, reset on each (re)connect.
            bool snapshot_complete = false;
            // Inbound remote edits (incl. the join snapshot) that arrived before a
            // stage bridge was attached. Buffered here instead of dropped, and
            // replayed onto the stage in attach_stage so a late-joiner / reconnect
            // snapshot lands regardless of socket-vs-stage-load ordering.
            std::vector<model::PrimEdit> pending_remote;
            // Post-attach settle: frames left before conversion-time writes are
            // deemed done; -1 = disabled/elapsed. Counted down in poll().
            int  arm_countdown = -1;
            bool settle_done = false;   // settle elapsed (one of the two arming gates)
            // Latest server_seq this session has received+applied. Outbound updates
            // carry it as their base (the server's stale check); advanced from every
            // ordered server message (broadcast/ack/snapshot). 0 = no state yet.
            uint64_t server_seq = 0;
        };

        // Invoked by a session's stage bridge when its live stage changes;
        // broadcasts the edit only for that session when it is armed + remote and
        // not currently applying a remote edit (loopback suppression).
        void on_stage_changed(const std::string& session_id, const model::PrimEdit& edit);

        // Monotonically advance a session's applied server_seq from an inbound
        // server message and push the new base to its socket (so subsequent
        // outbound updates carry a current, non-stale base). Never decreases.
        void advance_server_seq(Session& s, uint64_t server_seq);

        // Arm outbound broadcasting only when BOTH gates hold: the post-attach
        // settle elapsed (settle_done — conversion-time writes done) AND the join
        // snapshot completed (snapshot_complete). The protocol forbids sending
        // updates before SnapshotComplete; this enforces it. No-op unless remote.
        void try_arm(Session& s);

        // Shared tail of open_new_session / open_existing_session: record the
        // session, mint + open its socket, compute the stage download URL + full
        // socket URL, and report on_session_ready. Does not create or look up the
        // session itself — the caller supplies the resolved SessionInfo.
        void enter_session(const model::SessionInfo& session);

        // Open the collaboration WebSocket for a session record (minting the
        // transport via ws_factory) and wire its callbacks; lifecycle via
        // on_socket_opened / on_handshake / on_disconnected / on_socket_error.
        void open_session_socket(Session& s, const std::string& ws_url);

        // Look up a session by id, or nullptr if unknown.
        Session* find_session(const std::string& id);
        const Session* find_session(const std::string& id) const;

        // Tear down + erase a single session record (detach stage, close socket).
        // Does not report on_session_closed; the caller decides.
        void teardown_session(Session& s);

        bool           initialized_ = false;
        CollabPorts    ports_;
        CollabObserver* observer_ = nullptr;

        std::unique_ptr<RestClient> rest_;

        // All live sessions, keyed by session id.
        std::map<std::string, Session> sessions_;
    };

} // namespace net
} // namespace idtxflow
