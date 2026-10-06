#pragma once

/**
 * @file IWebSocketTransport.h
 * @brief Port for the session WebSocket: send/receive binary frames and observe
 *        connection state. The wire protocol and coalescing live in the core; the
 *        concrete socket library stays behind this interface.
 *
 * Implementer contract:
 *   - Configure before connecting: set_headers() and the callbacks are
 *     registered prior to connect().
 *   - Report the connection lifecycle through OnState in order:
 *     Connecting -> Connected, then Disconnected or Error on teardown.
 *   - Callbacks may be delivered on a background thread; the core marshals them
 *     to the main thread, so the implementation need not.
 *   - Ownership of the network loop is the implementation's choice: a
 *     self-threaded socket may no-op poll(); otherwise poll() must pump I/O.
 *
 * Usage:
 * @code
 *   ws->set_on_binary([](const std::string& bytes) { handle_frame(bytes); });
 *   ws->set_on_state([](IWebSocketTransport::State s, int code, std::string reason) {
 *       // s == Connected -> ready to send; Disconnected/Error -> tear down
 *   });
 *   ws->set_headers({ {"Authorization", "Bearer " + token} });
 *   ws->connect(url);                 // async; progress arrives via OnState
 *   // ...later, once Connected...
 *   if (ws->is_open()) ws->send_binary(frame);
 *   ws->close();
 * @endcode
 */

#include <functional>
#include <map>
#include <string>

namespace idtxflow
{
namespace net
{
namespace ports
{
struct IWebSocketTransport
{
    virtual ~IWebSocketTransport() = default;

    /// Connection lifecycle states reported through OnState.
    enum class State
    {
        Disconnected,
        Connecting,
        Connected,
        Error
    };

    /// Inbound binary frame payload (raw bytes).
    using OnBinary = std::function<void(const std::string&)>;
    /// Connection-state change: (state, code, reason). `code` is the
    /// WebSocket close code on Disconnected (0 otherwise); `reason` is the
    /// close/error text when available, empty otherwise.
    using OnState = std::function<void(State, int, std::string)>;

    /// Headers sent on the upgrade (e.g. Authorization).
    virtual void set_headers(std::map<std::string, std::string> headers) = 0;

    /// Open the socket to `url`, replacing any existing connection.
    virtual void connect(std::string url) = 0;
    /// Close the connection and release the socket.
    virtual void close() = 0;
    /// Send one binary frame; no-op if the socket is not open.
    virtual void send_binary(const std::string& bytes) = 0;
    /// True while the socket is open.
    virtual bool is_open() const = 0;

    /// Register the inbound-binary-frame callback.
    virtual void set_on_binary(OnBinary callback) = 0;
    /// Register the connection-state callback.
    virtual void set_on_state(OnState callback) = 0;

    /// Pump the transport's I/O; a self-threaded implementation may no-op.
    virtual void poll() = 0;
};

} // namespace ports
} // namespace net
} // namespace idtxflow
