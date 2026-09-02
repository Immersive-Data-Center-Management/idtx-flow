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
                                                const std::string& from_client_id)>;
        using AckCb        = std::function<void(bool ok, const std::string& error)>;
        using ErrorCb      = std::function<void(const std::string& code, const std::string& message)>;
        using DisconnectCb = std::function<void(model::CloseReason reason, int code,
                                                const std::string& text)>;

        SessionSocket(ports::IWebSocketTransport* ws, ports::IClock* clock)
            : ws_(ws), clock_(clock) {}

        void set_session_id(const std::string& id) { session_id_ = id; }
        const std::string& session_id() const { return session_id_; }

        /// Open the socket, sending the bearer token on the upgrade handshake.
        void connect(const std::string& url, const std::string& bearer_token);
        void close();
        bool is_open() const { return is_open_; }

        /// Queue an outbound edit; the latest edit per prim wins until the next poll.
        void send_edit(const model::PrimEdit& edit);

        /// Send one coalesced frame per pending prim. Call once per frame.
        void poll();

        void on_opened(OpenedCb cb)         { on_opened_ = std::move(cb); }
        void on_handshake(HandshakeCb cb)   { on_handshake_ = std::move(cb); }
        void on_remote_edit(RemoteEditCb cb){ on_remote_edit_ = std::move(cb); }
        void on_ack(AckCb cb)               { on_ack_ = std::move(cb); }
        void on_error(ErrorCb cb)           { on_error_ = std::move(cb); }
        void on_disconnected(DisconnectCb cb){ on_disconnected_ = std::move(cb); }

    private:
        IDTX_LOG_CATEGORY("SessionSocket")

        void handle_binary(const std::string& bytes);
        void handle_state(ports::IWebSocketTransport::State state, int code, const std::string& reason);
        void flush_pending();

        ports::IWebSocketTransport* ws_;
        ports::IClock*              clock_;
        std::string                 session_id_;
        bool                        is_open_ = false;

        std::mutex                             pending_mutex_;
        std::map<std::string, model::PrimEdit> pending_;   // prim_path -> latest edit

        OpenedCb     on_opened_;
        HandshakeCb  on_handshake_;
        RemoteEditCb on_remote_edit_;
        AckCb        on_ack_;
        ErrorCb      on_error_;
        DisconnectCb on_disconnected_;
    };

} // namespace net
} // namespace idtxflow
