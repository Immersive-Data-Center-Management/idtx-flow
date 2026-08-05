#pragma once
/**
 * @file GodotEnvironmentProviders.h
 * @brief Godot-side implementations of the host-agnostic idtx::IEnvironmentProvider
 *        contract used by the Compute_Environment USD computation.
 *
 * The IDTX USD library (`libidtx_usd`) exposes an EnvironmentProviderRegistry and the
 * IEnvironmentProvider interface (see <idtx/EnvironmentProvider.h>, copied into the SDK
 * include dir by the SCons build). It knows nothing about Godot. Here on the host side we
 * implement concrete providers and register them under distinct key prefixes:
 *
 *   "env:NAME"      -> GodotEnvVarProvider     (reads process environment variables)
 *   "auth:NAME"     -> GodotAuthProvider       (resolves/refreshes OAuth/OIDC tokens)
 *   "var:NAME"      -> GodotVarProvider        (built-in dynamic variables, e.g. timestamps)
 *   "project:PATH"  -> GodotProjectSettingProvider (reads Godot ProjectSettings)
 *
 * Thread-safety
 * -------------
 * The IDTX exec worker calls into IEnvironmentProvider::Resolve() from a BACKGROUND
 * thread (see ExecBridgeManager). Environment-variable reads via std::getenv are fine for
 * read-only use. Godot's ProjectSettings, however, is a main-thread-affine singleton, so
 * the GodotProjectSettingProvider takes an immutable snapshot of the relevant settings on
 * the MAIN thread at construction time and only reads from that snapshot afterwards.
 */

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>

#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <ixwebsocket/IXHttpClient.h>

#include <idtx/EnvironmentProvider.h>

namespace idtxflow
{
namespace exec
{
    /**
     * @brief Resolves auth token requests from the internal cache or requests a new one from the auth endpoints
     * configured in the Project-Settings
     */
    class GodotAuthProvider final : public idtx::IEnvironmentProvider
    {
    public:
        bool Resolve(const std::string& name, std::string& out) const override
        {
            if (name != "TOKEN") return false;

            // The environment computation is configured to be time dependnent and thus we revisit this code path every now and then
            // and validate that a token is still valid or requires re-fresh during computation runs.

            std::lock_guard<std::mutex> lock(mutex_);

            // If a cached token exists and is not (about to be) expired, reuse it.
            if (!auth_token_.empty() && !IsTokenExpired())
            {
                out = auth_token_;
                return true;
            }

            // retrieve the properties from the project settings required to request an auth token.
            godot::ProjectSettings* settings = godot::ProjectSettings::get_singleton();
            if (settings == nullptr)
            {
                return false;
            }
            
            auto oidc_wellknown_url = settings->get_setting("idtxflow/authentication/wellknown_url");
            auto oauth_token_url = settings->get_setting("idtxflow/authentication/token_url");
            auto oauth_username = settings->get_setting("idtxflow/authentication/username");
            auto oauth_password = settings->get_setting("idtxflow/authentication/password");
            auto oauth_client_id = settings->get_setting("idtxflow/authentication/client_id");
            
            if (!oidc_wellknown_url || !oauth_token_url || !oauth_username || !oauth_password) return false;
            
            ix::HttpClient http_client_;
            ix::SocketTLSOptions tls_options_;
            
            http_client_.setTLSOptions(tls_options_);
            ix::HttpRequestArgsPtr args = http_client_.createRequest(oauth_token_url.stringify().utf8().get_data());
            args->followRedirects = true;
            args->maxRedirects = 5;
            args->connectTimeout = 30;
            args->transferTimeout = 120;
            args->compress = false;
            args->extraHeaders.insert({"Content-Type", "application/x-www-form-urlencoded"});
            args->extraHeaders.insert({"Accept",       "application/json"});

            // Decide the grant: use the refresh token if we have one, otherwise fall back
            // to the resource-owner password credentials grant.
            ix::HttpResponsePtr response;            
            // When the actual token was empty or expired we come here
            if (!refresh_token_.empty())
            {
                // OAuth2 token endpoints (RFC 6749) require the parameters to be sent as
                // application/x-www-form-urlencoded. These must go in the httpParameters
                // (2nd) argument; the httpFormDataParameters (3rd) argument produces a
                // multipart/form-data body which the server's form parser will not read.
                response = http_client_.post(args->url,
                {
                    {"grant_type",    "refresh_token"},
                    {"refresh_token", refresh_token_},
                    {"client_id",     oauth_client_id.stringify().utf8().get_data()},
                }, {}, args);
            }
            else
            {
                response = http_client_.post(args->url,
                {
                    {"grant_type", "password"},
                    {"username",   oauth_username.stringify().utf8().get_data()},
                    {"password",   oauth_password.stringify().utf8().get_data()},
                    {"client_id",  oauth_client_id.stringify().utf8().get_data()},
                }, {}, args);
            }

            if (!response || response->statusCode < 200 || response->statusCode >= 300 || !ParseTokenResponse(response->body))
            {
                return false;
            }

            out = auth_token_;
            return true;
        }

    private:
        /**
         * @brief Parse the JSON body returned by the OAuth/OIDC token endpoint.
         *
         * A typical response looks like:
         * @code
         * {
         *   "access_token":  "eyJ...",
         *   "expires_in":    3600,
         *   "refresh_token": "eyJ...",
         *   "token_type":    "Bearer",
         *   ...
         * }
         * @endcode
         *
         * Extracts the access token, the (relative) expiry in seconds and the refresh
         * token. Populates auth_token_ (prefixed with the token_type, e.g. "Bearer "),
         * expiry_epoch_seconds_ and refresh_token_ on success.
         *
         * @param body Raw JSON string from the token endpoint.
         * @return true if an access_token was successfully extracted, false otherwise.
         */
        bool ParseTokenResponse(const std::string& body) const
        {
            using namespace godot;

            if (body.empty())
            {
                return false;
            }

            const Variant parsed = JSON::parse_string(String(body.c_str()));
            if (parsed.get_type() != Variant::DICTIONARY)
            {
                // Malformed JSON or not an object at the top level.
                return false;
            }

            const Dictionary dict = parsed;

            // access_token is mandatory.
            if (!dict.has("access_token"))
            {
                return false;
            }
            const String access_token = dict["access_token"];
            if (access_token.is_empty())
            {
                return false;
            }

            // token_type (defaults to "Bearer") is prepended so downstream consumers can
            // use the value directly as an Authorization header.
            String token_type = "Bearer";
            if (dict.has("token_type"))
            {
                const String tt = dict["token_type"];
                if (!tt.is_empty())
                {
                    // Normalize the casing of well-known schemes (servers often send
                    // "bearer" lowercase); otherwise keep the server-provided value.
                    token_type = (tt.to_lower() == "bearer") ? String("Bearer") : tt;
                }
            }

            auth_token_ = std::string((token_type + " " + access_token).utf8().get_data());

            // expires_in is a relative lifetime in seconds. Convert to an absolute epoch
            // so we can cheaply check for expiry later. If absent, treat the token as
            // non-expiring (expiry 0 -> IsTokenExpired() returns false).
            expiry_epoch_seconds_ = 0;
            if (dict.has("expires_in"))
            {
                const Variant expires_in_var = dict["expires_in"];
                int64_t expires_in = 0;
                if (expires_in_var.get_type() == Variant::INT ||
                    expires_in_var.get_type() == Variant::FLOAT)
                {
                    expires_in = static_cast<int64_t>(expires_in_var);
                }
                else if (expires_in_var.get_type() == Variant::STRING)
                {
                    expires_in = static_cast<int64_t>(String(expires_in_var).to_int());
                }

                if (expires_in > 0)
                {
                    const int64_t now =
                        std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
                    expiry_epoch_seconds_ = now + expires_in;
                }
            }

            // refresh_token is optional; keep any previously stored one if not provided.
            if (dict.has("refresh_token"))
            {
                const String refresh = dict["refresh_token"];
                if (!refresh.is_empty())
                {
                    refresh_token_ = std::string(refresh.utf8().get_data());
                }
            }

            return true;
        }

        /**
         * @brief Whether the cached token has expired (or is within the refresh skew).
         *
         * Returns false when no expiry was recorded (treated as non-expiring). Applies a
         * small skew so we refresh slightly before the hard expiry to avoid races.
         */
        bool IsTokenExpired() const
        {
            if (expiry_epoch_seconds_ == 0)
            {
                return false;
            }
            constexpr int64_t kRefreshSkewSeconds = 30;
            const int64_t now =
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
            return now >= (expiry_epoch_seconds_ - kRefreshSkewSeconds);
        }

        // Cache guarded by mutex_. Marked mutable so the const Resolve() override can
        // refresh/populate the cache. Resolve() may be invoked from a background worker
        // thread, hence the locking.
        mutable std::mutex mutex_;
        mutable std::string auth_token_ = "";
        mutable std::string refresh_token_ = "";
        // Absolute expiry as seconds since the Unix epoch; 0 means "never expires".
        mutable int64_t expiry_epoch_seconds_ = 0;
    };
    
    /**
     * @brief Resolves a small set of built-in dynamic variables.
     *
     * Registered under the "var" prefix, so a USD `inputs:key = "var:CURRENT_TIME"` resolves
     * to a dynamically computed value. Unlike the environment / project-setting providers
     * these values are computed fresh on every Resolve() call, so they always reflect the
     * time at which the computation runs.
     *
     * Supported names (initial implementation):
     *   - CURRENT_TIME       -> the current timestamp as UTC (seconds since the Unix epoch)
     *   - CURRENT_TIME_LESS5 -> the current timestamp as UTC minus 5 seconds
     *
     * The timestamps are emitted as ISO-8601 UTC strings (e.g. "2026-03-08T14:29:04Z") so
     * they can be embedded directly into query strings, JSON bodies, etc.
     */
    class GodotVarProvider final : public idtx::IEnvironmentProvider
    {
    public:
        bool Resolve(const std::string& name, std::string& out) const override
        {
            if (name == "CURRENT_TIME")
            {
                out = FormatUtcIso8601(std::chrono::system_clock::now());
                return true;
            }
            if (name == "CURRENT_TIME_LESS5")
            {
                out = FormatUtcIso8601(std::chrono::system_clock::now() - std::chrono::seconds(20));
                return true;
            }
            if (name == "INTERVAL")
            {
                out = "20s";
                return true;
            }
            return false;
        }

    private:
        /**
         * @brief Format a system_clock time point as an ISO-8601 UTC string, e.g.
         *        "2026-03-08T14:29:04Z".
         */
        static std::string FormatUtcIso8601(std::chrono::system_clock::time_point tp)
        {
            const std::time_t t = std::chrono::system_clock::to_time_t(tp);
            std::tm tmUtc{};
#if defined(_WIN32)
            gmtime_s(&tmUtc, &t);
#else
            gmtime_r(&t, &tmUtc);
#endif
            char buffer[32];
            const std::size_t written =
                std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
            return std::string(buffer, written);
        }
    };

    /**
     * @brief Resolves keys against the process environment variables.
     *
     * Registered under the "env" prefix, so a USD `inputs:key = "env:PATH"` resolves the
     * PATH environment variable. `std::getenv` is used for a simple, portable read.
     */
    class GodotEnvVarProvider final : public idtx::IEnvironmentProvider
    {
    public:
        bool Resolve(const std::string& name, std::string& out) const override
        {
            if (name.empty())
            {
                return false;
            }
            // std::getenv is safe for read-only access. We copy immediately into a
            // std::string to avoid any lifetime concerns with the returned pointer.
            if (const char* value = std::getenv(name.c_str()))
            {
                out = value;
                return true;
            }
            
            return false;
        }
    };

    /**
     * @brief Resolves keys against a snapshot of Godot's ProjectSettings.
     *
     * Registered under the "project" prefix, so a USD `inputs:key = "project:physics/gravity"`
     * resolves that project setting. Because ProjectSettings must be accessed from the main
     * thread, this provider captures a snapshot of all defined settings at construction
     * (which the host performs on the main thread during module init) and serves subsequent
     * lookups from that thread-safe snapshot.
     */
    class GodotProjectSettingProvider final : public idtx::IEnvironmentProvider
    {
    public:
        /**
         * Build the snapshot. MUST be called on the Godot main thread (the host does this
         * during initialize_idtxflow_module). Captures every property name known to
         * ProjectSettings and its current value stringified via godot::Variant.
         */
        GodotProjectSettingProvider()
        {
            using namespace godot;

            ProjectSettings* settings = ProjectSettings::get_singleton();
            if (settings == nullptr)
            {
                return;
            }

            const TypedArray<Dictionary> props = settings->get_property_list();
            for (int i = 0; i < props.size(); ++i)
            {
                const Dictionary prop = props[i];
                if (!prop.has("name"))
                {
                    continue;
                }
                const String name = prop["name"];
                const std::string key = name.utf8().get_data();
                if (key.empty())
                {
                    continue;
                }

                const Variant value = settings->get_setting(name);
                // Stringify the variant. For simple scalar/string settings this yields the
                // expected textual value; complex types are serialized to their Godot
                // string representation, which downstream string consumers can still use.
                const String asString = value.stringify();
                snapshot_.emplace(key, std::string(asString.utf8().get_data()));
            }
        }

        bool Resolve(const std::string& key, std::string& out) const override
        {
            const auto it = snapshot_.find(key);
            if (it == snapshot_.end())
            {
                return false;
            }
            out = it->second;
            return true;
        }

    private:
        // Immutable after construction -> no locking needed for reads on the worker thread.
        std::unordered_map<std::string, std::string> snapshot_;
    };

} // namespace exec
} // namespace idtxflow