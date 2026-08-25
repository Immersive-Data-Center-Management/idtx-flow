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
        bool is_initialized() const { return initialized_; }

        // --- configuration / auth ---
        void set_base_url(const std::string& url);
        std::string base_url() const;
        std::string ws_base_url() const;
        std::string download_url(const std::string& usd_file) const;
        bool is_authenticated() const;
        void clear_credentials();

        // --- REST operations (results delivered to the observer) ---
        void login(const std::string& username, const std::string& password);
        void list_files(const std::string& name_contains, const std::string& extension);
        void create_session(const std::string& usd_file, const std::string& mode);
        void delete_session(const std::string& session_id);

        // --- high-level session flow ---
        // Drive the whole server-import lifecycle: create the session, compute
        // the authenticated stage download URL, open the session socket, then
        // report on_session_ready so the host performs the engine-specific stage
        // load and attaches it back (attach_stage). Owns the active session id,
        // mode, and ws-url composition. A create failure is reported through the
        // existing on_request_failed(Op::CreateSession, ...).
        void begin_session(const std::string& usd_file, const std::string& mode);
        // Tear the active session down: detach the stage, close the socket, and
        // request deletion on the backend, then report on_session_closed. Safe to
        // call with no active session (still reports, with an empty id).
        void end_session();

        // --- session socket ---
        void open_session_socket(const std::string& session_id, const std::string& ws_url);
        void close_session_socket();
        bool is_socket_open() const;

        // --- transform sync (TfNotice-as-source) ---
        // Attach the live stage for a loaded session; `remote` enables broadcasting.
        void attach_stage(ports::IStageBridge* stage, bool remote);
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

        bool           initialized_ = false;
        CollabPorts    ports_;
        CollabObserver* observer_ = nullptr;

        std::unique_ptr<RestClient>    rest_;
        std::unique_ptr<SessionSocket> socket_;

        bool remote_ = false;
        bool armed_ = false;
        bool applying_remote_ = false;

        // Identity of the session the high-level flow (begin_session) owns, so
        // end_session can tear down exactly what it created without the host
        // tracking the id.
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
