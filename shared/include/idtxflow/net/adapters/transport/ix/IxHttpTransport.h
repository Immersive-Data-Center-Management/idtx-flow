#pragma once

/**
 * @file IxHttpTransport.h
 * @brief IHttpTransport backed by IXWebSocket's HTTP client.
 *
 * Sends requests exactly as given (method, endpoint, body, headers) against a
 * configured base URL and returns the raw status/body — it neither reads the
 * auth token nor parses JSON; callers supply headers and interpret bodies. Async
 * requests run on a bounded worker pool so a burst of calls uses a fixed number
 * of threads rather than spawning one per request.
 *
 * The IXWebSocket dependency stays in the .cpp; this header is standard-library
 * only.
 */

#include <string>

#include <idtxflow/net/ports/IHttpTransport.h>
#include <idtxflow/utils/ThreadPool.h>

namespace idtxflow
{
namespace net
{
namespace adapters
{
    class IxHttpTransport : public ports::IHttpTransport
    {
    public:
        IxHttpTransport() = default;

        void set_base_url(std::string url) override;
        std::string base_url() const override;

        void request_async(const Request& request, Cb callback) override;
        Response request_sync(const Request& request) override;

        void set_timeouts(int connect_ms, int transfer_ms) override;

    private:
        std::string base_url_ = "http://localhost:8080";
        int         connect_ms_ = 30000;
        int         transfer_ms_ = 120000;
        utils::ThreadPool pool_{2};
    };

} // namespace adapters
} // namespace net
} // namespace idtxflow
