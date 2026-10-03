#pragma once

/**
 * @file SessionSocket.h
 * @brief Session protocol over a WebSocket: inbound message dispatch, per-prim
 *        outbound coalescing, and disconnect classification.
 *
 * It speaks the collaboration protocol through the wire codec and a WebSocket
 * port, holding no transport, protobuf, or engine types. Outbound edits are
 * collapsed to at most one frame per prim per poll so a burst of local changes
 * (e.g. a gizmo drag) produces a single update. Results are delivered through
 * caller-supplied callbacks so any binding can route them to its own sink.
 */

#include <functional>
#include <map>
#include <mutex>
#include <string>

#include <idtxflow/net/model/CloseReason.h>
#include <idtxflow/net/model/Types.h>
#include <idtxflow/net/ports/IClock.h>
#include <idtxflow/net/ports/IWebSocketTransport.h>
#include <idtxflow/utils/Logger.h>

namespace idtxflow
{
namespace net
{
    class SessionSocket
    {
    public:
        using OpenedCb    = std::function<void()>;
        using HandshakeCb = std::function<void(const std::string& session_id,
                                               const std::string& usd_path,
                                               const std::string& usd_uri)>;
        using RemoteEditCb = std::function<void(const model::PrimEdit& edit,
                                                const std::string& from_client_id,
                                                uint64_t server_seq)>;
        using AckCb        = std::function<void(bool ok, const std::string& error,
                                                uint64_t request_id, uint64_t server_seq)>;
        using ErrorCb      = std::function<void(const std::string& code, const std::string& message)>;
        using DisconnectCb = std::function<void(model::CloseReason reason, int code,
                                                const std::string& text)>;
        using SnapshotCompleteCb = std::function<void(uint64_t server_seq)>;

        SessionSocket(ports::IWebSocketTransport* ws, ports::IClock* clock)
            : ws_(ws), clock_(clock) {}

        void set_session_id(const std::string& id) { session_id_ = id; }
        const std::string& session_id() const { return session_id_; }

        /// The engine updates this to the latest applied server_seq; outbound
        /// updates carry it as their base for the server's stale check.
        void set_base_server_seq(uint64_t seq) { base_server_seq_ = seq; }

        /// Open the socket, sending the bearer token on the upgrade handshake.
        void connect(const std::string& url, const std::string& bearer_token);
        void close();
        bool is_open() const { return is_open_; }

        /// Queue an outbound edit; the latest edit per prim wins until the next poll.
        void send_edit(const model::PrimEdit& edit);

        /// Resolve an in-flight update by its Ack. ok=true drops the record;
        /// ok=false (stale/invalid_base/apply_failed/queue_full) re-queues the
        /// edit so the next flush resends it on the now-advanced base — the server
        /// authors nothing and sends no correction for stale/invalid_base, so the
        /// client must resend to converge. Correlated by request_id.
        void handle_ack(bool ok, const std::string& error, uint64_t request_id);

        /// Send one coalesced frame per pending prim. Call once per frame.
        void poll();

        void on_opened(OpenedCb cb)         { on_opened_ = std::move(cb); }
        void on_handshake(HandshakeCb cb)   { on_handshake_ = std::move(cb); }
        void on_remote_edit(RemoteEditCb cb){ on_remote_edit_ = std::move(cb); }
        void on_ack(AckCb cb)               { on_ack_ = std::move(cb); }
        void on_error(ErrorCb cb)           { on_error_ = std::move(cb); }
        void on_disconnected(DisconnectCb cb){ on_disconnected_ = std::move(cb); }
        void on_snapshot_complete(SnapshotCompleteCb cb){ on_snapshot_complete_ = std::move(cb); }

    private:
        IDTX_LOG_CATEGORY("SessionSocket")

        void handle_binary(const std::string& bytes);
        void handle_state(ports::IWebSocketTransport::State state, int code, const std::string& reason);
        void flush_pending();

        ports::IWebSocketTransport* ws_;
        ports::IClock*              clock_;
        std::string                 session_id_;
        bool                        is_open_ = false;
        // Base server_seq carried on outbound updates (engine keeps it current).
        uint64_t                    base_server_seq_ = 0;
        // Per-connection monotonic request id for outbound updates (0 = unset).
        uint64_t                    next_request_id_ = 1;

        std::mutex                             pending_mutex_;
        std::map<std::string, model::PrimEdit> pending_;   // prim_path -> latest edit
        // Edits sent and awaiting an Ack, keyed by request_id. On rejection we
        // re-queue the edit so it is resent on the advanced base; on success we
        // drop it. Bounded by in-flight requests (cleared as acks arrive).
        std::map<uint64_t, model::PrimEdit>    inflight_;   // request_id -> sent edit

        OpenedCb     on_opened_;
        HandshakeCb  on_handshake_;
        RemoteEditCb on_remote_edit_;
        AckCb        on_ack_;
        ErrorCb      on_error_;
        DisconnectCb on_disconnected_;
        SnapshotCompleteCb on_snapshot_complete_;
    };

} // namespace net
} // namespace idtxflow
