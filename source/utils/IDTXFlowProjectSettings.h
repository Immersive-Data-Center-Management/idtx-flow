#pragma once
/**
 * @file IDTXFlowProjectSettings.h
 * @brief Registration of IDTXFlow add-on specific settings into Godot's ProjectSettings.
 *
 * This header declares a small helper that registers the OIDC / OAuth related settings
 * used by the IDTXFlow add-on into Godot's editor Project Settings dialog. The settings
 * are grouped under a common "idtxflow/" prefix so they appear in a dedicated section of
 * the Project Settings window.
 *
 * Registered settings
 * --------------------
 *   idtxflow/oidc/wellknown_url      (String)  -> OIDC_WELLKNOWN_URL
 *   idtxflow/oidc/audiences          (String)  -> OIDC_AUDIENCES
 *   idtxflow/oauth/token_url         (String)  -> OAUTH_TOKEN_URL
 *   idtxflow/oauth/username          (String)  -> OAUTH_USERNAME
 *   idtxflow/oauth/password          (String)  -> OAUTH_PASSWORD        (NOT serialized)
 *   idtxflow/oauth/client_id         (String)  -> OAUTH_CLIENT_ID
 *   idtxflow/oauth/client_secret     (String)  -> OAUTH_CLIENT_SECRET   (NOT serialized)
 *
 * Secret handling
 * ---------------
 * The password and the client secret are sensitive values that must never be written to
 * the on-disk project file (project.godot). Godot's ProjectSettings supports flagging a
 * setting as "internal" via set_as_internal(): internal settings are still usable at
 * runtime/edit-time but are excluded from serialization to the project file. We use this
 * mechanism for OAUTH_PASSWORD and OAUTH_CLIENT_SECRET.
 *
 * Threading
 * ---------
 * Registration touches the ProjectSettings singleton and therefore MUST run on the Godot
 * main thread. The add-on performs this during module initialization
 * (initialize_idtxflow_module) which executes on the main thread.
 */

#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <idtxflow/utils/Logger.h>

namespace idtxflow
{
namespace utils
{
    /**
     * @brief Registers all IDTXFlow add-on settings with Godot's ProjectSettings.
     *
     * Idempotent: if a setting already exists (e.g. loaded from project.godot on a
     * subsequent launch) its stored value is preserved; only the default, property info
     * and (for secrets) the internal flag are (re)applied.
     *
     * MUST be called on the Godot main thread.
     */
    class IDTXFlowProjectSettings
    {
    public:
        // Fully-qualified ProjectSettings paths for each add-on setting. Grouping under the
        // "idtxflow/" prefix keeps them together in the editor's Project Settings dialog.
        static constexpr const char* OIDC_WELLKNOWN_URL  = "idtxflow/authentication/wellknown_url";
        static constexpr const char* OIDC_AUDIENCES      = "idtxflow/authentication/audiences";
        static constexpr const char* OAUTH_TOKEN_URL     = "idtxflow/authentication/token_url";
        static constexpr const char* OAUTH_USERNAME      = "idtxflow/authentication/username";
        static constexpr const char* OAUTH_PASSWORD      = "idtxflow/authentication/password";
        static constexpr const char* OAUTH_CLIENT_ID     = "idtxflow/authentication/client_id";
        static constexpr const char* OAUTH_CLIENT_SECRET = "idtxflow/authentication/client_secret";

        /**
         * Register every add-on setting. Safe to call once during module init.
         */
        static void Register()
        {
            using namespace godot;

            ProjectSettings* settings = ProjectSettings::get_singleton();
            if (settings == nullptr)
            {
                IDTX_LOGF(IDTX_WARN, "ProjectSettings singleton unavailable; add-on settings not registered");
                return;
            }

            // Non-secret string settings: plain text fields in the editor.
            register_string(settings, OIDC_WELLKNOWN_URL, /*secret=*/false);
            register_string(settings, OIDC_AUDIENCES,     /*secret=*/false);
            register_string(settings, OAUTH_TOKEN_URL,    /*secret=*/false);
            register_string(settings, OAUTH_USERNAME,     /*secret=*/false);
            register_string(settings, OAUTH_CLIENT_ID,    /*secret=*/false);

            // Secret string settings: rendered as password fields AND excluded from
            // serialization to project.godot via set_as_internal().
            register_string(settings, OAUTH_PASSWORD,      /*secret=*/true);
            register_string(settings, OAUTH_CLIENT_SECRET, /*secret=*/true);

            IDTX_LOGF(IDTX_INFO, "IDTXFlow project settings registered");
        }

    private:
        /**
         * Register a single String setting.
         *
         * @param settings  the ProjectSettings singleton (non-null).
         * @param path      fully-qualified setting path (e.g. "idtxflow/oauth/token_url").
         * @param secret    when true the value is treated as sensitive: shown as a password
         *                  field in the editor and marked internal so it is never written
         *                  to the project file.
         */
        static void register_string(godot::ProjectSettings* settings, const char* path, bool secret)
        {
            using namespace godot;

            const String setting_path(path);

            // Preserve any value already loaded from project.godot; otherwise seed an empty
            // default so the setting exists and shows up in the editor.
            if (!settings->has_setting(setting_path))
            {
                settings->set_setting(setting_path, String(""));
            }

            // Establish the default value used for the "revert to default" feature and to
            // decide whether a value differs from its default when serializing.
            settings->set_initial_value(setting_path, String(""));

            // Describe the property so the editor renders an appropriate control. Secrets
            // use PROPERTY_HINT_PASSWORD so their characters are masked in the UI.
            Dictionary info;
            info["name"] = setting_path;
            info["type"] = Variant::STRING;
            info["hint"] = secret ? PROPERTY_HINT_PASSWORD : PROPERTY_HINT_NONE;
            info["hint_string"] = String("");
            settings->add_property_info(info);

            if (secret)
            {
                // Internal settings are fully functional at runtime/edit-time but are
                // explicitly excluded from being saved into project.godot. This guarantees
                // the password / client secret are never serialized to disk.
                //settings->set_as_internal(setting_path, true);
            }
        }
    };

} // namespace utils
} // namespace idtxflow