#pragma once

/**
 * @file HttpAssetCache.h
 * @brief Engine-agnostic HTTP asset fetching with local disk caching and background threading.
 *
 * This is a **header-only** module that provides:
 *   - A concept `HttpFetcherLike` for pluggable HTTP download implementations
 *   - A default `DefaultHttpFetcher` that uses IXWebSocket's ix::HttpClient
 *   - An `HttpAssetCache` class that manages background fetches, local caching,
 *     and thread-safe resolution of HTTP(S) URLs to local file paths
 *
 * ## Design principles
 *
 * - **Engine-agnostic**: Uses only C++ standard library + IXWebSocket (for the default fetcher).
 *   Game engines can provide their own fetcher via the `HttpFetcherLike` concept.
 * - **Non-blocking for the main thread**: The caller (game engine) is expected to invoke
 *   USD layer loading from a background thread. `Resolve()` blocks that worker thread
 *   until the fetch completes, but never touches the main thread.
 * - **Concurrent fetch deduplication**: If multiple threads request the same URL simultaneously,
 *   only one fetch is performed; others wait on a condition variable.
 *
 * ## Cache layout
 *
 * ```
 * {cache_dir}/
 *   {sha256_of_url}/          # hash-based subdirectory (unique per URL)
 *     original_filename.usd   # original filename and extension preserved
 * ```
 *
 * ## Usage
 *
 * ```cpp
 * // With default IXWebSocket fetcher:
 * auto cache = HttpAssetCache(std::filesystem::path("/tmp/usd_cache"));
 *
 * // With custom engine-specific fetcher:
 * auto cache = HttpAssetCache(cache_path, MyEngineFetcher{});
 *
 * // Resolve (blocks worker thread until file is available):
 * auto local = cache.Resolve("https://server.com/models/chair.usd");
 * if (local) {
 *     // Open local->string() with ArFilesystemAsset
 * }
 * ```
 */

#include <atomic>
#include <condition_variable>
#include <concepts>
#include <filesystem>
#include <map>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "detail/Sha256.h"
#include <idtxflow/utils/Logger.h>
#include <ixwebsocket/IXHttpClient.h>

namespace idtxflow
{
namespace resolver
{

// -----------------------------------------------------------------------
// HttpFetcherLike concept
// -----------------------------------------------------------------------

/**
 * A callable that downloads a URL to a local file path.
 * Must return true on success, false on failure.
 *
 * Signature: bool(const std::string& url, const std::filesystem::path& destination)
 */
template <typename F>
concept HttpFetcherLike = requires(F f, const std::string& url, const std::filesystem::path& dest) {
    { f(url, dest) } -> std::convertible_to<bool>;
};

// -----------------------------------------------------------------------
// Default fetcher: DefaultHttpFetcher (IXWebSocket)
// -----------------------------------------------------------------------

/**
 * Default HTTP fetcher using IXWebSocket's ix::HttpClient.
 * Downloads a URL to a local file. Supports HTTP/HTTPS, redirects, and timeouts.
 *
 * This fetcher is always available - IXWebSocket is bundled as a required dependency.
 *
 * TLS options can be configured at construction time:
 * - Default: `caFile = "SYSTEM"` - uses the OS certificate store.
 * - `caFile = "NONE"` - disables certificate verification.
 * - `caFile = "/path/to/ca-bundle.pem"` - uses a custom CA bundle file.
 */
struct DefaultHttpFetcher
{
    IDTX_LOG_CATEGORY("HttpFetcher")

    /// TLS options passed to IXWebSocket's HttpClient.
    /// Default uses the system certificate store ("SYSTEM").
    ix::SocketTLSOptions tls_options;

    /// Default constructor - uses system CA store for certificate verification.
    DefaultHttpFetcher()
    {
        IDTX_LOG(IDTX_INFO, "TLS initialized: caFile='{}' (default)",
                 tls_options.caFile.empty() ? "(empty/system)" : tls_options.caFile);
    }

    /// Construct with explicit TLS options.
    explicit DefaultHttpFetcher(ix::SocketTLSOptions opts)
        : tls_options(std::move(opts))
    {
        IDTX_LOG(IDTX_INFO, "TLS initialized: caFile='{}', certFile='{}', keyFile='{}', peerVerifyDisabled={}",
                 tls_options.caFile.empty() ? "(empty/system)" : tls_options.caFile,
                 tls_options.certFile.empty() ? "(none)" : tls_options.certFile,
                 tls_options.keyFile.empty() ? "(none)" : tls_options.keyFile,
                 tls_options.isPeerVerifyDisabled() ? "true" : "false");
    }

    inline bool operator()(const std::string& url, const std::filesystem::path& dest) const
    {
        // Ensure the destination directory exists
        std::filesystem::create_directories(dest.parent_path());

        IDTX_LOG(IDTX_DEBUG, "Fetching '{}' TLS: caFile='{}', certFile='{}', keyFile='{}', peerVerifyDisabled={}", url,
                 tls_options.caFile.empty() ? "(empty/system)" : tls_options.caFile,
                 tls_options.certFile.empty() ? "(none)" : tls_options.certFile,
                 tls_options.keyFile.empty() ? "(none)" : tls_options.keyFile,
                 tls_options.isPeerVerifyDisabled() ? "true" : "false");

        ix::HttpClient client;
        client.setTLSOptions(tls_options);

        ix::HttpRequestArgsPtr args = client.createRequest(url);
        args->followRedirects = true;
        args->maxRedirects = 5;
        args->connectTimeout = 30;
        args->transferTimeout = 120;
        args->compress = false;

        auto response = client.get(url, args);

        if (!response || response->statusCode < 200 || response->statusCode >= 300)
        {
            const std::string err = response ? response->errorMsg : "null response";
            const int code = response ? response->statusCode : 0;
            IDTX_LOG(IDTX_ERROR, "Download failed for '{}': {} (HTTP {})", url, err, code);
            IDTX_LOG(IDTX_ERROR, "  TLS context: caFile='{}', certFile='{}', keyFile='{}', peerVerifyDisabled={}",
                     tls_options.caFile.empty() ? "(empty/system)" : tls_options.caFile,
                     tls_options.certFile.empty() ? "(none)" : tls_options.certFile,
                     tls_options.keyFile.empty() ? "(none)" : tls_options.keyFile,
                     tls_options.isPeerVerifyDisabled() ? "true" : "false");
            return false;
        }

        // Write response body to the destination file
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

        IDTX_LOG(IDTX_INFO, "Downloaded: {} -> {} (HTTP {}, {} bytes)", url, dest.string(), response->statusCode,
                 response->body.size());
        return true;
    }
};

// -----------------------------------------------------------------------
// Fetch state tracking
// -----------------------------------------------------------------------

enum class FetchState
{
    Idle,     ///< No fetch has been initiated
    Fetching, ///< A background fetch is in progress
    Cached,   ///< File is available in the local cache
    Failed    ///< Fetch was attempted but failed
};

// -----------------------------------------------------------------------
// HttpAssetCache
// -----------------------------------------------------------------------

/**
 * Manages downloading HTTP(S) assets to a local cache directory with background
 * threading and deduplication.
 *
 * @tparam Fetcher A callable matching `HttpFetcherLike` — downloads a URL to a file.
 *                 Defaults to `DefaultHttpFetcher`.
 */
template <HttpFetcherLike Fetcher = DefaultHttpFetcher> class HttpAssetCache
{
    IDTX_LOG_CATEGORY("HttpCache")

  public:
    /**
     * Construct a cache backed by the given directory.
     *
     * @param cache_dir   The root directory for cached files. Created if it doesn't exist.
     * @param fetcher     The HTTP download implementation to use.
     */
    explicit HttpAssetCache(std::filesystem::path cache_dir, Fetcher fetcher = {})
        : cache_dir_(std::move(cache_dir)),
          fetcher_(std::move(fetcher))
    {
        std::error_code ec;
        std::filesystem::create_directories(cache_dir_, ec);
        if (ec)
        {
            IDTX_LOG(IDTX_WARN, "Failed to create cache directory '{}': {}", cache_dir_.string(), ec.message());
        }
    }

    ~HttpAssetCache()
    {
        // Wait for any in-flight fetches to complete
        std::unique_lock lock(mutex_);
        for (auto& [url, entry]: entries_)
        {
            if (entry->fetch_thread.joinable())
            {
                lock.unlock();
                entry->fetch_thread.join();
                lock.lock();
            }
        }
    }

    // Non-copyable, movable
    HttpAssetCache(const HttpAssetCache&) = delete;
    HttpAssetCache& operator=(const HttpAssetCache&) = delete;
    HttpAssetCache(HttpAssetCache&&) = default;
    HttpAssetCache& operator=(HttpAssetCache&&) = default;

    /**
     * Check whether a URL uses the http:// or https:// scheme.
     */
    static inline bool IsHttpUrl(const std::string& url)
    {
        return url.starts_with("http://") || url.starts_with("https://");
    }

    /**
     * Resolve an HTTP(S) URL to a local cached file path.
     *
     * - If the file is already cached, returns the local path immediately.
     * - If a fetch is not yet started, initiates a background download and waits.
     * - If a fetch is already in progress (from another thread), waits for it.
     *
     * **This call blocks the calling thread** until the file is available or the
     * fetch fails. It is intended to be called from a worker thread, never from
     * the main/render thread.
     *
     * @param url  The HTTP(S) URL to resolve.
     * @return     Local filesystem path to the cached file, or std::nullopt on failure.
     */
    inline std::optional<std::filesystem::path> Resolve(const std::string& url)
    {
        auto entry = GetOrCreateEntry(url);

        std::unique_lock lock(entry->mutex);

        // Fast path: already cached
        if (entry->state == FetchState::Cached)
        {
            return entry->local_path;
        }

        // If idle, kick off the fetch
        if (entry->state == FetchState::Idle)
        {
            entry->state = FetchState::Fetching;
            entry->local_path = UrlToCachePath(url);

            // Launch fetch on a detached worker — entry lifetime is managed by shared_ptr
            std::string url_copy = url;
            auto entry_ref = entry;
            entry->fetch_thread = std::thread([this, url_copy, entry_ref]() {
                bool success = fetcher_(url_copy, entry_ref->local_path);
                {
                    std::lock_guard lk(entry_ref->mutex);
                    entry_ref->state = success ? FetchState::Cached : FetchState::Failed;
                }
                entry_ref->cv.notify_all();
            });
        }

        // Wait for completion (Fetching → Cached or Failed)
        entry->cv.wait(lock,
                       [&entry]() { return entry->state == FetchState::Cached || entry->state == FetchState::Failed; });

        if (entry->state == FetchState::Cached)
        {
            return entry->local_path;
        }

        return std::nullopt;
    }

    /**
     * Initiate a background fetch without waiting for the result.
     * Useful for pre-warming the cache (e.g., prefetching referenced assets).
     *
     * @param url  The HTTP(S) URL to prefetch.
     */
    inline void Prefetch(const std::string& url)
    {
        auto entry = GetOrCreateEntry(url);

        std::lock_guard lock(entry->mutex);

        if (entry->state != FetchState::Idle)
        {
            return; // Already fetching, cached, or failed
        }

        entry->state = FetchState::Fetching;
        entry->local_path = UrlToCachePath(url);

        std::string url_copy = url;
        auto entry_ref = entry;
        entry->fetch_thread = std::thread([this, url_copy, entry_ref]() {
            bool success = fetcher_(url_copy, entry_ref->local_path);
            {
                std::lock_guard lk(entry_ref->mutex);
                entry_ref->state = success ? FetchState::Cached : FetchState::Failed;
            }
            entry_ref->cv.notify_all();
        });
    }

    /**
     * Check if a URL is already cached locally (non-blocking).
     */
    inline bool IsCached(const std::string& url) const
    {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(url);
        if (it == entries_.end())
        {
            // Check if it exists on disk from a previous session
            auto path = UrlToCachePath(url);
            return std::filesystem::exists(path);
        }
        std::lock_guard entry_lock(it->second->mutex);
        return it->second->state == FetchState::Cached;
    }

    /**
     * Evict a specific URL from the cache (deletes the local file and directory).
     */
    inline void Evict(const std::string& url)
    {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(url);
        if (it != entries_.end())
        {
            std::lock_guard entry_lock(it->second->mutex);
            if (it->second->fetch_thread.joinable())
            {
                IDTX_LOG(IDTX_WARN, "Cannot evict URL while fetch is in progress: {}", url);
                return;
            }
            std::error_code ec;
            std::filesystem::remove_all(it->second->local_path.parent_path(), ec);
            entries_.erase(it);
        }
        else
        {
            // Remove from disk if it exists from a previous session
            auto path = UrlToCachePath(url);
            std::error_code ec;
            std::filesystem::remove_all(path.parent_path(), ec);
        }
    }

    /**
     * Remove all cached files.
     */
    inline void ClearCache()
    {
        std::lock_guard lock(mutex_);

        // Wait for all in-flight fetches
        for (auto& [url, entry]: entries_)
        {
            if (entry->fetch_thread.joinable())
            {
                entry->fetch_thread.join();
            }
        }
        entries_.clear();

        std::error_code ec;
        std::filesystem::remove_all(cache_dir_, ec);
        std::filesystem::create_directories(cache_dir_, ec);
    }

    /**
     * Get the root cache directory path.
     */
    inline const std::filesystem::path& GetCacheDirectory() const
    {
        return cache_dir_;
    }

  private:
    /**
     * Per-URL state tracking entry.
     */
    struct FetchEntry
    {
        FetchState state = FetchState::Idle;
        std::filesystem::path local_path;
        mutable std::mutex mutex;
        std::condition_variable cv;
        std::thread fetch_thread;

        ~FetchEntry()
        {
            if (fetch_thread.joinable())
            {
                fetch_thread.join();
            }
        }
    };

    std::filesystem::path cache_dir_;
    Fetcher fetcher_;

    mutable std::mutex mutex_; // Protects entries_ map
    std::map<std::string, std::shared_ptr<FetchEntry>> entries_;

    /**
     * Get or create a FetchEntry for the given URL.
     * Thread-safe — acquires the map-level mutex.
     */
    inline std::shared_ptr<FetchEntry> GetOrCreateEntry(const std::string& url)
    {
        std::lock_guard lock(mutex_);
        auto& entry = entries_[url];
        if (!entry)
        {
            entry = std::make_shared<FetchEntry>();

            // Check if this URL was cached in a previous session (file exists on disk)
            auto path = UrlToCachePath(url);
            if (std::filesystem::exists(path))
            {
                entry->state = FetchState::Cached;
                entry->local_path = path;
            }
        }
        return entry;
    }

    /**
     * Convert a URL to a local cache path.
     *
     * Layout: {cache_dir}/{sha256_of_url}/{original_filename.ext}
     *
     * The hash ensures uniqueness per URL. The original filename is preserved
     * for human readability and so that OpenUSD can identify the file format
     * from the extension.
     */
    inline std::filesystem::path UrlToCachePath(const std::string& url) const
    {
        // Extract the original filename from the URL
        std::string filename = ExtractFilename(url);
        if (filename.empty())
        {
            filename = "cached_asset";
        }

        // Hash the full URL for the directory name
        std::string hash = detail::Sha256::HashString(url);
        std::filesystem::path cache_file_location = (cache_dir_ / hash / filename).lexically_normal();

        // Verify the resolved path does not escape the expected cache directory.
        // A malicious filename (e.g. "../../secret") could cause path traversal.
        std::filesystem::path expected_base = (cache_dir_ / hash).lexically_normal();
        std::filesystem::path relative = cache_file_location.lexically_relative(expected_base);
        if (relative.empty() || relative.string().starts_with(".."))
        {
            IDTX_LOG(IDTX_ERROR, "Path traversal detected: filename '{}' escapes cache directory", filename);
            return {};
        }

        return cache_file_location;
    }

    /**
     * Extract the filename (with extension) from a URL.
     * E.g., "https://server.com/path/to/chair.usd?v=2" → "chair.usd"
     */
    static inline std::string ExtractFilename(const std::string& url)
    {
        // Strip query string and fragment
        std::string clean_url = url;
        auto query_pos = clean_url.find('?');
        if (query_pos != std::string::npos)
        {
            clean_url = clean_url.substr(0, query_pos);
        }
        auto fragment_pos = clean_url.find('#');
        if (fragment_pos != std::string::npos)
        {
            clean_url = clean_url.substr(0, fragment_pos);
        }

        // Find the last '/' to get the filename
        auto slash_pos = clean_url.rfind('/');
        if (slash_pos != std::string::npos && slash_pos + 1 < clean_url.size())
        {
            return clean_url.substr(slash_pos + 1);
        }

        return {};
    }
};

} // namespace resolver
} // namespace idtxflow