#include <idtxflow/net/protocol/SessionSocket.h>

#include <limits>

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
    {
        std::lock_guard<std::mutex> lock(mutex_);
        is_open_ = false;
        last_sent_.clear();
        pending_requests_.clear();
    }
    // close() may deliver a callback that destroys this socket. Finish all
    // local state changes before handing control to the transport.
    ws_->close();
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
    case wire::DecodedMessage::Kind::Handshake: {
        HandshakeCb callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = on_handshake_;
        }
        if (callback) callback(decoded.handshake.session_id, decoded.handshake.usd_path, decoded.handshake.usd_uri);
        break;
    }
    case wire::DecodedMessage::Kind::RemoteEdit: {
        RemoteEditCb callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = on_remote_edit_;
        }
        if (callback) callback(decoded.remote_edit.edit, decoded.remote_edit.from_client_id, decoded.server_seq);
        break;
    }
    case wire::DecodedMessage::Kind::Ack: {
        AckCb callback;
        model::RequestKind kind = model::RequestKind::Unknown;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto pending = pending_requests_.find(decoded.request_id);
            if (pending != pending_requests_.end())
            {
                kind = pending->second;
                pending_requests_.erase(pending);
            }
            callback = on_ack_;
        }
        // Erase before notification: an immediate callback can send another
        // request, deliver a duplicate Ack, or destroy this socket.
        if (callback) callback(decoded.ack.ok, decoded.ack.error, decoded.request_id, decoded.server_seq, kind);
        break;
    }
    case wire::DecodedMessage::Kind::Error: {
        ErrorCb callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = on_error_;
        }
        IDTX_LOG(IDTX_WARN, "inbound session error code='{}' msg='{}'", decoded.error.code, decoded.error.message);
        if (callback) callback(decoded.error.code, decoded.error.message);
        break;
    }
    case wire::DecodedMessage::Kind::SnapshotComplete: {
        SnapshotCompleteCb callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = on_snapshot_complete_;
        }
        if (callback) callback(decoded.server_seq);
        break;
    }
    default:
        break;
    }
}

void SessionSocket::handle_state(ports::IWebSocketTransport::State state, int code, const std::string& reason)
{
    using State = ports::IWebSocketTransport::State;
    switch (state)
    {
    case State::Connected: {
        OpenedCb callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            is_open_ = true;
            last_sent_.clear();
            pending_requests_.clear();
            callback = on_opened_;
        }
        if (callback) callback();
        break;
    }
    case State::Disconnected: {
        // A close frame arrived; interpret its code/reason as an end-of-session cause.
        // Drop the last-sent cache so a reconnect resends current state against the
        // fresh base rather than deduping against a value the snapshot may have reset.
        DisconnectCb callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            is_open_ = false;
            last_sent_.clear();
            pending_requests_.clear();
            callback = on_disconnected_;
        }
        if (callback) callback(model::parse(code, reason), code, reason);
        break;
    }
    case State::Error: {
        // The socket failed or its upgrade was rejected, so it never carried a
        // clean close frame; classify it as a transport failure.
        DisconnectCb callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            is_open_ = false;
            last_sent_.clear();
            pending_requests_.clear();
            callback = on_disconnected_;
        }
        if (callback) callback(model::transport_failure(), 0, reason);
        break;
    }
    default:
        break;
    }
}

uint64_t SessionSocket::register_request(model::RequestKind kind)
{
    if (next_request_id_ > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) return 0;
    constexpr size_t kMaxPendingRequests = 4096;
    if (pending_requests_.size() >= kMaxPendingRequests) pending_requests_.erase(pending_requests_.begin());
    const uint64_t request_id = next_request_id_++;
    pending_requests_.emplace(request_id, kind);
    return request_id;
}

uint64_t SessionSocket::send(const model::PrimEdit& edit)
{
    uint64_t request_id;
    std::string bytes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_open_)
        {
            IDTX_LOG(IDTX_DEBUG, "[trace] F send SKIP: socket not open (is_open_=false) prim='{}'", edit.prim_path);
            return 0;
        }

        // Drop an edit identical to the last one sent for this prim (the duplicate
        // info-only USD notice): nothing changed, so there is nothing to broadcast.
        auto it = last_sent_.find(edit.prim_path);
        if (it != last_sent_.end() && same_payload(it->second, edit))
        {
            IDTX_LOG(IDTX_DEBUG, "[trace] F send SKIP: duplicate of last sent prim='{}'", edit.prim_path);
            return 0;
        }

        model::PrimEdit out = edit;
        out.timestamp = clock_->now_millis();
        request_id = register_request(model::RequestKind::Transform);
        if (!request_id) return 0;
        IDTX_LOG(IDTX_DEBUG, "[trace] F send prim='{}' base_seq={} req_id={}", edit.prim_path, base_server_seq_,
                 request_id);
        bytes = wire::encode_transform_update(session_id_, out, base_server_seq_, request_id);
        // Record before transport delivery: a synchronous Ack can recursively
        // send a newer value or retire this socket. Never overwrite it afterward.
        last_sent_[edit.prim_path] = out;
    }
    ws_->send_binary(bytes);
    return request_id;
}

uint64_t SessionSocket::send_create_prim(const model::PrimEdit& edit)
{
    uint64_t request_id;
    std::string bytes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_open_)
        {
            IDTX_LOG(IDTX_DEBUG, "send_create_prim SKIP: socket not open parent='{}'", edit.prim_path);
            return 0;
        }
        request_id = register_request(model::RequestKind::CreatePrim);
        if (!request_id) return 0;
        IDTX_LOG(IDTX_DEBUG, "send_create_prim parent='{}' base_seq={} req_id={}", edit.prim_path, base_server_seq_,
                 request_id);
        bytes = wire::encode_create_prim_request(session_id_, edit, base_server_seq_, request_id);
    }
    ws_->send_binary(bytes);
    return request_id;
}

uint64_t SessionSocket::send_set_prim_active(const model::PrimEdit& edit)
{
    uint64_t request_id;
    std::string bytes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!is_open_)
        {
            IDTX_LOG(IDTX_DEBUG, "send_set_prim_active SKIP: socket not open prim='{}'", edit.prim_path);
            return 0;
        }
        request_id = register_request(edit.active ? model::RequestKind::RestorePrim : model::RequestKind::DeletePrim);
        if (!request_id) return 0;
        IDTX_LOG(IDTX_DEBUG, "send_set_prim_active prim='{}' active={} base_seq={} req_id={}", edit.prim_path,
                 edit.active, base_server_seq_, request_id);
        bytes = wire::encode_set_prim_active_request(session_id_, edit, base_server_seq_, request_id);
    }
    ws_->send_binary(bytes);
    return request_id;
}

} // namespace net
} // namespace idtxflow
