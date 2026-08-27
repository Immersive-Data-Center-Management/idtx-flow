#pragma once

/**
 * @file JwtHttpFetcher.h
 * @brief An HttpFetcherLike that attaches the current JWT as a Bearer header.
 *
 * Installed once into the USD HTTP asset resolver, it is then driven from USD's
 * background worker threads to download protected assets. It is a value functor
 * copied into the resolver, so it holds its transport as a shared_ptr (safe to
 * copy) and reads the current token from the process-wide StaticTokenProvider at
 * fetch time, which also lets token rotation take effect without reconfiguring
 * the resolver.
 *
 * Engine- and transport-agnostic: it talks only to the IHttpTransport port and
 * names no HTTP library. The concrete transport (and its dependency on a
 * specific HTTP library) is injected by the composition root.
 */

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <idtxflow/net/adapters/auth/StaticTokenProvider.h>
#include <idtxflow/net/ports/IHttpTransport.h>
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

        /// Transport used to perform the download. Shared so the value functor
        /// can be copied into the resolver without dangling.
        std::shared_ptr<ports::IHttpTransport> http;

        JwtHttpFetcher() = default;
        explicit JwtHttpFetcher(std::shared_ptr<ports::IHttpTransport> transport)
            : http(std::move(transport))
        {
        }

        bool operator()(const std::string& url, const std::filesystem::path& dest) const
        {
            if (!http)
            {
                IDTX_LOG(IDTX_ERROR, "No transport configured; cannot fetch '{}'", url);
                return false;
            }

            std::filesystem::create_directories(dest.parent_path());

            ports::IHttpTransport::Request req;
            req.method = "GET";
            req.url = url; // absolute URL used verbatim by the transport

            // Attach the bearer token from the process-wide provider if present.
            const std::string auth = StaticTokenProvider::instance().auth_header_value();
            if (!auth.empty())
            {
                req.headers["Authorization"] = auth;
            }

            const ports::IHttpTransport::Response resp = http->request_sync(req);

            if (!resp.ok())
            {
                IDTX_LOG(IDTX_ERROR, "Authenticated download failed for '{}': {} (HTTP {})",
                    url, resp.error.empty() ? "request failed" : resp.error, resp.status);
                return false;
            }

            std::ofstream file(dest, std::ios::binary);
            if (!file.is_open())
            {
                IDTX_LOG(IDTX_ERROR, "Failed to open file for writing: {}", dest.string());
                return false;
            }

            file.write(resp.body.data(), static_cast<std::streamsize>(resp.body.size()));
            file.close();

            if (file.fail())
            {
                IDTX_LOG(IDTX_ERROR, "Failed to write file: {}", dest.string());
                std::error_code ec;
                std::filesystem::remove(dest, ec);
                return false;
            }

            IDTX_LOG(IDTX_INFO, "Downloaded (auth): {} -> {} (HTTP {}, {} bytes)",
                url, dest.string(), resp.status, resp.body.size());
            return true;
        }
    };

} // namespace adapters
} // namespace net
} // namespace idtxflow

