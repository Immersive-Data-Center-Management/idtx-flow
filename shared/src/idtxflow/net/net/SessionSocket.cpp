#include <idtxflow/net/net/SessionSocket.h>

#include <map>
#include <utility>

#include <idtxflow/net/wire/Codec.h>

namespace idtxflow
{
namespace net
{

void SessionSocket::connect(const std::string& url, const std::string& bearer_token)
{
    ws_->set_on_binary([this](const std::string& bytes) { handle_binary(bytes); });
    ws_->set_on_state([this](ports::IWebSocketTransport::State state, int code, std::string reason)
    {
        handle_state(state, code, reason);
    });

    if (!bearer_token.empty())
    {
        ws_->set_headers({ {"Authorization", "Bearer " + bearer_token} });
    }

    ws_->connect(url);
}

void SessionSocket::close()
{
    ws_->close();
    is_open_ = false;
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_.clear();
}

void SessionSocket::handle_binary(const std::string& bytes)
{
    wire::DecodedMessage decoded;
    if (!wire::decode(bytes, decoded))
    {
        return;
    }

    switch (decoded.kind)
    {
    case wire::DecodedMessage::Kind::Handshake:
        if (on_handshake_)
        {
            on_handshake_(decoded.handshake.session_id,
                          decoded.handshake.usd_path,
                          decoded.handshake.usd_uri);
        }
        break;
    case wire::DecodedMessage::Kind::RemoteEdit:
        if (on_remote_edit_)
        {
            on_remote_edit_(decoded.remote_edit.edit, decoded.remote_edit.from_client_id);
        }
        break;
    case wire::DecodedMessage::Kind::Ack:
        if (on_ack_) on_ack_(decoded.ack.ok, decoded.ack.error);
        break;
    case wire::DecodedMessage::Kind::Error:
        IDTX_LOG(IDTX_WARN, "inbound session error code='{}' msg='{}'",
                 decoded.error.code, decoded.error.message);
        if (on_error_) on_error_(decoded.error.code, decoded.error.message);
        break;
    default:
        break;
    }
}

void SessionSocket::handle_state(ports::IWebSocketTransport::State state, int code,
                                 const std::string& reason)
{
    using State = ports::IWebSocketTransport::State;
    switch (state)
    {
    case State::Connected:
        is_open_ = true;
        if (on_opened_) on_opened_();
        break;
    case State::Disconnected:
        // A close frame arrived; interpret its code/reason as an end-of-session cause.
        is_open_ = false;
        if (on_disconnected_) on_disconnected_(model::parse(code, reason), code, reason);
        break;
    case State::Error:
        // The socket failed or its upgrade was rejected, so it never carried a
        // clean close frame; classify it as a transport failure.
        is_open_ = false;
        if (on_disconnected_) on_disconnected_(model::transport_failure(), 0, reason);
        break;
    default:
        break;
    }
}

void SessionSocket::send_edit(const model::PrimEdit& edit)
{
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_[edit.prim_path] = edit;
}

void SessionSocket::poll()
{
    flush_pending();
}

void SessionSocket::flush_pending()
{
    if (!is_open_)
    {
        IDTX_LOG(IDTX_DEBUG, "[trace] F flush_pending SKIP: socket not open (is_open_=false)");
        return;
    }

    std::map<std::string, model::PrimEdit> to_send;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        if (pending_.empty())
        {
            return;
        }
        to_send.swap(pending_);
    }

    IDTX_LOG(IDTX_DEBUG, "[trace] F flush_pending sending {} edit(s) sid='{}'",
             to_send.size(), session_id_);
    const int64_t ts = clock_->now_millis();
    for (auto& [prim_path, edit] : to_send)
    {
        edit.timestamp = ts;
        IDTX_LOG(IDTX_DEBUG, "[trace] F send_binary prim='{}'", prim_path);
        ws_->send_binary(wire::encode_transform_update(session_id_, edit));
    }
}

} // namespace net
} // namespace idtxflow
