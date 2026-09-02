#include <idtxflow/net/wire/Codec.h>

#include "base.pb.h"
#include "transform.pb.h"
#include "session.pb.h"

namespace idtxflow
{
namespace net
{
namespace wire
{

bool decode(const std::string& bytes, DecodedMessage& out)
{
    out = DecodedMessage{};

    idtxcore::BaseMessage msg;
    if (!msg.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
    {
        return false;
    }

    switch (msg.message_case())
    {
    case idtxcore::BaseMessage::kHandshake:
    {
        const auto& h = msg.handshake();
        out.kind = DecodedMessage::Kind::Handshake;
        out.handshake.session_id = h.session_id();
        out.handshake.usd_path = h.usd_path();
        out.handshake.usd_uri = h.usd_uri();
        return true;
    }
    case idtxcore::BaseMessage::kXformBroadcast:
    {
        const auto& b = msg.xform_broadcast();
        const auto& u = b.update();

        out.kind = DecodedMessage::Kind::RemoteEdit;
        out.remote_edit.from_client_id = b.client_id();

        model::PrimEdit& edit = out.remote_edit.edit;
        edit.kind = model::PrimEdit::Kind::Transform;
        edit.prim_path = u.prim_path();

        if (u.has_matrix())
        {
            edit.is_matrix = true;
            const auto& m = u.matrix();
            edit.matrix.m = {
                m.m00(), m.m01(), m.m02(), m.m03(),
                m.m10(), m.m11(), m.m12(), m.m13(),
                m.m20(), m.m21(), m.m22(), m.m23(),
                m.m30(), m.m31(), m.m32(), m.m33()
            };
        }
        else if (u.has_seperate())   // the wire field name is intentionally "seperate"
        {
            edit.is_matrix = false;
            const auto& s = u.seperate();
            edit.separate.translation[0] = s.translation().x();
            edit.separate.translation[1] = s.translation().y();
            edit.separate.translation[2] = s.translation().z();
            edit.separate.rotation[0] = s.rotation().x();
            edit.separate.rotation[1] = s.rotation().y();
            edit.separate.rotation[2] = s.rotation().z();
            edit.separate.scale[0] = s.scale().x();
            edit.separate.scale[1] = s.scale().y();
            edit.separate.scale[2] = s.scale().z();
        }
        return true;
    }
    case idtxcore::BaseMessage::kAck:
    {
        const auto& a = msg.ack();
        out.kind = DecodedMessage::Kind::Ack;
        out.ack.ok = a.ok();
        out.ack.error = a.error();
        return true;
    }
    case idtxcore::BaseMessage::kError:
    {
        const auto& e = msg.error();
        out.kind = DecodedMessage::Kind::Error;
        out.error.code = e.code();
        out.error.message = e.message();
        return true;
    }
    default:
        return false;
    }
}

std::string encode_transform_update(const std::string& session_id, const model::PrimEdit& edit)
{
    idtxcore::BaseMessage msg;
    msg.set_session_id(session_id);

    idtxcore::TransformUpdate* upd = msg.mutable_xform_update();
    upd->set_session_id(session_id);
    upd->set_usd_file("");           // backend ignores; prim_path is authoritative
    upd->set_prim_path(edit.prim_path);
    upd->set_timestamp(edit.timestamp);

    if (edit.is_matrix)
    {
        idtxcore::Matrix4dTransform* m = upd->mutable_matrix();
        const auto& a = edit.matrix.m;
        m->set_m00(a[0]);  m->set_m01(a[1]);  m->set_m02(a[2]);  m->set_m03(a[3]);
        m->set_m10(a[4]);  m->set_m11(a[5]);  m->set_m12(a[6]);  m->set_m13(a[7]);
        m->set_m20(a[8]);  m->set_m21(a[9]);  m->set_m22(a[10]); m->set_m23(a[11]);
        m->set_m30(a[12]); m->set_m31(a[13]); m->set_m32(a[14]); m->set_m33(a[15]);
    }
    else
    {
        idtxcore::SeparateTransform* s = upd->mutable_seperate();   // "seperate" (sic)
        s->mutable_translation()->set_x(edit.separate.translation[0]);
        s->mutable_translation()->set_y(edit.separate.translation[1]);
        s->mutable_translation()->set_z(edit.separate.translation[2]);
        s->mutable_rotation()->set_x(edit.separate.rotation[0]);
        s->mutable_rotation()->set_y(edit.separate.rotation[1]);
        s->mutable_rotation()->set_z(edit.separate.rotation[2]);
        s->mutable_scale()->set_x(edit.separate.scale[0]);
        s->mutable_scale()->set_y(edit.separate.scale[1]);
        s->mutable_scale()->set_z(edit.separate.scale[2]);
    }

    std::string bytes;
    msg.SerializeToString(&bytes);
    return bytes;
}

void verify_protobuf_version()
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;
}

} // namespace wire
} // namespace net
} // namespace idtxflow
