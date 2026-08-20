#pragma once

/**
 * @file HttpResolver.h
 * @brief Engine-agnostic ArResolver for http:// and https:// URI schemes.
 *
 * This is a **header-only** module providing a complete `ArResolver` subclass that handles
 * HTTP(S) asset resolution for OpenUSD. It works in conjunction with `HttpAssetCache` to
 * fetch remote USD files and cache them locally.
 *
 * ## Resolution strategy
 *
 * - `_CreateIdentifier()`: Returns HTTP(S) URLs as-is (they are already absolute identifiers).
 * - `_Resolve()`: Returns the HTTP(S) URL as the `ArResolvedPath`. This is critical —
 *   keeping the scheme in the resolved path ensures that OpenUSD routes `_OpenAsset()`
 *   back to this resolver rather than the default filesystem resolver.
 * - `_OpenAsset()`: Uses `HttpAssetCache` to download the file (or retrieve from cache),
 *   then opens the local cached copy via `ArFilesystemAsset`.
 *
 * ## Registration
 *
 * This resolver must be registered via `plugInfo.json` for the `http` and `https` URI schemes:
 *
 * ```json
 * {
 *   "Plugins": [{
 *     "Info": {
 *       "Types": {
 *         "UsdHttpAssetResolver": {
 *           "bases": ["ArResolver"],
 *           "implementsContexts": false,
 *           "uriSchemes": ["http", "https"]
 *         }
 *       }
 *     },
 *     "Type": "library",
 *     "Name": "usdHttpResolver",
 *     "LibraryPath": "../../libUsdHttpResolver.so",
 *     "Root": "."
 *   }]
 * }
 * ```
 *
 * ## Engine integration
 *
 * The engine-specific layer must:
 * 1. Include this header
 * 2. Provide a `.cpp` file with `AR_DEFINE_RESOLVER(UsdHttpAssetResolver, ArResolver)`
 * 3. Call `UsdHttpAssetResolver::Configure()` at startup to set the cache directory
 *    (and optionally provide a custom HTTP fetcher)
 * 4. Include the appropriate `plugInfo.json` registration
 *
 * Example engine-specific `.cpp`:
 * ```cpp
 * #include <idtxflow/resolver/HttpResolver.h>
 * #include <pxr/usd/ar/defineResolver.h>
 *
 * PXR_NAMESPACE_OPEN_SCOPE
 * AR_DEFINE_RESOLVER(UsdHttpAssetResolver, ArResolver);
 * PXR_NAMESPACE_CLOSE_SCOPE
 *
 * // At startup:
 * pxr::UsdHttpAssetResolver::Configure("/path/to/cache");
 * ```
 */

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <pxr/base/tf/pathUtils.h>
#include <functional>

#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/ar/filesystemAsset.h>
#include <pxr/usd/ar/filesystemWritableAsset.h>

#include "HttpAssetCache.h"
#include "HttpAuthorization.h"
#include <idtxflow/utils/Logger.h>

PXR_NAMESPACE_OPEN_SCOPE

/**
 * ArResolver implementation for http:// and https:// URI schemes.
 *
 * Fetches remote USD assets to a local cache directory and serves them to OpenUSD
 * as local filesystem assets. All network I/O happens on background threads managed
 * by HttpAssetCache; the resolver's _OpenAsset() blocks the calling thread (which
 * should be a worker thread, not the main thread) until the download completes.
 */
class UsdHttpAssetResolver : public ArResolver
{
    IDTX_LOG_CATEGORY("HttpResolver")
    
public:
    UsdHttpAssetResolver() = default;
    ~UsdHttpAssetResolver() = default;

    // -------------------------------------------------------------------
    // Static configuration API
    // -------------------------------------------------------------------

    /**
     * Configure the HTTP resolver with a cache directory using the default IXWebSocket fetcher.
     * Must be called before any HTTP(S) assets are resolved.
     *
     * @param cache_dir   Local directory for caching downloaded files. Created if it doesn't exist.
     * @param tls_options TLS options for the IXWebSocket HTTP client. Defaults to using the
     *                    system certificate store ("SYSTEM").
     */
    static inline void Configure(const std::filesystem::path& cache_dir,
                                 ix::SocketTLSOptions tls_options = {})
    {
        std::lock_guard lock(ConfigMutex());
        idtxflow::resolver::DefaultHttpFetcher fetcher{std::move(tls_options)};
        Cache() = std::make_shared<idtxflow::resolver::HttpAssetCache<>>(cache_dir, std::move(fetcher));
        IDTX_LOG(IDTX_INFO, "Configured with cache dir: {} (DefaultHttpFetcher)", cache_dir.string());
    }

    /**
     * Set an OAuth Bearer token for a target HTTP origin.
     *
     * A complete URL may be supplied; only its scheme, host, and non-default port are used.
     * A bare host is treated as HTTPS. The token is snapshotted for every request to that
     * origin and is never applied to another origin during redirects.
     */
    static inline bool SetBearerTokenForHost(
        const std::string& target_host,
        const std::string& token)
    {
        return idtxflow::resolver::HttpAuthorizationRegistry::Instance()
            .SetBearerTokenForTarget(target_host, token);
    }

    /**
     * Set a complete Authorization header value for authentication schemes other than Bearer.
     */
    static inline bool SetAuthorizationHeaderForHost(
        const std::string& target_host,
        const std::string& authorization)
    {
        return idtxflow::resolver::HttpAuthorizationRegistry::Instance()
            .SetAuthorizationHeaderForTarget(target_host, authorization);
    }

    static inline bool ClearAuthorizationForHost(const std::string& target_host)
    {
        return idtxflow::resolver::HttpAuthorizationRegistry::Instance()
            .ClearAuthorizationForTarget(target_host);
    }

    static inline bool HasAuthorizationForHost(const std::string& target_host)
    {
        return idtxflow::resolver::HttpAuthorizationRegistry::Instance()
            .HasAuthorizationForTarget(target_host);
    }

    static inline void ClearAllAuthorizations()
    {
        idtxflow::resolver::HttpAuthorizationRegistry::Instance().ClearAllAuthorizations();
    }

    /**
     * Configure the HTTP resolver with a cache directory and a custom HTTP fetcher.
     * Use this when the target engine provides its own HTTP client implementation.
     *
     * @tparam Fetcher  A callable matching `HttpFetcherLike`.
     * @param cache_dir Local directory for caching downloaded files.
     * @param fetcher   The custom HTTP download implementation.
     */
    template<idtxflow::resolver::HttpFetcherLike Fetcher>
    static inline void ConfigureWithFetcher(const std::filesystem::path& cache_dir, Fetcher fetcher)
    {
        std::lock_guard lock(ConfigMutex());
        // Wrap the typed cache in a type-erased adapter so we can store it as a shared_ptr<void>
        auto typed_cache = std::make_shared<idtxflow::resolver::HttpAssetCache<Fetcher>>(
            cache_dir, std::move(fetcher));
        ErasedCache() = typed_cache;
        // Store a resolve function that captures the typed cache
        ResolveFunction() = [typed_cache](const std::string& url) -> std::optional<std::filesystem::path>
        {
            return typed_cache->Resolve(url);
        };
        PrefetchFunction() = [typed_cache](const std::string& url)
        {
            typed_cache->Prefetch(url);
        };
        IsCachedFunction() = [typed_cache](const std::string& url) -> bool
        {
            return typed_cache->IsCached(url);
        };
        IDTX_LOG(IDTX_INFO, "Configured with cache dir: {} (custom fetcher)", cache_dir.string());
    }

protected:
    // -------------------------------------------------------------------
    // ArResolver interface implementation
    // -------------------------------------------------------------------

    std::string _GetExtension(const std::string& path) const override
    {
        return TfGetExtension(path);
    }

    std::string _CreateIdentifier(
        const std::string& assetPath,
        const ArResolvedPath& anchorAssetPath) const override
    {
        // HTTP(S) URLs are already absolute identifiers
        if (IsHttpUrl(assetPath))
        {
            return assetPath;
        }

        // If the asset path is relative and the anchor is an HTTP URL, resolve relative to it
        if (!anchorAssetPath.IsEmpty() && IsHttpUrl(anchorAssetPath.GetPathString()))
        {
            return ResolveRelativeUrl(anchorAssetPath.GetPathString(), assetPath);
        }

        // Not ours — return as-is (the primary resolver will handle it)
        return assetPath;
    }

    std::string _CreateIdentifierForNewAsset(
        const std::string& assetPath,
        const ArResolvedPath& anchorAssetPath) const override
    {
        // Same logic as _CreateIdentifier — HTTP assets are read-only for now
        return _CreateIdentifier(assetPath, anchorAssetPath);
    }

    ArResolvedPath _Resolve(const std::string& assetPath) const override
    {
        if (!IsHttpUrl(assetPath))
        {
            return ArResolvedPath();
        }

        // IMPORTANT: Return the HTTP URL as the resolved path.
        // This keeps the http/https scheme in the resolved path, which ensures that
        // OpenUSD routes _OpenAsset() back to THIS resolver (scheme-based dispatch).
        return ArResolvedPath(assetPath);
    }

    ArResolvedPath _ResolveForNewAsset(const std::string& assetPath) const override
    {
        // HTTP assets are read-only
        return _Resolve(assetPath);
    }

    std::shared_ptr<ArAsset> _OpenAsset(const ArResolvedPath& resolvedPath) const override
    {
        const std::string& url = resolvedPath.GetPathString();

        if (!IsHttpUrl(url))
        {
            return nullptr;
        }

        // Resolve the URL to a local cached file (blocks until download completes or fails)
        auto local_path = ResolveViaCache(url);
        if (!local_path)
        {
            IDTX_LOG(IDTX_ERROR, "Failed to resolve asset: {}", url);
            return nullptr;
        }

        // Open the cached local file as a standard filesystem asset
        std::string asset_path = local_path->generic_string();
        return ArFilesystemAsset::Open(ArResolvedPath(asset_path));
    }

    std::shared_ptr<ArWritableAsset> _OpenAssetForWrite(
        const ArResolvedPath& resolvedPath,
        WriteMode writeMode) const override
    {
        // HTTP assets are read-only — writing is not supported
        IDTX_LOG(IDTX_ERROR, "Write not supported for HTTP assets: {}", resolvedPath.GetPathString());
        return nullptr;
    }

private:
    // -------------------------------------------------------------------
    // URL utilities
    // -------------------------------------------------------------------

    static inline bool IsHttpUrl(const std::string& url)
    {
        return idtxflow::resolver::HttpAssetCache<>::IsHttpUrl(url);
    }

    /**
     * Resolve a relative path against an HTTP base URL.
     * E.g., base="https://server.com/scenes/main.usd", rel="./textures/wood.png"
     *    → "https://server.com/scenes/textures/wood.png"
     */
    static inline std::string ResolveRelativeUrl(const std::string& base_url, const std::string& relative_path)
    {
        // Find the last '/' in the base URL path (after the scheme://host part)
        auto scheme_end = base_url.find("://");
        if (scheme_end == std::string::npos)
        {
            return relative_path;
        }

        auto last_slash = base_url.rfind('/');
        if (last_slash != std::string::npos && last_slash > scheme_end + 2)
        {
            // Replace everything after the last '/' with the relative path
            std::string resolved = base_url.substr(0, last_slash + 1) + relative_path;

            // Simple normalization: resolve "./" and "../" segments
            return NormalizeUrlPath(resolved);
        }

        // Base URL has no path beyond the host — append relative path
        return base_url + "/" + relative_path;
    }

    /**
     * Normalize a URL path by resolving "." and ".." segments.
     */
    static inline std::string NormalizeUrlPath(const std::string& url)
    {
        auto scheme_end = url.find("://");
        if (scheme_end == std::string::npos)
        {
            return url;
        }

        auto host_start = scheme_end + 3;
        auto path_start = url.find('/', host_start);
        if (path_start == std::string::npos)
        {
            return url;
        }

        std::string prefix = url.substr(0, path_start);
        std::string path = url.substr(path_start);

        return prefix + std::filesystem::path(path).lexically_normal().generic_string();
    }

    // -------------------------------------------------------------------
    // Cache access (type-erased for custom fetcher support)
    // -------------------------------------------------------------------

    /**
     * Resolve a URL through the configured cache.
     * Supports both the default IXWebSocket cache and custom-fetcher caches.
     */
    static inline std::optional<std::filesystem::path> ResolveViaCache(const std::string& url)
    {
        std::lock_guard lock(ConfigMutex());

        // Check for custom fetcher (type-erased) first
        if (ResolveFunction())
        {
            return ResolveFunction()(url);
        }
        
        // Fall back to default cache
        if (Cache())
        {
            return Cache()->Resolve(url);
        }
        
        IDTX_LOG(IDTX_ERROR, "Resolver not configured. Call UsdHttpAssetResolver::Configure() or ConfigureWithFetcher() before use.");
        return std::nullopt;
    }

    // -------------------------------------------------------------------
    // Static storage (Meyer's singletons for header-only safety)
    // -------------------------------------------------------------------

    static inline std::mutex& ConfigMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    // Direct storage for default fetcher cache
    using DefaultCache = idtxflow::resolver::HttpAssetCache<idtxflow::resolver::DefaultHttpFetcher>;

    static inline std::shared_ptr<DefaultCache>& Cache()
    {
        static std::shared_ptr<DefaultCache> cache;
        return cache;
    }

    // Type-erased storage for custom fetcher caches
    static inline std::shared_ptr<void>& ErasedCache()
    {
        static std::shared_ptr<void> cache;
        return cache;
    }

    using ResolveFn = std::function<std::optional<std::filesystem::path>(const std::string&)>;
    using PrefetchFn = std::function<void(const std::string&)>;
    using IsCachedFn = std::function<bool(const std::string&)>;

    static inline ResolveFn& ResolveFunction()
    {
        static ResolveFn fn;
        return fn;
    }

    static inline PrefetchFn& PrefetchFunction()
    {
        static PrefetchFn fn;
        return fn;
    }

    static inline IsCachedFn& IsCachedFunction()
    {
        static IsCachedFn fn;
        return fn;
    }
};

PXR_NAMESPACE_CLOSE_SCOPE
