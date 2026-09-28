/**
 * @file IxHttpTransportLiveTests.cpp
 * @brief Optional live check that IxHttpTransport can reach a real backend.
 *
 * Compiled and run only when the test build is configured with a backend URL
 * (the `IDTX_TESTS_WITH_IX` macro, set by the test target when IDTX_TEST_BASE_URL
 * is present in the environment). Without it this file is empty, so the default
 * test suite links no transport library and needs no server.
 */

#ifdef IDTX_TESTS_WITH_IX

#include "test_framework.h"

#include <idtxflow/net/adapters/transport/ix/IxHttpTransport.h>

#include <cstdlib>

TEST(IxHttpTransport, HealthReturns200)
{
    const char* base = std::getenv("IDTX_TEST_BASE_URL");
    CHECK(base != nullptr);
    if (base == nullptr)
    {
        return;
    }

    idtxflow::net::adapters::IxHttpTransport http;
    http.set_base_url(base);

    idtxflow::net::ports::IHttpTransport::Request req;
    req.method = "GET";
    req.endpoint = "/api/v1/health";

    const auto resp = http.request_sync(req);
    CHECK_EQ(resp.status, 200);
}

#endif // IDTX_TESTS_WITH_IX
