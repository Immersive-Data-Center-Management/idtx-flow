/**
 * @file RestClientTests.cpp
 * @brief Verifies REST orchestration: result parsing delivery, the 401-clears-
 *        token rule on any operation, DELETE treating 404 as gone, and URL
 *        derivation. Uses fakes only — no server, transport library, or engine.
 */

#include "test_framework.h"
#include "test_fakes.h"

#include <idtxflow/net/net/RestClient.h>

using idtxflow::net::RestClient;
using idtxflow::test::FakeHttp;
using idtxflow::test::FakeTokenProvider;
using idtxflow::test::ImmediateDispatcher;

namespace
{
    struct Fixture
    {
        FakeHttp http;
        FakeTokenProvider token;
        ImmediateDispatcher disp;
        RestClient client{&http, &token, &disp};
    };
}

TEST(RestClient, LoginParsesToken)
{
    Fixture f;
    f.http.queue(200, R"({"access_token":"tok","token_type":"Bearer","expires_in":60})");

    std::string got_token;
    bool errored = false;
    f.client.login("u", "p",
        [&](const idtxflow::net::model::LoginResult& lr) { got_token = lr.access_token; },
        [&](const idtxflow::net::model::RestError&) { errored = true; });

    CHECK(!errored);
    CHECK_EQ(got_token, std::string("tok"));
    // Login carries no auth header and posts to the login endpoint.
    CHECK_EQ(f.http.sent.at(0).method, std::string("POST"));
    CHECK_EQ(f.http.sent.at(0).endpoint, std::string("/api/v1/auth/login"));
}

TEST(RestClient, Any401ClearsToken)
{
    // A 401 on each protected operation must clear the stored token, not just login.
    {
        Fixture f;
        f.token.set("live");
        f.http.queue(401, R"({"error":"unauthorized","message":"expired"})");
        f.client.list_files("", "", nullptr, [](const idtxflow::net::model::RestError&) {});
        CHECK(f.token.get().empty());
    }
    {
        Fixture f;
        f.token.set("live");
        f.http.queue(401, R"({"error":"unauthorized","message":"expired"})");
        f.client.create_session("a.usda", "single_edit", nullptr,
                                [](const idtxflow::net::model::RestError&) {});
        CHECK(f.token.get().empty());
    }
    {
        Fixture f;
        f.token.set("live");
        f.http.queue(401, R"({"error":"unauthorized","message":"expired"})");
        f.client.delete_session("s1", nullptr, [](const idtxflow::net::model::RestError&) {});
        CHECK(f.token.get().empty());
    }
}

TEST(RestClient, ListParses)
{
    Fixture f;
    f.token.set("live");
    f.http.queue(200, R"({"files":[{"filepath":"scenes/a.usda","filename":"a.usda","size":10}]})");

    std::vector<idtxflow::net::model::FileEntry> got;
    f.client.list_files("", "",
        [&](const std::vector<idtxflow::net::model::FileEntry>& files) { got = files; },
        nullptr);

    CHECK_EQ(got.size(), static_cast<size_t>(1));
    CHECK_EQ(got.at(0).filepath, std::string("scenes/a.usda"));
    // Protected op carries the bearer header.
    CHECK_EQ(f.http.sent.at(0).headers.at("Authorization"), std::string("Bearer live"));
}

TEST(RestClient, CreateSessionParses)
{
    Fixture f;
    f.token.set("live");
    f.http.queue(201, R"({"session_id":"s1","usd_file":"a.usda","mode":"single_edit","ws_url":"/ws?sid=s1"})");

    std::string got_id;
    f.client.create_session("a.usda", "single_edit",
        [&](const idtxflow::net::model::SessionInfo& si) { got_id = si.session_id; },
        nullptr);

    CHECK_EQ(got_id, std::string("s1"));
}

TEST(RestClient, DeleteGoneOn404)
{
    // Both 204 and 404 count as "gone" (deleted callback fires, no error).
    for (int status : {204, 404})
    {
        Fixture f;
        f.token.set("live");
        f.http.queue(status, "");

        bool deleted = false;
        bool errored = false;
        f.client.delete_session("s1",
            [&] { deleted = true; },
            [&](const idtxflow::net::model::RestError&) { errored = true; });

        CHECK(deleted);
        CHECK(!errored);
    }
}

TEST(RestClient, UrlBuild)
{
    {
        Fixture f;
        f.client.set_base_url("http://host:8080");
        CHECK_EQ(f.client.ws_base_url(), std::string("ws://host:8080"));
        CHECK_EQ(f.client.download_url("scenes/a.usda"),
                 std::string("http://host:8080/api/v1/download/scenes/a.usda"));
    }
    {
        Fixture f;
        f.client.set_base_url("https://host");
        CHECK_EQ(f.client.ws_base_url(), std::string("wss://host"));
    }
}

TEST(RestClient, HealthOkOn200)
{
    Fixture f;
    f.http.queue(200, R"({"status":"ok"})");

    bool healthy = false;
    bool errored = false;
    f.client.health(
        [&](const idtxflow::net::model::HealthResult& hr) { healthy = hr.ok; },
        [&](const idtxflow::net::model::RestError&) { errored = true; });

    CHECK(healthy);
    CHECK(!errored);
    CHECK_EQ(f.http.sent.size(), static_cast<size_t>(1));
    CHECK_EQ(f.http.sent[0].method, std::string("GET"));
    CHECK_EQ(f.http.sent[0].endpoint, std::string("/api/v1/health"));
    // Unauthenticated probe: no Authorization header attached.
    CHECK(f.http.sent[0].headers.find("Authorization") == f.http.sent[0].headers.end());
}

TEST(RestClient, HealthErrorOnUnreachable)
{
    Fixture f;
    f.http.queue(0, "");  // transport failure (status 0)

    bool healthy = false;
    bool errored = false;
    f.client.health(
        [&](const idtxflow::net::model::HealthResult&) { healthy = true; },
        [&](const idtxflow::net::model::RestError&) { errored = true; });

    CHECK(!healthy);
    CHECK(errored);
}

TEST(RestClient, AuthenticatedCallsShortCircuitWithoutToken)
{
    // No token set on the provider: authenticated calls must fail fast with a
    // synthetic not_authenticated error and issue NO request to the transport.
    {
        Fixture f;
        bool errored = false;
        idtxflow::net::model::RestError got;
        f.client.list_files("", "",
            [&](const std::vector<idtxflow::net::model::FileEntry>&) {},
            [&](const idtxflow::net::model::RestError& e) { errored = true; got = e; });
        CHECK(errored);
        CHECK_EQ(got.http_code, 401);
        CHECK_EQ(got.error_code, std::string("not_authenticated"));
        CHECK_EQ(f.http.sent.size(), static_cast<size_t>(0));
    }
    {
        Fixture f;
        bool errored = false;
        f.client.create_session("scenes/a.usda", "single_edit",
            [&](const idtxflow::net::model::SessionInfo&) {},
            [&](const idtxflow::net::model::RestError&) { errored = true; });
        CHECK(errored);
        CHECK_EQ(f.http.sent.size(), static_cast<size_t>(0));
    }
    {
        Fixture f;
        bool errored = false;
        f.client.delete_session("sid-1",
            [&]() {},
            [&](const idtxflow::net::model::RestError&) { errored = true; });
        CHECK(errored);
        CHECK_EQ(f.http.sent.size(), static_cast<size_t>(0));
    }
}

TEST(RestClient, AuthenticatedCallSendsWhenTokenPresent)
{
    Fixture f;
    f.token.set("tok", "Bearer");
    f.http.queue(200, R"({"files":[]})");

    bool errored = false;
    f.client.list_files("", "",
        [&](const std::vector<idtxflow::net::model::FileEntry>&) {},
        [&](const idtxflow::net::model::RestError&) { errored = true; });

    CHECK(!errored);
    CHECK_EQ(f.http.sent.size(), static_cast<size_t>(1));
    CHECK_EQ(f.http.sent[0].headers.at("Authorization"), std::string("Bearer tok"));
}
