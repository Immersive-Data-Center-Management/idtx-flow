#pragma once

/**
 * @file WireCodec.h
 * @brief Translates the on-the-wire protobuf BaseMessage to and from the domain
 *        model. This is the only place the protobuf dependency lives; callers
 *        speak model types and never see a generated message.
 *
 * Inbound frames decode into one of a small set of neutral results (handshake,
 * remote edit, ack, error). Outbound transform edits encode into serialized
 * bytes ready to send. Matrix data uses the wire's row-major convention exactly.
 */

#include <string>

#include <idtxflow/net/model/Types.h>

namespace idtxflow
{
namespace net
{
namespace wire
{
/// The handshake that opens a session: identifiers and the USD location.
struct HandshakeMsg
{
    std::string session_id;
    std::string usd_path;
    std::string usd_uri;
};

/// A transform edit broadcast by a peer.
struct RemoteEditMsg
{
    std::string from_client_id;
    model::PrimEdit edit;
};

/// Acknowledgement of a submitted update.
struct AckMsg
{
    bool ok = false;
    std::string error;
};
/// A protocol-level error reported by the backend.
struct ErrorMsg
{
    std::string code;
    std::string message;
};

/// A decoded inbound frame: exactly one payload is populated per `kind`.
struct DecodedMessage
{
    enum class Kind
    {
        None,
        Handshake,
        RemoteEdit,
        Ack,
        Error,
        SnapshotComplete
    };

    Kind kind = Kind::None;
    HandshakeMsg handshake;
    RemoteEditMsg remote_edit;
    AckMsg ack;
    ErrorMsg error;

    // Envelope ordering fields (see base.proto): server_seq of the described
    // state (0 = none), and request_id echoed on an Ack (0 = unset).
    uint64_t server_seq = 0;
    uint64_t request_id = 0;
};

/// Parse a serialized BaseMessage. Returns false if the bytes don't parse or
/// carry a payload this client doesn't handle (out.kind stays None).
bool decode(const std::string& bytes, DecodedMessage& out);

/// Serialize a TransformUpdate BaseMessage for one prim edit. The matrix is
/// written row-major (m00..m33); the separate form fills the T/R/S fields.
/// @p base_server_seq is the client's current applied server_seq (the update's
/// base for the server's stale check); @p request_id correlates the Ack.
std::string encode_transform_update(const std::string& session_id, const model::PrimEdit& edit,
                                    uint64_t base_server_seq, uint64_t request_id);

/// Assert at startup that the linked protobuf runtime matches the headers the
/// generated messages were compiled against, failing fast on a version skew
/// instead of corrupting memory later. A no-op when versions agree.
void verify_protobuf_version();

} // namespace wire
} // namespace net
} // namespace idtxflow
