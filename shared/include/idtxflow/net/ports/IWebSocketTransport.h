#pragma once

/**
 * @file IWebSocketTransport.h
 * @brief Port for the session WebSocket: send/receive binary frames and observe
 *        connection state. The wire protocol and coalescing live in the core; the
 *        concrete socket library stays behind this interface.
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

        enum class State { Disconnected, Connecting, Connected, Error };

        using OnBinary = std::function<void(const std::string&)>;
        using OnState  = std::function<void(State, int, std::string)>;

        /// Headers sent on the upgrade (e.g. Authorization); set before connecting.
        virtual void set_headers(std::map<std::string, std::string> headers) = 0;

        virtual void connect(std::string url) = 0;
        virtual void close() = 0;
        virtual void send_binary(const std::string& bytes) = 0;
        virtual bool is_open() const = 0;

        virtual void set_on_binary(OnBinary callback) = 0;
        virtual void set_on_state(OnState callback) = 0;

        /// Kept for symmetry with the HTTP port; a self-threaded socket may no-op.
        virtual void poll() = 0;
    };

} // namespace ports
} // namespace net
} // namespace idtxflow
