#pragma once

/**
 * @file JwtHttpFetcher.h
 * @brief An HttpFetcherLike that injects the current JWT as a Bearer header.
 *
 * This is a drop-in replacement for `idtxflow::resolver::DefaultHttpFetcher` that
 * adds `Authorization: <type> <token>` (read from IdtxTokenHolder) to each request,
 * so the USD HTTP asset resolver can fetch protected `/api/v1/download/...` assets.
 *
 * Install it once at startup:
 *   pxr::UsdHttpAssetResolver::ConfigureWithFetcher(cache_dir,
 *       idtxflow::net::JwtHttpFetcher{});
 * Then set/clear the token on login/logout via IdtxTokenHolder — the fetcher reads
 * the current token at fetch time, so rotation needs no reconfiguration.
 *
 * Engine-agnostic: std + IXWebSocket only (same dependency surface as the default
 * fetcher). Satisfies the `idtxflow::resolver::HttpFetcherLike` concept.
 */

#include <filesystem>
#include <fstream>
#include <string>

#include <ixwebsocket/IXHttpClient.h>

#include <idtxflow/net/IdtxTokenHolder.h>
#include <idtxflow/utils/Logger.h>

namespace idtxflow
{
namespace net
{
    /**
     * HTTP fetcher (matching HttpFetcherLike) that downloads a URL to a local file,
     * attaching the current bearer token from IdtxTokenHolder when one is present.
     */
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

            // Attach the bearer token if we currently have one.
            const std::string auth = IdtxTokenHolder::AuthHeaderValue();
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

} // namespace net
} // namespace idtxflow