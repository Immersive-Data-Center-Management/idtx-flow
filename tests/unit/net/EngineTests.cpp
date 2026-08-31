/**
 * @file EngineTests.cpp
 * @brief Broadcast-gating for the collaboration engine: a stage-originated edit
 *        is sent only for an armed, remote session, and never while an inbound
 *        remote edit is being applied (loopback suppression).
 */

#include "test_framework.h"
#include "test_fakes.h"

#include <idtxflow/net/CollabEngine.h>
#include <idtxflow/net/net/RestClient.h>
#include <idtxflow/net/net/SessionSocket.h>

#include "base.pb.h"
#include "transform.pb.h"

using idtxflow::net::CollabEngine;
using idtxflow::net::CollabObserver;
using idtxflow::net::CollabPorts;
using idtxflow::net::model::PrimEdit;
using idtxflow::test::FakeClock;
using idtxflow::test::FakeHttp;
using idtxflow::test::FakeStageBridge;
using idtxflow::test::FakeTicker;
using idtxflow::test::FakeTokenProvider;
using idtxflow::test::FakeWebSocket;
using idtxflow::test::ImmediateDispatcher;

namespace
{
    // Minimal observer that ignores everything; the gating tests assert on the
    // fake transport's sent frames, not on observer calls.
    struct NullObserver : CollabObserver
    {
        void on_login_ok(const idtxflow::net::model::LoginResult&) override {}
        void on_health(const idtxflow::net::model::HealthResult&) override {}
        void on_thumbnail(const idtxflow::net::model::ThumbnailResult&) override {}
        void on_files(const std::vector<idtxflow::net::model::FileEntry>&) override {}
        void on_session_created(const idtxflow::net::model::SessionInfo&) override {}
        void on_request_failed(idtxflow::net::Op, const idtxflow::net::model::RestError&) override {}
        void on_session_ready(const idtxflow::net::model::SessionInfo&, const std::string&, const std::string&) override {}
        void on_session_closed(const std::string&) override {}
        void on_socket_opened() override {}
        void on_handshake(const std::string&, const std::string&, const std::string&) override {}
        void on_remote_edit(const PrimEdit&, const std::string&) override {}
        void on_ack(bool, const std::string&) override {}
        void on_socket_error(const std::string&, const std::string&) override {}
        void on_disconnected(idtxflow::net::model::CloseReason, int, const std::string&) override {}
    };

    struct Rig
    {
        FakeHttp http;
        FakeWebSocket ws;
        ImmediateDispatcher disp;
        FakeTokenProvider token;
        FakeClock clock;
        FakeTicker ticker;
        FakeStageBridge stage;
        NullObserver obs;
        CollabEngine engine;

        Rig()
        {
            CollabPorts ports;
            ports.http = &http;
            ports.ws = &ws;
            ports.dispatcher = &disp;
            ports.token = &token;
            ports.clock = &clock;
            ports.ticker = &ticker;
            engine.initialize(ports, &obs);
            engine.open_session_socket("s1", "ws://host");
        }

        ~Rig() { engine.shutdown(); }
    };

    PrimEdit edit_for(const std::string& prim)
    {
        PrimEdit e;
        e.prim_path = prim;
        e.is_matrix = true;
        return e;
    }

    std::string encode_broadcast(const std::string& prim)
    {
        idtxcore::BaseMessage msg;
        auto* b = msg.mutable_xform_broadcast();
        b->set_client_id("peer");
        b->mutable_update()->set_prim_path(prim);
        b->mutable_update()->mutable_matrix()->set_m00(1.0);
        std::string bytes;
        msg.SerializeToString(&bytes);
        return bytes;
    }
}

TEST(Engine, NotArmedNoSend)
{
    Rig rig;
    rig.engine.attach_stage(&rig.stage, /*remote*/ true);
    // Not armed yet: a stage change must not broadcast.
    rig.stage.emit_change(edit_for("/a"));
    rig.engine.poll();
    CHECK_EQ(rig.ws.sent.size(), static_cast<size_t>(0));
}

TEST(Engine, ArmEnablesOutbound)
{
    Rig rig;
    rig.engine.attach_stage(&rig.stage, /*remote*/ true);
    rig.engine.arm_sync();
    rig.stage.emit_change(edit_for("/a"));
    rig.engine.poll();
    CHECK_EQ(rig.ws.sent.size(), static_cast<size_t>(1));
}

TEST(Engine, ApplyingRemoteNoEcho)
{
    Rig rig;
    rig.engine.attach_stage(&rig.stage, /*remote*/ true);
    rig.engine.arm_sync();

    // Model a real bridge: applying a remote edit trips the stage's change
    // report. The engine must suppress that echo while applying_remote_ is set.
    rig.stage.apply_remote_hook = [&rig](const PrimEdit& e) { rig.stage.emit_change(e); };

    rig.ws.emit_binary(encode_broadcast("/a"));   // inbound remote edit
    rig.engine.poll();
    CHECK_EQ(rig.ws.sent.size(), static_cast<size_t>(0));

    // A subsequent genuine local change still broadcasts.
    rig.stage.emit_change(edit_for("/a"));
    rig.engine.poll();
    CHECK_EQ(rig.ws.sent.size(), static_cast<size_t>(1));
}

namespace
{
    // Observer that records the high-level session-flow callbacks so the
    // begin_session / end_session lifecycle can be asserted.
    struct RecordingObserver : CollabObserver
    {
        int  created = 0;
        int  ready = 0;
        int  closed = 0;
        int  failed = 0;
        std::string ready_session_id;
        std::string ready_stage_url;
        std::string ready_ws_url;
        std::string closed_id;

        void on_login_ok(const idtxflow::net::model::LoginResult&) override {}
        void on_health(const idtxflow::net::model::HealthResult&) override {}
        void on_thumbnail(const idtxflow::net::model::ThumbnailResult&) override {}
        void on_files(const std::vector<idtxflow::net::model::FileEntry>&) override {}
        void on_session_created(const idtxflow::net::model::SessionInfo&) override { ++created; }
        void on_request_failed(idtxflow::net::Op, const idtxflow::net::model::RestError&) override { ++failed; }
        void on_session_ready(const idtxflow::net::model::SessionInfo& s,
                              const std::string& stage_url, const std::string& ws_url) override
        {
            ++ready;
            ready_session_id = s.session_id;
            ready_stage_url = stage_url;
            ready_ws_url = ws_url;
        }
        void on_session_closed(const std::string& id) override { ++closed; closed_id = id; }
        void on_socket_opened() override {}
        void on_handshake(const std::string&, const std::string&, const std::string&) override {}
        void on_remote_edit(const PrimEdit&, const std::string&) override {}
        void on_ack(bool, const std::string&) override {}
        void on_socket_error(const std::string&, const std::string&) override {}
        void on_disconnected(idtxflow::net::model::CloseReason, int, const std::string&) override {}
    };

    // Flow rig: initialize with a recording observer but do NOT pre-open a
    // socket, so begin_session drives the full create -> socket -> ready path.
    struct FlowRig
    {
        FakeHttp http;
        FakeWebSocket ws;
        ImmediateDispatcher disp;
        FakeTokenProvider token;
        FakeClock clock;
        FakeTicker ticker;
        RecordingObserver obs;
        CollabEngine engine;

        FlowRig()
        {
            http.set_base_url("http://host:8080");
            token.set("live");
            CollabPorts ports;
            ports.http = &http;
            ports.ws = &ws;
            ports.dispatcher = &disp;
            ports.token = &token;
            ports.clock = &clock;
            ports.ticker = &ticker;
            engine.initialize(ports, &obs);
        }

        ~FlowRig() { engine.shutdown(); }
    };
}

TEST(SessionFlow, BeginOpensSocketAndReportsReady)
{
    FlowRig rig;
    rig.http.queue(201,
        R"({"session_id":"s1","usd_file":"scenes/a.usda","mode":"single_edit","ws_url":"/ws?sid=s1"})");

    rig.engine.begin_session("scenes/a.usda", "single_edit");

    // Ordinary created callback still fires, then the high-level ready step.
    CHECK_EQ(rig.obs.created, 1);
    CHECK_EQ(rig.obs.ready, 1);
    CHECK_EQ(rig.obs.failed, 0);
    // The socket was opened as part of the flow.
    CHECK(rig.engine.is_socket_open());
    // Ready carries the composed, ready-to-use URLs (host computes nothing).
    CHECK_EQ(rig.obs.ready_session_id, std::string("s1"));
    CHECK_EQ(rig.obs.ready_stage_url,
             std::string("http://host:8080/api/v1/download/scenes/a.usda"));
    CHECK_EQ(rig.obs.ready_ws_url, std::string("ws://host:8080/ws?sid=s1"));
}

TEST(SessionFlow, BeginCreateFailureReportsErrorNoSocket)
{
    FlowRig rig;
    rig.http.queue(500, R"({"error":"boom","message":"nope"})");

    rig.engine.begin_session("scenes/a.usda", "single_edit");

    CHECK_EQ(rig.obs.failed, 1);
    CHECK_EQ(rig.obs.ready, 0);
    CHECK_EQ(rig.obs.created, 0);
    CHECK(!rig.engine.is_socket_open());
}

TEST(SessionFlow, EndSessionTearsDownAndDeletes)
{
    FlowRig rig;
    rig.http.queue(201,
        R"({"session_id":"s1","usd_file":"scenes/a.usda","mode":"single_edit","ws_url":"/ws?sid=s1"})");
    rig.engine.begin_session("scenes/a.usda", "single_edit");
    CHECK(rig.engine.is_socket_open());

    rig.http.queue(204, "");   // canned DELETE response
    rig.engine.end_session();

    CHECK(!rig.engine.is_socket_open());
    CHECK_EQ(rig.obs.closed, 1);
    CHECK_EQ(rig.obs.closed_id, std::string("s1"));
    // The second HTTP request the engine sent is the session DELETE.
    CHECK_EQ(rig.http.sent.at(1).method, std::string("DELETE"));
}

TEST(SessionFlow, EndSessionNoActiveIsSafe)
{
    FlowRig rig;
    // No begin_session: end must be a safe no-op that still reports closed(empty).
    rig.engine.end_session();
    CHECK_EQ(rig.obs.closed, 1);
    CHECK_EQ(rig.obs.closed_id, std::string(""));
    CHECK(rig.http.sent.empty());
}

