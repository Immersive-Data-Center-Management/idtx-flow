#pragma once

/**
 * @file StaticTokenProvider.h
 * @brief ITokenProvider over process-wide, thread-safe token storage.
 *
 * The bearer token must be visible to everything that authenticates: REST calls,
 * the WebSocket upgrade, and the USD asset fetcher running on USD worker threads.
 * Storage is therefore process-wide and static, so a fetcher captured by value
 * (see JwtHttpFetcher) reads the current token at fetch time without holding a
 * pointer that could dangle. Set on login, cleared on logout / 401.
 *
 * Engine-agnostic: standard library only.
 */

#include <mutex>
#include <string>
#include <utility>

#include <idtxflow/net/ports/ITokenProvider.h>

namespace idtxflow
{
namespace net
{
namespace adapters
{
    class StaticTokenProvider : public ports::ITokenProvider
    {
    public:
        std::string get() const override
        {
            std::lock_guard<std::mutex> lock(mutex());
            return token();
        }

        void set(std::string access_token, std::string type = "Bearer") override
        {
            std::lock_guard<std::mutex> lock(mutex());
            token() = std::move(access_token);
            token_type() = type.empty() ? std::string("Bearer") : std::move(type);
        }

        void clear() override
        {
            std::lock_guard<std::mutex> lock(mutex());
            token().clear();
        }

        std::string auth_header_value() const override
        {
            std::lock_guard<std::mutex> lock(mutex());
            if (token().empty())
            {
                return {};
            }
            return token_type() + " " + token();
        }

        /// The process-wide provider shared by the engine and the USD fetcher.
        static StaticTokenProvider& instance()
        {
            static StaticTokenProvider provider;
            return provider;
        }

    private:
        // Process-wide storage guarded by a single mutex; shared across all
        // instances so a value-copied fetcher sees the same current token.
        static std::mutex& mutex()
        {
            static std::mutex m;
            return m;
        }
        static std::string& token()
        {
            static std::string t;
            return t;
        }
        static std::string& token_type()
        {
            static std::string tt = "Bearer";
            return tt;
        }
    };

} // namespace adapters
} // namespace net
} // namespace idtxflow
