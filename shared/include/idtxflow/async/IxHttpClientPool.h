#pragma once

/**
 * @file IxHttpClientPool.h
 * @brief Process-wide shared HTTP client pool for async requests.
 *
 * Each UsdRestDatasourceNode3D used to own a private async ix::HttpClient, and
 * IXWebSocket's async client spawns one background thread per instance. With many
 * datasource nodes in a scene that is one thread per node — resource usage scales
 * with node count. This pool replaces that with a single, shared facility (reachable
 * like the IdtxClient engine singleton) that caps concurrency to a fixed number of
 * worker threads regardless of how many callers exist.
 *
 * Requests run on a bounded utils::ThreadPool; each task creates a short-lived
 * SYNCHRONOUS ix::HttpClient, performs the request, and invokes the caller's callback
 * on the worker thread — the same pattern IxHttpTransport already uses, promoted to a
 * shared singleton. The caller supplies the ix::HttpRequestArgsPtr (built via
 * createRequest) and an ix::OnResponseCallback, mirroring the previous per-node flow.
 *
 * TLS options are owned here (platform-aware caFile, see IxTlsDefaults.h) so callers no
 * longer configure TLS themselves.
 */

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXSocketTLSOptions.h>

#include <idtxflow/net/adapters/transport/ix/IxTlsDefaults.h>
#include <idtxflow/utils/ThreadPool.h>
#include <idtxflow_godot/idtxflow_godot_api.h>

namespace idtxflow
{
namespace async
{
/**
 * @class IxHttpClientPool
 * @brief Shared, bounded pool for issuing async HTTP requests via IXWebSocket.
 *
 * Only the Instance() accessor crosses the module boundary (see the comment on it);
 * all other members operate on `this`, which refers to the one shared instance.
 */
class IxHttpClientPool
{
  public:
    /// Number of parallel requests processed at once (first-iteration cap).
    static constexpr std::size_t kDefaultPoolSize = 4;

    /**
     * Singleton accessor.
     *
     * Defined OUT-OF-LINE in source/async/IxHttpClientPool.cpp and exported from
     * libidtxflow via IDTXFLOW_GODOT_API — this is deliberate, not a style choice.
     * A header-inline Meyers singleton (a static local in an inline method) is forced
     * to HIDDEN visibility by godot-cpp's -fvisibility=hidden on ELF/Android, so every
     * shared library that includes this header would get its OWN pool. A non-inline,
     * exported accessor lands in libidtxflow's .dynsym with default visibility, so all
     * modules share the one instance. Mirrors ExecBridgeManager::Instance().
     */
    IDTXFLOW_GODOT_API static IxHttpClientPool& Instance();

    /// Build request args for an async call (mirrors ix::HttpClient::createRequest).
    ix::HttpRequestArgsPtr createRequest(const std::string& url, const std::string& verb) const
    {
        auto args = std::make_shared<ix::HttpRequestArgs>();
        args->url = url;
        args->verb = verb;
        return args;
    }

    /**
     * Queue an async HTTP request. The request runs on a pool worker thread; `callback`
     * is invoked there with the response once complete. The verb is read from
     * `args->verb`. Returns false (and never queues) if the pool is shutting down or
     * `args` is null — the caller should treat that as "request not started".
     */
    bool performRequest(ix::HttpRequestArgsPtr args, ix::OnResponseCallback callback)
    {
        if (!args || shutdown_.load(std::memory_order_acquire) || !pool_)
        {
            return false;
        }

        ix::SocketTLSOptions tls = tls_options_; // copy; consulted only for https:// URLs
        pool_->submit([args = std::move(args), callback = std::move(callback), tls = std::move(tls)]() mutable {
            ix::HttpClient client; // synchronous — already wrapped by the worker thread
            client.setTLSOptions(tls);

            ix::HttpResponsePtr response;
            const std::string& verb = args->verb;
            if (verb == ix::HttpClient::kGet)
                response = client.get(args->url, args);
            else if (verb == ix::HttpClient::kPost)
                response = client.post(args->url, args->body, args);
            else if (verb == ix::HttpClient::kDelete)
                response = client.Delete(args->url, args);
            else if (verb == ix::HttpClient::kHead)
                response = client.head(args->url, args);
            else if (verb == ix::HttpClient::kPut)
                response = client.put(args->url, args->body, args);
            else if (verb == ix::HttpClient::kPatch)
                response = client.patch(args->url, args->body, args);
            else
                response = client.request(args->url, verb, args->body, args);

            if (callback)
            {
                callback(response);
            }
        });
        return true;
    }

    /**
     * Stop accepting work and drain + join the worker threads. Idempotent. Call before
     * the hosting module is torn down so no HTTP worker outlives it (mirrors how
     * ExecBridgeManager::Cancel() is invoked at module uninitialize).
     */
    void Shutdown()
    {
        shutdown_.store(true, std::memory_order_release);
        pool_.reset(); // ThreadPool dtor drains the queue and joins all workers
    }

  private:
    IxHttpClientPool() : pool_(std::make_unique<utils::ThreadPool>(kDefaultPoolSize))
    {
        // Platform-aware CA selection (see IxTlsDefaults.h): OS trust store on desktop,
        // verification disabled on Android where MbedTLS cannot resolve "SYSTEM".
        tls_options_.caFile = net::adapters::default_ca_file();
    }

    IxHttpClientPool(const IxHttpClientPool&) = delete;
    IxHttpClientPool& operator=(const IxHttpClientPool&) = delete;

    std::unique_ptr<utils::ThreadPool> pool_;
    ix::SocketTLSOptions tls_options_;
    std::atomic<bool> shutdown_{false};
};

} // namespace async
} // namespace idtxflow
