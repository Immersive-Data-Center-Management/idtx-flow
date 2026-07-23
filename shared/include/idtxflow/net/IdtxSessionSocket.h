#pragma once

/**
 * @file IdtxSessionSocket.h
 * @brief Engine-agnostic WebSocket client + protobuf codec for an IDTX session.
 *
 * Speaks the collaboration wire protocol: binary frames, each a serialized
 * `idtxcore.BaseMessage`. Connects with an `Authorization: Bearer <token>` header
 * on the upgrade, dispatches inbound messages (handshake / transform broadcast /
 * ack / error) via std::function callbacks, and sends outbound TransformUpdates.
 *
 * No engine (Godot) types: transforms cross this boundary as a neutral 4x4
 * row-major matrix (Mat4) or a SeparateXform POD. The Godot shim converts to/from
 * Transform3D. Callbacks fire on the IXWebSocket network thread; the shim marshals
 * them onto the main thread.
 *
 * Outbound coalescing (≤ ~60 Hz per prim) is handled here so gizmo drags don't
 * flood the socket; drain is triggered from poll().
 */

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace ix { class WebSocket; }

namespace idtxflow
{
namespace net
{
    /// Row-major 4x4 transform (matches idtxcore.Matrix4dTransform m00..m33).
    struct Mat4
    {
        std::array<double, 16> m{ {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1} };
    };

    /// Separate translation / rotation(euler degrees) / scale (matches SeparateTransform).
    struct SeparateXform
    {
        double translation[3]{0,0,0};
        double rotation[3]{0,0,0};   ///< Euler degrees (USD convention)
        double scale[3]{1,1,1};
    };

    /// A decoded inbound transform broadcast.
    struct TransformBroadcastMsg
    {
        std::string from_client_id;
        std::string prim_path;
        bool        is_matrix = true;
        Mat4        matrix;
        SeparateXform separate;
    };

    // Callbacks (fired on the network thread).
    using OpenedCallback     = std::function<void()>;
    using HandshakeCallback  = std::function<void(const std::string& session_id,
                                                  const std::string& usd_path,
                                                  const std::string& usd_uri)>;
    using BroadcastCallback  = std::function<void(const TransformBroadcastMsg&)>;
    using AckCallback        = std::function<void(bool ok, const std::string& error)>;
    using SocketErrorCallback= std::function<void(const std::string& code, const std::string& message)>;
    using DisconnectCallback = std::function<void(int close_code, const std::string& reason)>;

    /**
     * WebSocket session client. One instance per active session.
     *
     * Lifecycle:
     *   connect_to(url, token) → opened() → handshake_received(...) → live
     *   send_transform_update_*(...) queues outbound (coalesced), flushed on poll()
     *   inbound frames dispatched to the broadcast/ack/error callbacks
     *   close() / server close → disconnected(code, reason)
     */
    class IdtxSessionSocket
    {
    public:
        IdtxSessionSocket();
        ~IdtxSessionSocket();

        IdtxSessionSocket(const IdtxSessionSocket&) = delete;
        IdtxSessionSocket& operator=(const IdtxSessionSocket&) = delete;

        void set_session_id(const std::string& id) { session_id_ = id; }
        const std::string& session_id() const { return session_id_; }

        /// Open the socket; `bearer_token` is sent as `Authorization: Bearer <token>`.
        void connect_to(const std::string& ws_url, const std::string& bearer_token);

        /// Gracefully close.
        void close();

        bool is_open() const { return is_open_; }

        /// Queue an outbound TransformUpdate as a 4x4 matrix (coalesced per prim).
        void send_transform_update_matrix(const std::string& prim_path, const Mat4& matrix);

        /// Queue an outbound TransformUpdate as separate T/R/S (coalesced per prim).
        void send_transform_update_separate(const std::string& prim_path, const SeparateXform& xform);

        /// Drain coalesced outbound updates. Call periodically (e.g. from the shim's
        /// _process); safe to call from the main thread.
        void poll();

        // --- callback setters ---
        void on_opened(OpenedCallback cb)          { on_opened_ = std::move(cb); }
        void on_handshake(HandshakeCallback cb)    { on_handshake_ = std::move(cb); }
        void on_broadcast(BroadcastCallback cb)    { on_broadcast_ = std::move(cb); }
        void on_ack(AckCallback cb)                { on_ack_ = std::move(cb); }
        void on_error(SocketErrorCallback cb)      { on_error_ = std::move(cb); }
        void on_disconnected(DisconnectCallback cb){ on_disconnected_ = std::move(cb); }

    private:
        void handle_binary(const std::string& payload);      ///< parse+dispatch a BaseMessage
        void send_serialized(const std::string& bytes);      ///< raw binary send
        void flush_pending();                                 ///< emit coalesced updates

        std::unique_ptr<ix::WebSocket> ws_;
        std::string session_id_;
        bool        is_open_ = false;

        // Outbound coalescing: latest pending update per prim path.
        struct PendingUpdate
        {
            bool is_matrix = true;
            Mat4 matrix;
            SeparateXform separate;
        };
        std::mutex pending_mutex_;
        std::map<std::string, PendingUpdate> pending_;

        OpenedCallback      on_opened_;
        HandshakeCallback   on_handshake_;
        BroadcastCallback   on_broadcast_;
        AckCallback         on_ack_;
        SocketErrorCallback on_error_;
        DisconnectCallback  on_disconnected_;
    };

} // namespace net
} // namespace idtxflow