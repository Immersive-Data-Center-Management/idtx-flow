#include <idtxflow/net/IdtxSessionSocket.h>

#include <chrono>
#include <cstdint>
#include <utility>

#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketSendData.h>

#include "base.pb.h"
#include "transform.pb.h"
#include "session.pb.h"

#include <idtxflow/utils/Logger.h>

namespace idtxflow
{
namespace net
{
namespace
{
    static constexpr const char* LOG_CATEGORY = "IdtxSessionSocket";

    int64_t NowMillis()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }
} // namespace

IdtxSessionSocket::IdtxSessionSocket() = default;

IdtxSessionSocket::~IdtxSessionSocket()
{
    close();
}

void IdtxSessionSocket::connect_to(const std::string& ws_url, const std::string& bearer_token)
{
    if (ws_)
    {
        close();
    }

    ws_ = std::make_unique<ix::WebSocket>();
    ws_->setUrl(ws_url);

    // Attach the bearer token to the upgrade handshake (backend JWT middleware
    // inspects it on connect).
    if (!bearer_token.empty())
    {
        ix::WebSocketHttpHeaders headers;
        headers["Authorization"] = "Bearer " + bearer_token;
        ws_->setExtraHeaders(headers);
    }

    ws_->setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg)
    {
        switch (msg->type)
        {
        case ix::WebSocketMessageType::Open:
            is_open_ = true;
            if (on_opened_) on_opened_();
            break;

        case ix::WebSocketMessageType::Message:
            if (msg->binary)
            {
                handle_binary(msg->str);
            }
            break;

        case ix::WebSocketMessageType::Close:
            is_open_ = false;
            if (on_disconnected_)
            {
                on_disconnected_(static_cast<int>(msg->closeInfo.code), msg->closeInfo.reason);
            }
            break;

        case ix::WebSocketMessageType::Error:
            if (on_error_)
            {
                on_error_("transport_error", msg->errorInfo.reason);
            }
            break;

        default:
            break;
        }
    });

    ws_->start();
}

void IdtxSessionSocket::close()
{
    if (ws_)
    {
        ws_->stop();
        ws_.reset();
    }
    is_open_ = false;
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_.clear();
}

void IdtxSessionSocket::handle_binary(const std::string& payload)
{
    idtxcore::BaseMessage msg;
    if (!msg.ParseFromArray(payload.data(), static_cast<int>(payload.size())))
    {
        IDTX_LOG(IDTX_ERROR, "Failed to parse inbound BaseMessage ({} bytes)", payload.size());
        return;
    }

    switch (msg.message_case())
    {
    case idtxcore::BaseMessage::kHandshake:
    {
        const auto& h = msg.handshake();
        if (on_handshake_)
        {
            on_handshake_(h.session_id(), h.usd_path(), h.usd_uri());
        }
        break;
    }
    case idtxcore::BaseMessage::kXformBroadcast:
    {
        const auto& b = msg.xform_broadcast();
        const auto& u = b.update();

        TransformBroadcastMsg out;
        out.from_client_id = b.client_id();
        out.prim_path = u.prim_path();

        if (u.has_matrix())
        {
            out.is_matrix = true;
            const auto& m = u.matrix();
            out.matrix.m = {
                m.m00(), m.m01(), m.m02(), m.m03(),
                m.m10(), m.m11(), m.m12(), m.m13(),
                m.m20(), m.m21(), m.m22(), m.m23(),
                m.m30(), m.m31(), m.m32(), m.m33()
            };
        }
        else if (u.has_seperate())   // NOTE: backend field name is "seperate"
        {
            out.is_matrix = false;
            const auto& s = u.seperate();
            out.separate.translation[0] = s.translation().x();
            out.separate.translation[1] = s.translation().y();
            out.separate.translation[2] = s.translation().z();
            out.separate.rotation[0] = s.rotation().x();
            out.separate.rotation[1] = s.rotation().y();
            out.separate.rotation[2] = s.rotation().z();
            out.separate.scale[0] = s.scale().x();
            out.separate.scale[1] = s.scale().y();
            out.separate.scale[2] = s.scale().z();
        }

        if (on_broadcast_) on_broadcast_(out);
        break;
    }
    case idtxcore::BaseMessage::kAck:
    {
        const auto& a = msg.ack();
        if (on_ack_) on_ack_(a.ok(), a.error());
        break;
    }
    case idtxcore::BaseMessage::kError:
    {
        const auto& e = msg.error();
        if (on_error_) on_error_(e.code(), e.message());
        break;
    }
    default:
        break;
    }
}

void IdtxSessionSocket::send_serialized(const std::string& bytes)
{
    if (ws_ && is_open_)
    {
        ws_->sendBinary(bytes);
    }
}

void IdtxSessionSocket::send_transform_update_matrix(const std::string& prim_path, const Mat4& matrix)
{
    std::lock_guard<std::mutex> lock(pending_mutex_);
    PendingUpdate& p = pending_[prim_path];
    p.is_matrix = true;
    p.matrix = matrix;
}

void IdtxSessionSocket::send_transform_update_separate(const std::string& prim_path, const SeparateXform& xform)
{
    std::lock_guard<std::mutex> lock(pending_mutex_);
    PendingUpdate& p = pending_[prim_path];
    p.is_matrix = false;
    p.separate = xform;
}

void IdtxSessionSocket::poll()
{
    flush_pending();
}

void IdtxSessionSocket::flush_pending()
{
    if (!is_open_)
    {
        return;
    }

    std::map<std::string, PendingUpdate> to_send;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        if (pending_.empty())
        {
            return;
        }
        to_send.swap(pending_);
    }

    const int64_t ts = NowMillis();

    for (auto& [prim_path, p] : to_send)
    {
        idtxcore::BaseMessage msg;
        msg.set_session_id(session_id_);

        idtxcore::TransformUpdate* upd = msg.mutable_xform_update();
        upd->set_session_id(session_id_);
        upd->set_usd_file("");           // backend ignores; prim_path is authoritative
        upd->set_prim_path(prim_path);
        upd->set_timestamp(ts);

        if (p.is_matrix)
        {
            idtxcore::Matrix4dTransform* m = upd->mutable_matrix();
            const auto& a = p.matrix.m;
            m->set_m00(a[0]);  m->set_m01(a[1]);  m->set_m02(a[2]);  m->set_m03(a[3]);
            m->set_m10(a[4]);  m->set_m11(a[5]);  m->set_m12(a[6]);  m->set_m13(a[7]);
            m->set_m20(a[8]);  m->set_m21(a[9]);  m->set_m22(a[10]); m->set_m23(a[11]);
            m->set_m30(a[12]); m->set_m31(a[13]); m->set_m32(a[14]); m->set_m33(a[15]);
        }
        else
        {
            idtxcore::SeparateTransform* s = upd->mutable_seperate();  // "seperate" (sic)
            s->mutable_translation()->set_x(p.separate.translation[0]);
            s->mutable_translation()->set_y(p.separate.translation[1]);
            s->mutable_translation()->set_z(p.separate.translation[2]);
            s->mutable_rotation()->set_x(p.separate.rotation[0]);
            s->mutable_rotation()->set_y(p.separate.rotation[1]);
            s->mutable_rotation()->set_z(p.separate.rotation[2]);
            s->mutable_scale()->set_x(p.separate.scale[0]);
            s->mutable_scale()->set_y(p.separate.scale[1]);
            s->mutable_scale()->set_z(p.separate.scale[2]);
        }

        std::string bytes;
        if (msg.SerializeToString(&bytes))
        {
            send_serialized(bytes);
        }
    }
}

} // namespace net
} // namespace idtxflow