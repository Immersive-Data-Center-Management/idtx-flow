#pragma once

/**
 * @file RestClient.h
 * @brief REST orchestration for the collaboration backend: builds requests,
 *        attaches auth, applies protocol rules, and delivers model results.
 *
 * It owns no transport, JSON, or engine types — it drives an IHttpTransport,
 * reads the bearer token from an ITokenProvider, translates bytes through the
 * REST codec, and posts results onto the engine thread via the dispatcher.
 * Results arrive through caller-supplied callbacks so any binding can wire them
 * to its own event sink.
 */

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <idtxflow/net/model/Types.h>
#include <idtxflow/net/ports/IHttpTransport.h>
#include <idtxflow/net/ports/IMainThreadDispatcher.h>
#include <idtxflow/net/ports/ITokenProvider.h>
#include <idtxflow/utils/Logger.h>

namespace idtxflow
{
namespace net
{
    class RestClient
    {
    public:
        using LoginCb   = std::function<void(const model::LoginResult&)>;
        using HealthCb  = std::function<void(const model::HealthResult&)>;
        using ThumbnailCb = std::function<void(const model::ThumbnailResult&)>;
        using FilesCb   = std::function<void(const std::vector<model::FileEntry>&)>;
        using SessionCb = std::function<void(const model::SessionInfo&)>;
        using DeletedCb = std::function<void()>;
        using ErrorCb   = std::function<void(const model::RestError&)>;

        RestClient(ports::IHttpTransport* http,
                   ports::ITokenProvider* token,
                   ports::IMainThreadDispatcher* dispatcher)
            : http_(http), token_(token), dispatcher_(dispatcher) {}

        void set_base_url(const std::string& url);

        /// GET /health (unauthenticated). A reachability/liveness probe: reports
        /// ok on a 2xx response, otherwise an error (including transport failure).
        void health(HealthCb on_ok, ErrorCb on_err);

        /// GET /thumbnail/<usd_file> (authenticated). Returns the image bytes +
        /// content-type. Successful results are cached in-memory keyed by
        /// usd_file, so a repeat request (e.g. re-selecting a file, or re-listing
        /// a directory) is served without another round-trip. Failures (incl.
        /// 404 "not generated yet") are NOT cached, so a later retry can pick up
        /// a thumbnail once the backend produces it.
        void fetch_thumbnail(const std::string& usd_file, ThumbnailCb on_ok, ErrorCb on_err);

        /// Drop all cached thumbnail bytes (e.g. on logout or a base-url change).
        void clear_thumbnail_cache();

        /// POST /auth/login (unauthenticated). Does not store the token; the
        /// caller decides what to do with the result.
        void login(const std::string& username, const std::string& password,
                   LoginCb on_ok, ErrorCb on_err);

        /// GET /files with optional filters (either may be empty). Authenticated.
        void list_files(const std::string& name_contains, const std::string& extension,
                        FilesCb on_ok, ErrorCb on_err);

        /// POST /sessions { usd_file, mode }. Authenticated.
        void create_session(const std::string& usd_file, const std::string& mode,
                            SessionCb on_ok, ErrorCb on_err);

        /// DELETE /sessions/<id>. 2xx and 404 both count as "gone". Authenticated.
        void delete_session(const std::string& session_id,
                            DeletedCb on_ok, ErrorCb on_err);

        /// Derive the WebSocket base from the HTTP base (http->ws, https->wss).
        std::string ws_base_url() const;

        /// Compose the full USD download URL: <base>/api/v1/download/<usd_file>.
        std::string download_url(const std::string& usd_file) const;

    private:
        IDTX_LOG_CATEGORY("RestClient")

        // Apply the protocol rule that a 401 on any operation invalidates the
        // stored token, then hand the error to the caller on the engine thread.
        void report_error(const model::RestError& error, const ErrorCb& on_err);

        // Attach the bearer token to an authenticated request, or short-circuit:
        // if no token is held this is an unauthenticated attempt at a protected
        // endpoint, so deliver a synthetic "not authenticated" error (no network
        // I/O, no noisy 401 log) and return false. Calling this is what marks a
        // request as authenticated.
        bool attach_auth(ports::IHttpTransport::Request& req, const ErrorCb& on_err);

        static std::string url_encode(const std::string& s);

        ports::IHttpTransport*        http_;
        ports::ITokenProvider*        token_;
        ports::IMainThreadDispatcher* dispatcher_;

        // In-memory thumbnail byte cache, keyed by usd_file. Accessed only on the
        // engine thread (all reads/writes happen inside dispatcher-posted work),
        // so no locking is required.
        std::map<std::string, model::ThumbnailResult> thumb_cache_;
    };

} // namespace net
} // namespace idtxflow
