/**
 * @file CodecTests.cpp
 * @brief Wire codec round-trips. Guards a high-risk area: the wire matrix
 *        convention (row-major, no transpose) and the outbound TransformUpdate
 *        shape, including the intentionally misspelled `seperate` field. Runs
 *        with no server or engine.
 */

#include "test_framework.h"

#include <idtxflow/net/wire/Codec.h>

#include "base.pb.h"
#include "transform.pb.h"

using idtxflow::net::model::PrimEdit;
using idtxflow::net::wire::DecodedMessage;

namespace
{
    // A matrix with 16 distinct values so any element swap or transpose is caught.
    PrimEdit make_matrix_edit()
    {
        PrimEdit edit;
        edit.kind = PrimEdit::Kind::Transform;
        edit.prim_path = "/root/cube";
        edit.is_matrix = true;
        edit.timestamp = 123456789;
        for (int i = 0; i < 16; ++i)
        {
            edit.matrix.m[i] = static_cast<double>(i) + 0.5;
        }
        return edit;
    }
}

TEST(Codec, Mat4RoundTripIdentity)
{
    const PrimEdit original = make_matrix_edit();

    const std::string bytes =
        idtxflow::net::wire::encode_transform_update("sess-1", original);

    // Decode is only defined for inbound message kinds (handshake/broadcast/ack/
    // error), so inspect the encoded outbound update via the generated message and
    // confirm each row-major element survives unchanged.
    idtxcore::BaseMessage msg;
    CHECK(msg.ParseFromString(bytes));
    CHECK(msg.message_case() == idtxcore::BaseMessage::kXformUpdate);

    const auto& u = msg.xform_update();
    CHECK_EQ(u.prim_path(), std::string("/root/cube"));
    CHECK_EQ(u.timestamp(), static_cast<int64_t>(123456789));
    CHECK(u.has_matrix());

    const auto& m = u.matrix();
    const auto& a = original.matrix.m;
    CHECK_NEAR(m.m00(), a[0],  1e-9); CHECK_NEAR(m.m01(), a[1],  1e-9);
    CHECK_NEAR(m.m02(), a[2],  1e-9); CHECK_NEAR(m.m03(), a[3],  1e-9);
    CHECK_NEAR(m.m10(), a[4],  1e-9); CHECK_NEAR(m.m11(), a[5],  1e-9);
    CHECK_NEAR(m.m12(), a[6],  1e-9); CHECK_NEAR(m.m13(), a[7],  1e-9);
    CHECK_NEAR(m.m20(), a[8],  1e-9); CHECK_NEAR(m.m21(), a[9],  1e-9);
    CHECK_NEAR(m.m22(), a[10], 1e-9); CHECK_NEAR(m.m23(), a[11], 1e-9);
    CHECK_NEAR(m.m30(), a[12], 1e-9); CHECK_NEAR(m.m31(), a[13], 1e-9);
    CHECK_NEAR(m.m32(), a[14], 1e-9); CHECK_NEAR(m.m33(), a[15], 1e-9);

    // A broadcast carrying the same matrix must decode back to identical values,
    // proving the inbound row-major fill mirrors the outbound one.
    idtxcore::BaseMessage broadcast;
    auto* b = broadcast.mutable_xform_broadcast();
    b->set_client_id("peer-9");
    *b->mutable_update() = u;

    std::string broadcast_bytes;
    CHECK(broadcast.SerializeToString(&broadcast_bytes));

    DecodedMessage decoded;
    CHECK(idtxflow::net::wire::decode(broadcast_bytes, decoded));
    CHECK(decoded.kind == DecodedMessage::Kind::RemoteEdit);
    CHECK_EQ(decoded.remote_edit.from_client_id, std::string("peer-9"));
    CHECK(decoded.remote_edit.edit.is_matrix);
    for (int i = 0; i < 16; ++i)
    {
        CHECK_NEAR(decoded.remote_edit.edit.matrix.m[i], a[i], 1e-9);
    }
}

TEST(Codec, SeperateFieldPresent)
{
    PrimEdit edit;
    edit.kind = PrimEdit::Kind::Transform;
    edit.prim_path = "/root/cube";
    edit.is_matrix = false;
    edit.timestamp = 42;
    edit.separate.translation[0] = 1.0;
    edit.separate.translation[1] = 2.0;
    edit.separate.translation[2] = 3.0;
    edit.separate.rotation[0] = 10.0;
    edit.separate.rotation[1] = 20.0;
    edit.separate.rotation[2] = 30.0;
    edit.separate.scale[0] = 4.0;
    edit.separate.scale[1] = 5.0;
    edit.separate.scale[2] = 6.0;

    const std::string bytes =
        idtxflow::net::wire::encode_transform_update("sess-1", edit);

    idtxcore::BaseMessage msg;
    CHECK(msg.ParseFromString(bytes));
    const auto& u = msg.xform_update();

    // The separate branch must populate the (intentionally misspelled) `seperate`
    // field, not the matrix field.
    CHECK(u.has_seperate());
    CHECK(!u.has_matrix());

    const auto& s = u.seperate();
    CHECK_NEAR(s.translation().x(), 1.0, 1e-9);
    CHECK_NEAR(s.translation().y(), 2.0, 1e-9);
    CHECK_NEAR(s.translation().z(), 3.0, 1e-9);
    CHECK_NEAR(s.rotation().x(), 10.0, 1e-9);
    CHECK_NEAR(s.rotation().y(), 20.0, 1e-9);
    CHECK_NEAR(s.rotation().z(), 30.0, 1e-9);
    CHECK_NEAR(s.scale().x(), 4.0, 1e-9);
    CHECK_NEAR(s.scale().y(), 5.0, 1e-9);
    CHECK_NEAR(s.scale().z(), 6.0, 1e-9);
}
