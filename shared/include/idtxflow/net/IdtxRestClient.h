#pragma once

/**
 * @file IdtxRestClient.h
 * @brief Engine-agnostic REST client for the IDTX-Core /api/v1 backend.
 *
 * Implements the HTTP/JSON side of the collaboration contract:
 *   - POST   /api/v1/auth/login
 *   - GET    /api/v1/files
 *   - POST   /api/v1/sessions
 *   - DELETE /api/v1/sessions/<id>
 *
 * Uses IXWebSocket's ix::HttpClient for transport (already linked) and reports
 * results through std::function callbacks with plain C++ structs — no engine
 * (Godot) types. The Godot binding layer (source/net) converts these to
 * Dictionaries/Arrays and marshals callbacks onto the main thread.
 *
 * Threading: each call spawns a detached worker thread (ix::HttpClient blocks),
 * so callbacks fire on a background thread. Callers that need main-thread
 * delivery must marshal themselves (the Godot shim does).
 */

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace idtxflow
{
namespace net
{
    /// A backend error payload: { "error": code, "message": msg } plus HTTP status.
    struct RestError
    {
        int         http_code = 0;      ///< HTTP status (0 = transport failure).
        std::string error_code;         ///< snake_case code from the body, or a transport tag.
        std::string message;            ///< human-readable message.
    };

    /// Result of POST /api/v1/auth/login.
    struct LoginResult
    {
        std::string access_token;
        std::string token_type = "Bearer";
        int64_t     expires_in = 0;     ///< seconds
        std::string refresh_token;
        std::string scope;
    };

    /// A single file entry from GET /api/v1/files.
    struct FileEntry
    {
        std::string filepath;           ///< e.g. "scenes/foo.usda" (used for sessions + download)
        std::string filename;           ///< e.g. "foo.usda"
        std::string directory;          ///< e.g. "scenes"
        int64_t     size = 0;
        int64_t     modified = 0;       ///< opaque; sort key only
    };

    /// Result of POST /api/v1/sessions (and GET /api/v1/sessions/<id>).
    struct SessionInfo
    {
        std::string session_id;
        std::string usd_file;
        std::string mode;               ///< e.g. "single_edit"
        int64_t     client_count = 0;
        int64_t     created_at = 0;
        std::string ws_url;             ///< e.g. "/ws?sid=..." (relative to base)
        std::string protocol;           ///< e.g. "protobuf-binary"
    };

    // Callback signatures (fired on a background worker thread).
    using LoginCallback   = std::function<void(const LoginResult&)>;
    using FilesCallback   = std::function<void(const std::vector<FileEntry>&)>;
    using SessionCallback = std::function<void(const SessionInfo&)>;
    using DeleteCallback  = std::function<void()>;
    using ErrorCallback   = std::function<void(const RestError&)>;

    /**
     * Stateless-ish REST client. Holds only the base URL; the bearer token is read
     * from IdtxTokenHolder at call time so it stays in sync with login/logout.
     */
    class IdtxRestClient
    {
    public:
        IdtxRestClient() = default;
        explicit IdtxRestClient(std::string base_url) : base_url_(NormalizeBase(std::move(base_url))) {}

        void set_base_url(const std::string& url) { base_url_ = NormalizeBase(url); }
        const std::string& base_url() const { return base_url_; }

        /**
         * POST /api/v1/auth/login  (no auth header).
         * On 200 → on_ok(LoginResult). On failure → on_err(RestError).
         * NOTE: this does NOT store the token; the caller decides (the shim calls
         * IdtxTokenHolder::Set after a successful login).
         */
        void login(const std::string& username,
                   const std::string& password,
                   LoginCallback on_ok,
                   ErrorCallback on_err) const;

        /**
         * GET /api/v1/files  (optional filters; both may be empty).
         * Sends the current bearer token.
         */
        void list_files(const std::string& name_contains,
                        const std::string& extension,
                        FilesCallback on_ok,
                        ErrorCallback on_err) const;

        /**
         * POST /api/v1/sessions { usd_file, mode }.
         * Sends the current bearer token.
         */
        void create_session(const std::string& usd_file,
                            const std::string& mode,
                            SessionCallback on_ok,
                            ErrorCallback on_err) const;

        /**
         * DELETE /api/v1/sessions/<session_id>. 204 and 404 both count as "gone".
         * Sends the current bearer token. on_err only fires for unexpected failures.
         */
        void delete_session(const std::string& session_id,
                            DeleteCallback on_ok,
                            ErrorCallback on_err) const;

        /**
         * Derive the WebSocket base from the configured HTTP base:
         *   http://host:port  -> ws://host:port
         *   https://host:port -> wss://host:port
         * The relative ws_url (from SessionInfo) is appended by the caller.
         */
        std::string ws_base_url() const;

        /// Compose the full USD download URL for a file: <base>/api/v1/download/<usd_file>
        std::string download_url(const std::string& usd_file) const;

    private:
        static std::string NormalizeBase(std::string url);

        std::string base_url_ = "http://localhost:8080";
    };

} // namespace net
} // namespace idtxflow