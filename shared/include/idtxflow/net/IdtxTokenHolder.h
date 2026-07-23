#pragma once

/**
 * @file IdtxTokenHolder.h
 * @brief Process-wide, thread-safe holder for the current IDTX bearer (JWT) token.
 *
 * The collaboration client authenticates over REST and receives a JWT. That token
 * must also be attached to:
 *   - subsequent REST calls,
 *   - the WebSocket upgrade handshake, and
 *   - the USD asset fetcher used by the HTTP asset resolver (see JwtHttpFetcher).
 *
 * The USD asset resolver is configured **once** with a fetcher (via
 * `UsdHttpAssetResolver::ConfigureWithFetcher`) and is then driven from USD's
 * background worker threads. To let token rotation take effect without
 * reconfiguring the resolver, the fetcher reads the *current* token from this
 * shared, static holder at fetch time rather than capturing it by value.
 *
 * Engine-agnostic (std only); lives under shared/ alongside the resolver plumbing.
 */

#include <mutex>
#include <string>

namespace idtxflow
{
namespace net
{
    /**
     * A Meyer's-singleton style, thread-safe holder for the active bearer token.
     *
     * All access is guarded by an internal mutex. Set the token on login, clear it
     * on logout / 401. Readers (REST, WS, asset fetcher) call `Get()` / `AuthHeaderValue()`.
     */
    class IdtxTokenHolder
    {
    public:
        /// Set (or replace) the current access token and its type (e.g. "Bearer").
        static void Set(const std::string& access_token, const std::string& token_type = "Bearer")
        {
            std::lock_guard<std::mutex> lock(Mutex());
            Token() = access_token;
            TokenType() = token_type.empty() ? std::string("Bearer") : token_type;
        }

        /// Clear the current token (e.g. on logout or after a 401).
        static void Clear()
        {
            std::lock_guard<std::mutex> lock(Mutex());
            Token().clear();
            // keep the token type default
        }

        /// Return the raw access token (empty string if none).
        static std::string Get()
        {
            std::lock_guard<std::mutex> lock(Mutex());
            return Token();
        }

        /// True if a non-empty token is currently held.
        static bool HasToken()
        {
            std::lock_guard<std::mutex> lock(Mutex());
            return !Token().empty();
        }

        /**
         * Return the full HTTP Authorization header *value* (e.g. "Bearer eyJ...").
         * Returns an empty string when no token is held (caller should then omit
         * the header entirely).
         */
        static std::string AuthHeaderValue()
        {
            std::lock_guard<std::mutex> lock(Mutex());
            if (Token().empty())
            {
                return {};
            }
            return TokenType() + " " + Token();
        }

    private:
        static std::mutex& Mutex()
        {
            static std::mutex m;
            return m;
        }

        static std::string& Token()
        {
            static std::string t;
            return t;
        }

        static std::string& TokenType()
        {
            static std::string tt = "Bearer";
            return tt;
        }
    };

} // namespace net
} // namespace idtxflow