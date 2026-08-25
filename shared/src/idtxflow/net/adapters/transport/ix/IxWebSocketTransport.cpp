#include <idtxflow/net/adapters/transport/ix/IxWebSocketTransport.h>

#include <utility>

#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketSendData.h>

namespace idtxflow
{
namespace net
{
namespace adapters
{

IxWebSocketTransport::IxWebSocketTransport() = default;

IxWebSocketTransport::~IxWebSocketTransport()
{
    close();
}

void IxWebSocketTransport::set_headers(std::map<std::string, std::string> headers)
{
    headers_ = std::move(headers);
}

void IxWebSocketTransport::connect(std::string url)
{
    if (ws_)
    {
        close();
    }

    ws_ = std::make_unique<ix::WebSocket>();
    ws_->setUrl(url);

    if (!headers_.empty())
    {
        ix::WebSocketHttpHeaders headers;
        for (const auto& [key, value] : headers_)
        {
            headers[key] = value;
        }
        ws_->setExtraHeaders(headers);
    }

    ws_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg)
    {
        switch (msg->type)
        {
        case ix::WebSocketMessageType::Open:
            is_open_ = true;
            if (on_state_) on_state_(State::Connected, 0, std::string());
            break;

        case ix::WebSocketMessageType::Message:
            if (msg->binary && on_binary_)
            {
                on_binary_(msg->str);
            }
            break;

        case ix::WebSocketMessageType::Close:
            is_open_ = false;
            if (on_state_)
            {
                on_state_(State::Disconnected,
                          static_cast<int>(msg->closeInfo.code),
                          msg->closeInfo.reason);
            }
            break;

        case ix::WebSocketMessageType::Error:
            if (on_state_)
            {
                on_state_(State::Error, 0, msg->errorInfo.reason);
            }
            break;

        default:
            break;
        }
    });

    if (on_state_) on_state_(State::Connecting, 0, std::string());
    ws_->start();
}

void IxWebSocketTransport::close()
{
    if (ws_)
    {
        ws_->stop();
        ws_.reset();
    }
    is_open_ = false;
}

void IxWebSocketTransport::send_binary(const std::string& bytes)
{
    IDTX_LOG(IDTX_DEBUG, "[trace] G Ix::send_binary ws={} is_open_={} bytes={}",
             ws_ != nullptr, is_open_, bytes.size());
    if (ws_ && is_open_)
    {
        ws_->sendBinary(bytes);
    }
}

bool IxWebSocketTransport::is_open() const
{
    return is_open_;
}

void IxWebSocketTransport::set_on_binary(OnBinary callback)
{
    on_binary_ = std::move(callback);
}

void IxWebSocketTransport::set_on_state(OnState callback)
{
    on_state_ = std::move(callback);
}

void IxWebSocketTransport::poll()
{
    // IXWebSocket runs its own network thread and delivers frames via the message
    // callback, so there is nothing to pump here.
}

} // namespace adapters
} // namespace net
} // namespace idtxflow
