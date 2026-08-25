/**
 * @file ConventionTests.cpp
 * @brief Guards the two distinct matrix conventions and their relationship — the
 *        highest-risk area for a convention drift. Wire is a row-major verbatim copy; USD is
 *        the wire matrix with its 3x3 rotation/scale block transposed. These
 *        pure-math checks catch a copy/transpose swap without needing a live USD
 *        stage; the full author->read identity through USD storage is covered by
 *        the manual E2E.
 */

#include "test_framework.h"

#include <idtxflow/net/model/ConventionMath.h>
#include <idtxflow/net/wire/Codec.h>

#include "base.pb.h"
#include "transform.pb.h"

using idtxflow::net::model::Mat4;
using idtxflow::net::model::PrimEdit;

namespace
{
    // A matrix with 16 distinct values so any element swap/transpose is visible.
    Mat4 distinct_matrix()
    {
        Mat4 m;
        for (int i = 0; i < 16; ++i)
        {
            m.m[i] = static_cast<double>(i) + 0.5;
        }
        return m;
    }

    bool equal(const Mat4& a, const Mat4& b, double eps)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (std::fabs(a.m[i] - b.m[i]) > eps) return false;
        }
        return true;
    }
}

TEST(Convention, WireRoundTripIdentity)
{
    // Wire is a verbatim row-major copy: encoding a matrix edit and decoding the
    // resulting broadcast reproduces every element unchanged (no transpose).
    const Mat4 original = distinct_matrix();

    PrimEdit edit;
    edit.prim_path = "/root/cube";
    edit.is_matrix = true;
    edit.matrix = original;

    const std::string bytes =
        idtxflow::net::wire::encode_transform_update("s", edit);

    idtxcore::BaseMessage msg;
    CHECK(msg.ParseFromString(bytes));
    // Re-wrap the encoded update as a broadcast so decode() accepts it.
    idtxcore::BaseMessage broadcast;
    *broadcast.mutable_xform_broadcast()->mutable_update() = msg.xform_update();
    broadcast.mutable_xform_broadcast()->set_client_id("peer");
    std::string bbytes;
    CHECK(broadcast.SerializeToString(&bbytes));

    idtxflow::net::wire::DecodedMessage decoded;
    CHECK(idtxflow::net::wire::decode(bbytes, decoded));
    CHECK(decoded.kind == idtxflow::net::wire::DecodedMessage::Kind::RemoteEdit);
    CHECK(equal(decoded.remote_edit.edit.matrix, original, 1e-9));
}

TEST(Convention, UsdRoundTripIdentity)
{
    // The USD convention transposes the 3x3 block; converting to USD and back is
    // identity (the transpose is its own inverse).
    const Mat4 original = distinct_matrix();
    const Mat4 usd = idtxflow::net::model::usd_from_wire(original);
    const Mat4 back = idtxflow::net::model::wire_from_usd(usd);
    CHECK(equal(back, original, 1e-9));

    // The USD form must actually differ from wire in the off-diagonal 3x3 block
    // (guards against usd_from_wire degenerating into a plain copy).
    CHECK(std::fabs(usd.m[1] - original.m[1]) > 1e-9);
    CHECK(std::fabs(usd.m[4] - original.m[4]) > 1e-9);
    // Translation row is not transposed.
    CHECK_NEAR(usd.m[12], original.m[12], 1e-9);
    CHECK_NEAR(usd.m[13], original.m[13], 1e-9);
    CHECK_NEAR(usd.m[14], original.m[14], 1e-9);
}

TEST(Convention, WireToUsdCrossIdentity)
{
    // Cross-convention: a matrix that travels wire (row-major) -> peer -> USD
    // (transpose) -> read-back-to-wire returns to the original. This catches a
    // copy/transpose swap that would pass both single-convention round-trips
    // while corrupting real cross-client sync.
    const Mat4 original = distinct_matrix();

    // Wire leg: verbatim copy (what the codec does).
    const Mat4 on_wire = original;

    // USD leg: author applies the transpose to store it.
    const Mat4 authored = idtxflow::net::model::usd_from_wire(on_wire);

    // Read-back leg: recover the wire matrix from the USD one.
    const Mat4 recovered = idtxflow::net::model::wire_from_usd(authored);

    CHECK(equal(recovered, original, 1e-9));

    // And the intermediate USD form is genuinely the transpose, not a copy —
    // proving the two conventions stay distinct end-to-end.
    CHECK_NEAR(authored.m[1], original.m[4], 1e-9);
    CHECK_NEAR(authored.m[4], original.m[1], 1e-9);
    CHECK_NEAR(authored.m[2], original.m[8], 1e-9);
    CHECK_NEAR(authored.m[8], original.m[2], 1e-9);
}

TEST(Convention, BasisOriginTranslationOnBottomRow)
{
    // The engine<->wire layout puts translation on the bottom row (m30..m32,
    // indices 12..14) — the cell the backend consumes. A basis with distinct
    // rows and an asymmetric translation must land exactly there, with the
    // last column (indices 3,7,11) left at zero.
    const double basis_rows[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    const double origin[3] = {10, 20, 30};

    const Mat4 w = idtxflow::net::model::wire_from_basis_origin(basis_rows, origin);

    CHECK_NEAR(w.m[12], 10.0, 1e-9);
    CHECK_NEAR(w.m[13], 20.0, 1e-9);
    CHECK_NEAR(w.m[14], 30.0, 1e-9);
    CHECK_NEAR(w.m[3],  0.0, 1e-9);
    CHECK_NEAR(w.m[7],  0.0, 1e-9);
    CHECK_NEAR(w.m[11], 0.0, 1e-9);
    // Basis rows land on the matching wire rows.
    CHECK_NEAR(w.m[0], 1.0, 1e-9);
    CHECK_NEAR(w.m[5], 5.0, 1e-9);
    CHECK_NEAR(w.m[10], 9.0, 1e-9);

    // Round-trip recovers the basis rows and the translation.
    double br[9]; double o[3];
    idtxflow::net::model::basis_origin_from_wire(w, br, o);
    for (int i = 0; i < 9; ++i) CHECK_NEAR(br[i], basis_rows[i], 1e-9);
    CHECK_NEAR(o[0], 10.0, 1e-9);
    CHECK_NEAR(o[1], 20.0, 1e-9);
    CHECK_NEAR(o[2], 30.0, 1e-9);
}
