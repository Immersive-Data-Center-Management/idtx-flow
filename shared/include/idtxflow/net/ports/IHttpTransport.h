#pragma once

/**
 * @file IHttpTransport.h
 * @brief Port for issuing HTTP requests to the backend. The core builds requests
 *        and interprets responses; the concrete transport (and its dependency on
 *        a specific HTTP library) stays behind this interface.
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
            std::string method;     ///< e.g. "GET", "POST", "DELETE"
            std::string endpoint;   ///< path appended to the base url
            std::string body;
            std::map<std::string, std::string> headers;

            /// Absolute request URL used verbatim when non-empty; otherwise the
            /// transport composes `base_url + endpoint`. Lets a caller target a
            /// fully-qualified URL (e.g. an authenticated asset download) without
            /// depending on a configured base url.
            std::string url;
        };

        struct Response
        {
            int         status = 0;   ///< HTTP status (0 = transport failure)
            std::string body;
            std::string error;        ///< transport error text when status is 0
            std::map<std::string, std::string> headers;

            bool ok() const { return status >= 200 && status < 300; }
        };

        using Cb = std::function<void(const Response&)>;

        virtual void set_base_url(std::string url) = 0;
        virtual std::string base_url() const = 0;

        /// Issue a request off the main thread; the callback fires on a worker.
        virtual void request_async(const Request& request, Cb callback) = 0;

        /// Issue a request and block until it completes.
        virtual Response request_sync(const Request& request) = 0;

        virtual void set_timeouts(int connect_ms, int transfer_ms) = 0;
    };

} // namespace ports
} // namespace net
} // namespace idtxflow
