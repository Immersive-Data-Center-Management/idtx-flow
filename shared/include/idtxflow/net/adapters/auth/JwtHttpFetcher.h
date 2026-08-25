#pragma once

/**
 * @file JwtHttpFetcher.h
 * @brief An HttpFetcherLike that attaches the current JWT as a Bearer header.
 *
 * Installed once into the USD HTTP asset resolver, it is then driven from USD's
 * background worker threads to download protected assets. It is a value functor
 * copied into the resolver, so it must not hold a per-instance pointer that
 * could dangle; it reads the current token from the process-wide
 * StaticTokenProvider at fetch time, which also lets token rotation take effect
 * without reconfiguring the resolver.
 *
 * Engine-agnostic: std + IXWebSocket only.
 */

#include <filesystem>
#include <fstream>
#include <string>

#include <ixwebsocket/IXHttpClient.h>

#include <idtxflow/net/adapters/auth/StaticTokenProvider.h>
#include <idtxflow/utils/Logger.h>

namespace idtxflow
{
namespace net
{
namespace adapters
{
    struct JwtHttpFetcher
    {
        IDTX_LOG_CATEGORY("JwtHttpFetcher")

        /// TLS options passed to IXWebSocket's HttpClient (defaults to system CA store).
        ix::SocketTLSOptions tls_options;

        JwtHttpFetcher() = default;
        explicit JwtHttpFetcher(ix::SocketTLSOptions opts) : tls_options(std::move(opts)) {}

        bool operator()(const std::string& url, const std::filesystem::path& dest) const
        {
            std::filesystem::create_directories(dest.parent_path());

            ix::HttpClient client;
            client.setTLSOptions(tls_options);

            ix::HttpRequestArgsPtr args = client.createRequest(url);
            args->followRedirects = true;
            args->maxRedirects = 5;
            args->connectTimeout = 30;
            args->transferTimeout = 120;
            args->compress = false;

            // Attach the bearer token from the process-wide provider if present.
            const std::string auth = StaticTokenProvider::instance().auth_header_value();
            if (!auth.empty())
            {
                args->extraHeaders["Authorization"] = auth;
            }

            auto response = client.get(url, args);

            if (!response || response->statusCode < 200 || response->statusCode >= 300)
            {
                const std::string err = response ? response->errorMsg : "null response";
                const int code = response ? response->statusCode : 0;
                IDTX_LOG(IDTX_ERROR, "Authenticated download failed for '{}': {} (HTTP {})",
                    url, err, code);
                return false;
            }

            std::ofstream file(dest, std::ios::binary);
            if (!file.is_open())
            {
                IDTX_LOG(IDTX_ERROR, "Failed to open file for writing: {}", dest.string());
                return false;
            }

            file.write(response->body.data(), static_cast<std::streamsize>(response->body.size()));
            file.close();

            if (file.fail())
            {
                IDTX_LOG(IDTX_ERROR, "Failed to write file: {}", dest.string());
                std::error_code ec;
                std::filesystem::remove(dest, ec);
                return false;
            }

            IDTX_LOG(IDTX_INFO, "Downloaded (auth): {} -> {} (HTTP {}, {} bytes)",
                url, dest.string(), response->statusCode, response->body.size());
            return true;
        }
    };

} // namespace adapters
} // namespace net
} // namespace idtxflow
