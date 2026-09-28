#pragma once

/**
 * @file IxWebSocketTransport.h
 * @brief IWebSocketTransport backed by IXWebSocket's WebSocket.
 *
 * Opens the session socket with caller-supplied headers (e.g. the Authorization
 * upgrade header), forwards inbound binary frames, and reports connection-state
 * transitions. Protocol decoding and close-reason classification are the core's
 * concern, not this adapter's.
 *
 * The IXWebSocket type is forward-declared so this header stays standard-library
 * only; the dependency lives in the .cpp.
 */

#include <map>
#include <memory>
#include <string>

#include <idtxflow/net/ports/IWebSocketTransport.h>
#include <idtxflow/utils/Logger.h>

namespace ix { class WebSocket; }

namespace idtxflow
{
namespace net
{
namespace adapters
{
    class IxWebSocketTransport : public ports::IWebSocketTransport
    {
    public:
        IxWebSocketTransport();
        ~IxWebSocketTransport() override;

        IxWebSocketTransport(const IxWebSocketTransport&) = delete;
        IxWebSocketTransport& operator=(const IxWebSocketTransport&) = delete;

        void set_headers(std::map<std::string, std::string> headers) override;
        void connect(std::string url) override;
        void close() override;
        void send_binary(const std::string& bytes) override;
        bool is_open() const override;

        void set_on_binary(OnBinary callback) override;
        void set_on_state(OnState callback) override;

        void poll() override;

    private:
        IDTX_LOG_CATEGORY("IxWebSocketTransport")

        std::unique_ptr<ix::WebSocket>     ws_;
        std::map<std::string, std::string> headers_;
        bool                               is_open_ = false;
        OnBinary                           on_binary_;
        OnState                            on_state_;
    };

} // namespace adapters
} // namespace net
} // namespace idtxflow
