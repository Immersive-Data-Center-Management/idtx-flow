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

#include <iostream>
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
        IDTXFLOW_API std::string get() const override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return token_;
        }

        IDTXFLOW_API void set(std::string access_token, std::string type = "Bearer") override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            token_ = std::move(access_token);
            token_type_ = type.empty() ? std::string("Bearer") : std::move(type);
        }

        IDTXFLOW_API void clear() override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            token_.clear();
        }

        IDTXFLOW_API std::string auth_header_value() const override
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (token_.empty())
            {
                return {};
            }
            return token_type_ + " " + token_;
        }

        /// The process-wide provider shared by the engine and the USD fetcher.
        IDTXFLOW_API static StaticTokenProvider& instance()
        {
            static StaticTokenProvider provider;
            return provider;
        }

    private:
        explicit StaticTokenProvider() {}
        ~StaticTokenProvider() = default;

        // disallow copy construct on the StaticTokenProvider 
        StaticTokenProvider(const StaticTokenProvider&) = delete;
        StaticTokenProvider& operator=(const StaticTokenProvider&) = delete;

        std::string token_type_ = "Bearer";
        std::string token_;
        mutable std::mutex mutex_;
    };

} // namespace adapters
} // namespace net
} // namespace idtxflow
