#pragma once

/**
 * @file test_fakes.h
 * @brief Hand-written port fakes for exercising the core with no server, engine,
 *        or transport library. Each fake is minimal and scriptable.
 */

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <idtxflow/net/ports/IHttpTransport.h>
#include <idtxflow/net/ports/IMainThreadDispatcher.h>
#include <idtxflow/net/ports/ITokenProvider.h>
#include <idtxflow/net/ports/IWebSocketTransport.h>
#include <idtxflow/net/ports/IClock.h>
#include <idtxflow/net/ports/IStageBridge.h>
#include <idtxflow/net/ports/IFrameTicker.h>

namespace idtxflow
{
namespace test
{
    /// Runs posted callbacks inline, so async flows complete synchronously within
    /// a test.
    struct ImmediateDispatcher : net::ports::IMainThreadDispatcher
    {
        void post(std::function<void()> fn) override { if (fn) fn(); }
    };

    /// In-memory token provider mirroring the process-wide static one.
    struct FakeTokenProvider : net::ports::ITokenProvider
    {
        std::string token;
        std::string type = "Bearer";

        std::string get() const override { return token; }
        void set(std::string t, std::string ty = "Bearer") override
        {
            token = std::move(t);
            type = ty.empty() ? std::string("Bearer") : std::move(ty);
        }
        void clear() override { token.clear(); }
        std::string auth_header_value() const override
        {
            return token.empty() ? std::string() : type + " " + token;
        }
    };

    /// Scriptable HTTP transport: each request pops the next canned response and
    /// records what was sent (method, endpoint, headers) for assertions. Runs the
    /// callback inline, so no real threading is involved.
    struct FakeHttp : net::ports::IHttpTransport
    {
        struct Sent
        {
            std::string method;
            std::string endpoint;
            std::string body;
            std::string url;
            std::map<std::string, std::string> headers;
        };

        std::string        base = "http://localhost:8080";
        std::vector<Response> responses;   // consumed front-to-back
        std::vector<Sent>     sent;
        size_t                index = 0;

        void queue(int status, std::string body)
        {
            Response r;
            r.status = status;
            r.body = std::move(body);
            responses.push_back(std::move(r));
        }

        void set_base_url(std::string url) override { base = std::move(url); }
        std::string base_url() const override { return base; }
        void set_timeouts(int, int) override {}

        Response next()
        {
            if (index < responses.size())
            {
                return responses[index++];
            }
            Response empty;
            empty.status = 0;
            empty.error = "no canned response";
            return empty;
        }

        void request_async(const Request& request, Cb callback) override
        {
            sent.push_back(Sent{request.method, request.endpoint, request.body, request.url, request.headers});
            if (callback) callback(next());
        }

        Response request_sync(const Request& request) override
        {
            sent.push_back(Sent{request.method, request.endpoint, request.body, request.url, request.headers});
            return next();
        }
    };

    /// Fixed-value clock for deterministic timestamps.
    struct FakeClock : net::ports::IClock
    {
        int64_t millis = 1000;
        int64_t now_millis() const override { return millis; }
    };

    /// Scriptable WebSocket transport: records sent frames and lets a test drive
    /// inbound frames and state transitions. `connect` reports Connected so the
    /// socket is immediately usable.
    struct FakeWebSocket : net::ports::IWebSocketTransport
    {
        std::vector<std::string>           sent;
        std::map<std::string, std::string> headers;
        bool                               open = false;
        OnBinary                           on_binary;
        OnState                            on_state;

        void set_headers(std::map<std::string, std::string> h) override { headers = std::move(h); }
        void connect(std::string) override
        {
            open = true;
            if (on_state) on_state(State::Connected, 0, std::string());
        }
        void close() override { open = false; }
        void send_binary(const std::string& bytes) override { sent.push_back(bytes); }
        bool is_open() const override { return open; }
        void set_on_binary(OnBinary cb) override { on_binary = std::move(cb); }
        void set_on_state(OnState cb) override { on_state = std::move(cb); }
        void poll() override {}

        // --- test drivers ---
        void emit_binary(const std::string& bytes) { if (on_binary) on_binary(bytes); }
        void emit_state(State state, int code, const std::string& reason)
        {
            if (on_state) on_state(state, code, reason);
        }
    };

    /// Stage bridge fake: captures the engine's change-report sink and records
    /// author/apply calls. `emit_change` simulates a stage-originated change.
    struct FakeStageBridge : net::ports::IStageBridge
    {
        OnChanged on_changed;
        int build_index_calls = 0;
        std::vector<net::model::PrimEdit> authored;
        std::vector<net::model::PrimEdit> applied;
        // Optional: models a real bridge whose apply_remote_edit trips a stage
        // change (which must be suppressed from re-broadcast by the engine).
        std::function<void(const net::model::PrimEdit&)> apply_remote_hook;

        void build_index() override { ++build_index_calls; }
        bool read_prim(const std::string&, net::model::PrimEdit&) const override { return false; }
        void author_local_edit(const net::model::PrimEdit& edit) override { authored.push_back(edit); }
        void apply_remote_edit(const net::model::PrimEdit& edit) override
        {
            applied.push_back(edit);
            if (apply_remote_hook) apply_remote_hook(edit);
        }
        void set_on_changed(OnChanged sink) override { on_changed = std::move(sink); }

        void emit_change(const net::model::PrimEdit& edit) { if (on_changed) on_changed(edit); }
    };

    /// Frame ticker fake: holds the tick so a test can drive it manually.
    struct FakeTicker : net::ports::IFrameTicker
    {
        Tick tick;
        void set_tick(Tick fn) override { tick = std::move(fn); }
        void clear_tick() override { tick = nullptr; }
        void fire() { if (tick) tick(); }
    };

} // namespace test
} // namespace idtxflow
