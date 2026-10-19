#pragma once

/**
 * @file SessionSocket.h
 * @brief Session protocol over a WebSocket: inbound message dispatch, immediate
 *        outbound send, and disconnect classification.
 *
 * It speaks the collaboration protocol through the wire codec and a WebSocket
 * port, holding no transport, protobuf, or engine types. An outbound edit is
 * encoded and sent the moment it is authored (one transform notice per node per
 * frame already bounds the rate); a per-prim last-sent cache drops an edit
 * identical to the one just sent for that prim. Acks are reported for
 * diagnostics only — the server resolves ordering, so nothing is resent.
 * Results are delivered through caller-supplied callbacks so any binding can
 * route them to its own sink.
 */

#include <functional>
#include <map>
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
    using OpenedCb = std::function<void()>;
    using HandshakeCb =
        std::function<void(const std::string& session_id, const std::string& usd_path, const std::string& usd_uri)>;
    using RemoteEditCb =
        std::function<void(const model::PrimEdit& edit, const std::string& from_client_id, uint64_t server_seq)>;
    using AckCb = std::function<void(bool ok, const std::string& error, uint64_t request_id, uint64_t server_seq)>;
    using ErrorCb = std::function<void(const std::string& code, const std::string& message)>;
    using DisconnectCb = std::function<void(model::CloseReason reason, int code, const std::string& text)>;
    using SnapshotCompleteCb = std::function<void(uint64_t server_seq)>;

    SessionSocket(ports::IWebSocketTransport* ws, ports::IClock* clock)
        : ws_(ws),
          clock_(clock)
    {
    }

    void set_session_id(const std::string& id)
    {
        session_id_ = id;
    }
    const std::string& session_id() const
    {
        return session_id_;
    }

    /// The engine updates this to the latest applied server_seq; outbound
    /// updates carry it as their base for the server's stale check.
    void set_base_server_seq(uint64_t seq)
    {
        base_server_seq_ = seq;
    }

    /// Open the socket, sending the bearer token on the upgrade handshake.
    void connect(const std::string& url, const std::string& bearer_token);
    void close();
    bool is_open() const
    {
        return is_open_;
    }

    /// Encode and send one edit immediately on the current base_server_seq_.
    /// No-op if the socket is closed or the edit is identical (payload only,
    /// ignoring the timestamp) to the last one sent for its prim — this drops
    /// the duplicate info-only USD notice.
    void send(const model::PrimEdit& edit);

    void on_opened(OpenedCb cb)
    {
        on_opened_ = std::move(cb);
    }
    void on_handshake(HandshakeCb cb)
    {
        on_handshake_ = std::move(cb);
    }
    void on_remote_edit(RemoteEditCb cb)
    {
        on_remote_edit_ = std::move(cb);
    }
    void on_ack(AckCb cb)
    {
        on_ack_ = std::move(cb);
    }
    void on_error(ErrorCb cb)
    {
        on_error_ = std::move(cb);
    }
    void on_disconnected(DisconnectCb cb)
    {
        on_disconnected_ = std::move(cb);
    }
    void on_snapshot_complete(SnapshotCompleteCb cb)
    {
        on_snapshot_complete_ = std::move(cb);
    }

  private:
    IDTX_LOG_CATEGORY("SessionSocket")

    void handle_binary(const std::string& bytes);
    void handle_state(ports::IWebSocketTransport::State state, int code, const std::string& reason);

    ports::IWebSocketTransport* ws_;
    ports::IClock* clock_;
    std::string session_id_;
    bool is_open_ = false;
    // Base server_seq carried on outbound updates (engine keeps it current).
    uint64_t base_server_seq_ = 0;
    // Per-connection monotonic request id for outbound updates (0 = unset).
    uint64_t next_request_id_ = 1;

    // Last edit sent per prim (payload only). Drops a repeated identical notice
    // for the same prim. Main-thread only; cleared on close so a reconnect
    // resends the current state.
    std::map<std::string, model::PrimEdit> last_sent_; // prim_path -> last sent edit

    OpenedCb on_opened_;
    HandshakeCb on_handshake_;
    RemoteEditCb on_remote_edit_;
    AckCb on_ack_;
    ErrorCb on_error_;
    DisconnectCb on_disconnected_;
    SnapshotCompleteCb on_snapshot_complete_;
};

} // namespace net
} // namespace idtxflow
