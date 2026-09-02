#pragma once

/**
 * @file ITokenProvider.h
 * @brief Port for the current bearer token, shared by everything that must
 *        authenticate: REST calls, the WebSocket upgrade, and the USD asset
 *        fetcher. Readers ask for the token at use time so rotation on
 *        login/logout takes effect without reconfiguring them.
 */

#include <string>

namespace idtxflow
{
namespace net
{
namespace ports
{
    struct ITokenProvider
    {
        virtual ~ITokenProvider() = default;

        /// The raw bearer token, or an empty string when none is held.
        virtual std::string get() const = 0;

        /// Store (or replace) the token and its type (e.g. "Bearer").
        virtual void set(std::string token, std::string type = "Bearer") = 0;

        /// Forget the current token (on logout or after a 401).
        virtual void clear() = 0;

        /// The full Authorization header value ("Bearer x"), or empty to omit the
        /// header entirely when no token is held.
        virtual std::string auth_header_value() const = 0;
    };

} // namespace ports
} // namespace net
} // namespace idtxflow
