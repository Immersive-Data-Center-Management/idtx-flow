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

#include <idtx/EnvironmentProvider.h>

namespace idtxflow
{
namespace exec
{
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