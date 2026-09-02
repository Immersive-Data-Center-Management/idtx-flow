/**
 * @file SessionSocketTests.cpp
 * @brief Verifies session protocol behavior with a fake transport: per-prim
 *        outbound coalescing, inbound dispatch (handshake / remote edit / ack /
 *        error as distinct callbacks), and disconnect classification for both
 *        the close-frame path and the transport-failure path.
 */

#include "test_framework.h"
#include "test_fakes.h"

#include <idtxflow/net/net/SessionSocket.h>

#include "base.pb.h"
#include "transform.pb.h"
#include "session.pb.h"

using idtxflow::net::SessionSocket;
using idtxflow::net::model::CloseReason;
using idtxflow::net::model::PrimEdit;
using idtxflow::net::ports::IWebSocketTransport;
using idtxflow::test::FakeClock;
using idtxflow::test::FakeWebSocket;

namespace
{
    PrimEdit matrix_edit(const std::string& prim, double first)
    {
        PrimEdit e;
        e.prim_path = prim;
        e.is_matrix = true;
        e.matrix.m[0] = first;
        return e;
    }

    std::string encode_broadcast(const std::string& client_id, const std::string& prim)
    {
        idtxcore::BaseMessage msg;
        auto* b = msg.mutable_xform_broadcast();
        b->set_client_id(client_id);
        auto* u = b->mutable_update();
        u->set_prim_path(prim);
        u->mutable_matrix()->set_m00(1.0);
        std::string bytes;
        msg.SerializeToString(&bytes);
        return bytes;
    }

    std::string encode_handshake(const std::string& sid)
    {
        idtxcore::BaseMessage msg;
        auto* h = msg.mutable_handshake();
        h->set_session_id(sid);
        h->set_usd_path("/stage");
        h->set_usd_uri("http://host/x.usda");
        std::string bytes;
        msg.SerializeToString(&bytes);
        return bytes;
    }

    std::string encode_ack(bool ok, const std::string& error)
    {
        idtxcore::BaseMessage msg;
        auto* a = msg.mutable_ack();
        a->set_ok(ok);
        a->set_error(error);
        std::string bytes;
        msg.SerializeToString(&bytes);
        return bytes;
    }

    std::string encode_error(const std::string& code, const std::string& message)
    {
        idtxcore::BaseMessage msg;
        auto* e = msg.mutable_error();
        e->set_code(code);
        e->set_message(message);
        std::string bytes;
        msg.SerializeToString(&bytes);
        return bytes;
    }
}

TEST(SessionSocket, CoalescesToOneFramePerPoll)
{
    FakeWebSocket ws;
    FakeClock clock;
    SessionSocket socket(&ws, &clock);
    socket.set_session_id("s1");
    socket.connect("ws://host", "tok");

    // Several edits to one prim collapse to a single frame.
    socket.send_edit(matrix_edit("/a", 1.0));
    socket.send_edit(matrix_edit("/a", 2.0));
    socket.send_edit(matrix_edit("/a", 3.0));
    socket.poll();
    CHECK_EQ(ws.sent.size(), static_cast<size_t>(1));

    // The surviving frame is the latest edit for the prim.
    idtxcore::BaseMessage sent;
    CHECK(sent.ParseFromString(ws.sent.at(0)));
    CHECK(sent.message_case() == idtxcore::BaseMessage::kXformUpdate);
    CHECK_NEAR(sent.xform_update().matrix().m00(), 3.0, 1e-9);

    // Distinct prims produce distinct frames.
    ws.sent.clear();
    socket.send_edit(matrix_edit("/a", 1.0));
    socket.send_edit(matrix_edit("/b", 1.0));
    socket.poll();
    CHECK_EQ(ws.sent.size(), static_cast<size_t>(2));

    // Nothing pending -> no frames.
    ws.sent.clear();
    socket.poll();
    CHECK_EQ(ws.sent.size(), static_cast<size_t>(0));
}

TEST(SessionSocket, InboundDecodes)
{
    FakeWebSocket ws;
    FakeClock clock;
    SessionSocket socket(&ws, &clock);
    socket.connect("ws://host", "tok");

    std::string hs_session, edit_prim, edit_client, err_code;
    bool ack_ok = false;
    bool ack_fired = false;
    bool err_fired = false;

    socket.on_handshake([&](const std::string& sid, const std::string&, const std::string&)
                        { hs_session = sid; });
    socket.on_remote_edit([&](const PrimEdit& e, const std::string& from)
                          { edit_prim = e.prim_path; edit_client = from; });
    socket.on_ack([&](bool ok, const std::string&) { ack_ok = ok; ack_fired = true; });
    socket.on_error([&](const std::string& code, const std::string&) { err_code = code; err_fired = true; });

    ws.emit_binary(encode_handshake("s1"));
    CHECK_EQ(hs_session, std::string("s1"));

    ws.emit_binary(encode_broadcast("peer-9", "/root/cube"));
    CHECK_EQ(edit_prim, std::string("/root/cube"));
    CHECK_EQ(edit_client, std::string("peer-9"));

    // Ack and error are distinct callbacks (no sentinel folding one into the other).
    ws.emit_binary(encode_ack(true, ""));
    CHECK(ack_fired);
    CHECK(ack_ok);

    ws.emit_binary(encode_error("busy", "try later"));
    CHECK(err_fired);
    CHECK_EQ(err_code, std::string("busy"));
}

TEST(SessionSocket, CloseReasonParseWsClose)
{
    FakeWebSocket ws;
    FakeClock clock;
    SessionSocket socket(&ws, &clock);
    socket.connect("ws://host", "tok");

    CloseReason got = CloseReason::unknown;
    socket.on_disconnected([&](CloseReason r, int, const std::string&) { got = r; });

    // The single-edit lockout arrives as a dedicated close code.
    ws.emit_state(IWebSocketTransport::State::Disconnected, 4409, "");
    CHECK(got == CloseReason::single_edit_busy);

    // Reason-text paths.
    ws.emit_state(IWebSocketTransport::State::Disconnected, 4000, "session no longer exists");
    CHECK(got == CloseReason::session_gone);

    ws.emit_state(IWebSocketTransport::State::Disconnected, 4000, "missing session id");
    CHECK(got == CloseReason::missing_session_id);

    // A normal close.
    ws.emit_state(IWebSocketTransport::State::Disconnected, 1000, "");
    CHECK(got == CloseReason::normal);
}

TEST(SessionSocket, CloseReasonParseUpgradeRejected)
{
    FakeWebSocket ws;
    FakeClock clock;
    SessionSocket socket(&ws, &clock);
    socket.connect("ws://host", "tok");

    CloseReason got = CloseReason::unknown;
    int got_code = -1;
    socket.on_disconnected([&](CloseReason r, int code, const std::string&) { got = r; got_code = code; });

    // A transport failure / rejected upgrade never yields a close frame.
    ws.emit_state(IWebSocketTransport::State::Error, 0, "connection refused");
    CHECK(got == CloseReason::transport_error);
    CHECK_EQ(got_code, 0);
}

