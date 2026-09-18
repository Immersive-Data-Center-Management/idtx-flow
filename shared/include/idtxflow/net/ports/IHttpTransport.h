#pragma once

/**
 * @file IHttpTransport.h
 * @brief Port for issuing HTTP requests to the backend. The core builds requests
 *        and interprets responses; the concrete transport (and its dependency on
 *        a specific HTTP library) stays behind this interface.
 *
 * Implementer contract:
 *   - Compose the target URL as: Request.url verbatim when non-empty, else
 *     base_url + Request.endpoint. If neither yields a URL (empty base_url and
 *     no absolute url), fail with a transport error (Response.status = 0) rather
 *     than guessing a default host.
 *   - Support the methods -- GET, POST, DELETE. Reject any other method with a transport error
 *   - On transport failure (no HTTP round-trip: unreachable, timeout, bad
 *     method, unconfigured base url) set Response.status = 0 and put a message
 *     in Response.error. On success set the real HTTP status / body / headers.
 *   - request_async delivers its result on a worker thread; request_sync blocks
 *     the caller. Neither marshals to any main thread.
 *
 * Usage:
 * @code
 *   http->set_base_url("https://host");   // or leave empty and set Request.url
 *   IHttpTransport::Request req;
 *   req.method = "GET";
 *   req.endpoint = "/api/v1/health";      // composed as base_url + endpoint
 *   http->request_async(req, [](const IHttpTransport::Response& resp) {
 *       if (!resp.ok()) return;   // status==0 => transport error in resp.error
 *       // use resp.status / resp.body / resp.headers
 *   });
 *   // or, blocking: const auto resp = http->request_sync(req);
 * @endcode
 */

#include <functional>
#include <map>
#include <string>

namespace idtxflow
{
namespace net
{
namespace ports
{
    struct IHttpTransport
    {
        virtual ~IHttpTransport() = default;

        struct Request
        {
            std::string method;     ///< "GET", "POST", or "DELETE"
            std::string endpoint;   ///< path appended to the base url
            std::string body;       ///< request body (used by POST; empty otherwise)
            std::map<std::string, std::string> headers;   ///< extra request headers (e.g. Authorization)

            /// Absolute request URL used verbatim when non-empty; otherwise the
            /// transport composes `base_url + endpoint`. Lets a caller target a
            /// fully-qualified URL  without depending on a configured base url.
            std::string url;
        };

        struct Response
        {
            int         status = 0;   ///< HTTP status (0 = transport failure)
            std::string body;         ///< response body (fetched bytes / text)
            std::string error;        ///< transport error text when status is 0
            std::map<std::string, std::string> headers;   ///< response headers

            /// True for a 2xx status.
            bool ok() const { return status >= 200 && status < 300; }
        };

        /// Async completion callback, invoked with the Response.
        using Cb = std::function<void(const Response&)>;

        /// Base URL prepended to Request.endpoint (may be empty).
        virtual void set_base_url(std::string url) = 0;
        /// The configured base URL, or "" if unset.
        virtual std::string base_url() const = 0;

        /// Issue a request off the main thread; the callback fires on a worker.
        virtual void request_async(const Request& request, Cb callback) = 0;

        /// Issue a request and block until it completes.
        virtual Response request_sync(const Request& request) = 0;

        /// Connect / transfer timeouts in milliseconds.
        virtual void set_timeouts(int connect_ms, int transfer_ms) = 0;
    };

} // namespace ports
} // namespace net
} // namespace idtxflow
