#include <idtxflow/net/protocol/SessionSocket.h>

#include <idtxflow/net/wire/WireCodec.h>

namespace idtxflow
{
namespace net
{
namespace
{
// Payload-only equality: whether two edits author the same transform to the
// same prim. The timestamp is stamped per send, so it is deliberately ignored.
bool same_payload(const model::PrimEdit& a, const model::PrimEdit& b)
{
    if (a.kind != b.kind || a.is_matrix != b.is_matrix) return false;
    if (a.is_matrix)
    {
        return a.matrix.m == b.matrix.m; // std::array has operator==
    }
    const model::SeparateXform& x = a.separate;
    const model::SeparateXform& y = b.separate;
    for (int i = 0; i < 3; ++i)
    {
        if (x.translation[i] != y.translation[i] || x.rotation[i] != y.rotation[i] || x.scale[i] != y.scale[i])
            return false;
    }
    return true;
}
} // namespace

void SessionSocket::connect(const std::string& url, const std::string& bearer_token)
{
    ws_->set_on_binary([this](const std::string& bytes) { handle_binary(bytes); });
    ws_->set_on_state([this](ports::IWebSocketTransport::State state, int code, std::string reason) {
        handle_state(state, code, reason);
    });

    if (!bearer_token.empty())
    {
        ws_->set_headers({{"Authorization", "Bearer " + bearer_token}});
    }

    ws_->connect(url);
}

void SessionSocket::close()
{
    ws_->close();
    is_open_ = false;
    last_sent_.clear();
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
            on_handshake_(decoded.handshake.session_id, decoded.handshake.usd_path, decoded.handshake.usd_uri);
        }
        break;
    case wire::DecodedMessage::Kind::RemoteEdit:
        if (on_remote_edit_)
        {
            on_remote_edit_(decoded.remote_edit.edit, decoded.remote_edit.from_client_id, decoded.server_seq);
        }
        break;
    case wire::DecodedMessage::Kind::Ack:
        if (on_ack_) on_ack_(decoded.ack.ok, decoded.ack.error, decoded.request_id, decoded.server_seq);
        break;
    case wire::DecodedMessage::Kind::Error:
        IDTX_LOG(IDTX_WARN, "inbound session error code='{}' msg='{}'", decoded.error.code, decoded.error.message);
        if (on_error_) on_error_(decoded.error.code, decoded.error.message);
        break;
    case wire::DecodedMessage::Kind::SnapshotComplete:
        if (on_snapshot_complete_) on_snapshot_complete_(decoded.server_seq);
        break;
    default:
        break;
    }
}

void SessionSocket::handle_state(ports::IWebSocketTransport::State state, int code, const std::string& reason)
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
        // Drop the last-sent cache so a reconnect resends current state against the
        // fresh base rather than deduping against a value the snapshot may have reset.
        last_sent_.clear();
        if (on_disconnected_) on_disconnected_(model::parse(code, reason), code, reason);
        break;
    case State::Error:
        // The socket failed or its upgrade was rejected, so it never carried a
        // clean close frame; classify it as a transport failure.
        is_open_ = false;
        last_sent_.clear();
        if (on_disconnected_) on_disconnected_(model::transport_failure(), 0, reason);
        break;
    default:
        break;
    }
}

void SessionSocket::send(const model::PrimEdit& edit)
{
    if (!is_open_)
    {
        IDTX_LOG(IDTX_DEBUG, "[trace] F send SKIP: socket not open (is_open_=false) prim='{}'", edit.prim_path);
        return;
    }

    // Drop an edit identical to the last one sent for this prim (the duplicate
    // info-only USD notice): nothing changed, so there is nothing to broadcast.
    auto it = last_sent_.find(edit.prim_path);
    if (it != last_sent_.end() && same_payload(it->second, edit))
    {
        IDTX_LOG(IDTX_DEBUG, "[trace] F send SKIP: duplicate of last sent prim='{}'", edit.prim_path);
        return;
    }

    model::PrimEdit out = edit;
    out.timestamp = clock_->now_millis();
    const uint64_t request_id = next_request_id_++;
    IDTX_LOG(IDTX_DEBUG, "[trace] F send prim='{}' base_seq={} req_id={}", edit.prim_path, base_server_seq_,
             request_id);
    ws_->send_binary(wire::encode_transform_update(session_id_, out, base_server_seq_, request_id));
    last_sent_[edit.prim_path] = out;
}

} // namespace net
} // namespace idtxflow
