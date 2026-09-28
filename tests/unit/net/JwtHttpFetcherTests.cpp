/**
 * @file JwtHttpFetcherTests.cpp
 * @brief Verifies the JWT asset fetcher drives the IHttpTransport port with an
 *        absolute URL and the current bearer header, writes the body to disk on
 *        success, and fails cleanly on a non-2xx response. Uses fakes only — no
 *        server, transport library, or engine.
 */

#include "test_framework.h"
#include "test_fakes.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include <idtxflow/net/adapters/auth/JwtHttpFetcher.h>
#include <idtxflow/net/adapters/auth/StaticTokenProvider.h>

using idtxflow::net::adapters::JwtHttpFetcher;
using idtxflow::net::adapters::StaticTokenProvider;
using idtxflow::test::FakeHttp;

namespace
{
    std::filesystem::path temp_dest(const std::string& leaf)
    {
        auto dir = std::filesystem::temp_directory_path() / "idtx_jwt_fetcher_tests";
        return dir / leaf;
    }

    std::string read_file(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
}

TEST(JwtHttpFetcher, SuccessUsesAbsoluteUrlAndBearerAndWritesBody)
{
    auto http = std::make_shared<FakeHttp>();
    http->queue(200, "usd-bytes");

    StaticTokenProvider::instance().set("tok", "Bearer");

    const std::string url = "https://host/api/v1/download/scenes/foo.usda";
    const auto dest = temp_dest("foo.usda");
    std::error_code ec;
    std::filesystem::remove(dest, ec);

    JwtHttpFetcher fetcher{http};
    const bool ok = fetcher(url, dest);

    CHECK(ok);
    CHECK_EQ(http->sent.size(), static_cast<size_t>(1));
    CHECK_EQ(http->sent[0].method, std::string("GET"));
    CHECK_EQ(http->sent[0].url, url);                       // absolute URL, not base+endpoint
    CHECK(http->sent[0].endpoint.empty());
    CHECK_EQ(http->sent[0].headers.at("Authorization"), std::string("Bearer tok"));
    CHECK(std::filesystem::exists(dest));
    CHECK_EQ(read_file(dest), std::string("usd-bytes"));

    std::filesystem::remove(dest, ec);
    StaticTokenProvider::instance().clear();
}

TEST(JwtHttpFetcher, NonSuccessReturnsFalseAndWritesNoFile)
{
    auto http = std::make_shared<FakeHttp>();
    http->queue(401, "unauthorized");

    StaticTokenProvider::instance().clear(); // no token -> no Authorization header

    const std::string url = "https://host/api/v1/download/secret.usda";
    const auto dest = temp_dest("secret.usda");
    std::error_code ec;
    std::filesystem::remove(dest, ec);

    JwtHttpFetcher fetcher{http};
    const bool ok = fetcher(url, dest);

    CHECK(!ok);
    CHECK_EQ(http->sent.size(), static_cast<size_t>(1));
    CHECK_EQ(http->sent[0].url, url);
    CHECK(http->sent[0].headers.find("Authorization") == http->sent[0].headers.end());
    CHECK(!std::filesystem::exists(dest));
}

TEST(JwtHttpFetcher, NoTransportFailsCleanly)
{
    JwtHttpFetcher fetcher{}; // default-constructed: no transport injected
    const bool ok = fetcher("https://host/x.usda", temp_dest("x.usda"));
    CHECK(!ok);
}
