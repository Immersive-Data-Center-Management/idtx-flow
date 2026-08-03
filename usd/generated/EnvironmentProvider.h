#pragma once
/**
 * @file EnvironmentProvider.h
 * @brief Host-agnostic extension point that lets the IDTX USD library resolve an
 *        input key string into a value string without knowing *how* the value is
 *        produced (environment variables, engine project settings, a registry, ...).
 *
 * ------------------------------------------------------------------------------------
 * Architecture
 * ------------------------------------------------------------------------------------
 * The IDTX USD extension is compiled into a shared library (`libidtx_usd.{dll,dylib,so}`)
 * that must remain completely agnostic of any host game engine (e.g. Godot). The
 * `Compute_Environment` USD computation needs to turn its `inputs:key` string into an
 * `outputs:value` string, but the actual lookup depends entirely on the host.
 *
 * This header defines a small contract, mirroring the existing `IExecBridgeHandler`
 * pattern:
 *
 *   1. `IEnvironmentProvider` - a pure abstract interface implemented by the host.
 *   2. `EnvironmentProviderRegistry` - a process-wide singleton, OWNED AND EXPORTED by
 *      the USD library. The host registers its provider implementation(s) here at
 *      startup. The computation queries the registry at compute time.
 *
 * The registry supports KEYED / PREFIXED providers so that distinct domains can be
 * served by distinct implementations. A key of the form "<prefix>:<name>" (e.g.
 * "env:PATH" or "project:physics/gravity") is routed to the provider registered for
 * that prefix. A provider registered with an empty prefix acts as the default/fallback
 * provider for keys that carry no known prefix.
 *
 * ------------------------------------------------------------------------------------
 * DLL boundary notes (esp. Windows)
 * ------------------------------------------------------------------------------------
 *  - Only the registry's methods are exported (`IDTX_API`). `IEnvironmentProvider`
 *    itself has only pure-virtual functions + a virtual destructor; its vtable is
 *    provided by the host, so it needs no exported symbol (same as `IExecBridgeHandler`).
 *  - `std::string` crosses the boundary. This is safe as long as the host DLL and the
 *    USD DLL are built with the same C++ runtime / STL (both use MSVC `/MD` in this
 *    project). This is the same invariant already relied upon by the ExecBridge, which
 *    passes `pxr::VtValue` (containing `std::string`) across the boundary.
 *  - Providers must be thread-safe: the IDTX exec worker thread invokes the computation
 *    (and therefore `Resolve`) off the main thread. Do not throw across the boundary;
 *    return `false` on failure instead.
 *
 * ------------------------------------------------------------------------------------
 * Host usage (see also register_types.cpp)
 * ------------------------------------------------------------------------------------
 * @code
 *   #include <idtx/EnvironmentProvider.h>
 *
 *   class GodotEnvVarProvider : public idtx::IEnvironmentProvider {
 *   public:
 *       bool Resolve(const std::string& name, std::string& out) const override {
 *           if (const char* v = std::getenv(name.c_str())) { out = v; return true; }
 *           return false;
 *       }
 *   };
 *
 *   static GodotEnvVarProvider g_envProvider;
 *
 *   // during module init:
 *   idtx::EnvironmentProviderRegistry::Instance().RegisterProvider("env", &g_envProvider);
 *   // during module de-init (AFTER the exec thread is cancelled):
 *   idtx::EnvironmentProviderRegistry::Instance().UnregisterAll();
 * @endcode
 */

#include <map>
#include <mutex>
#include <string>

#include "./api.h"  // IDTX_API

namespace idtx
{
    /**
     * @class IEnvironmentProvider
     * @brief Host-implemented contract that resolves a key string into a value string.
     *
     * Implemented by the host application (e.g. the Godot GDExtension) and registered
     * with the EnvironmentProviderRegistry so the host-agnostic USD computation can call
     * it across the DLL boundary.
     *
     * Implementations MUST be thread-safe and MUST NOT allow exceptions to escape
     * across the DLL boundary.
     */
    class IEnvironmentProvider
    {
    public:
        virtual ~IEnvironmentProvider() = default;

        /**
         * Resolve @p key to a value.
         *
         * @param key   The lookup key WITHOUT the routing prefix. For example, when the
         *              authored input is "env:PATH", the registry routes to the provider
         *              registered for prefix "env" and passes "PATH" as @p key.
         *              For the default (empty-prefix) provider the full authored string
         *              is passed unchanged.
         * @param out   Receives the resolved value on success. Left untouched on failure.
         * @return true if the key was resolved, false otherwise.
         */
        virtual bool Resolve(const std::string& key, std::string& out) const = 0;
    };

    /**
     * @class EnvironmentProviderRegistry
     * @brief Process-wide, thread-safe registry that owns the mapping from key-prefix to
     *        provider implementation.
     *
     * This registry lives in and is exported by the IDTX USD library. Host applications
     * link against the library and register their provider(s) at startup. The registry
     * does NOT take ownership of the provider pointers - the host is responsible for the
     * lifetime of its provider objects and must unregister (or call UnregisterAll())
     * before those objects are destroyed, and after the exec worker thread has stopped.
     */
    class EnvironmentProviderRegistry
    {
    public:
        /**
         * Singleton accessor. Exactly one instance exists inside the USD library.
         */
        IDTX_API static EnvironmentProviderRegistry& Instance();

        /**
         * Register a provider for a given key prefix.
         *
         * @param prefix   The routing prefix WITHOUT the trailing ':' (e.g. "env",
         *                 "project"). Pass an empty string to register the default
         *                 provider used for keys that carry no recognized prefix.
         * @param provider The host-owned provider implementation. Must outlive its
         *                 registration. Passing nullptr is equivalent to unregistering
         *                 the prefix.
         */
        IDTX_API void RegisterProvider(const std::string& prefix, IEnvironmentProvider* provider);

        /**
         * Remove the provider registered for @p prefix (if any).
         */
        IDTX_API void UnregisterProvider(const std::string& prefix);

        /**
         * Remove all registered providers. Call this during host de-init AFTER the exec
         * worker thread has been cancelled to avoid a dangling provider being used by an
         * in-flight computation.
         */
        IDTX_API void UnregisterAll();

        /**
         * Resolve @p fullKey using the appropriate registered provider.
         *
         * Routing rules:
         *  - If @p fullKey contains a ':' the substring before it is treated as the
         *    prefix and looked up in the registry. On a match, the remainder (after the
         *    ':') is passed to that provider's Resolve().
         *  - If there is no ':' , or the prefix is not registered, the default provider
         *    (registered with an empty prefix) is tried with the full key.
         *  - If nothing resolves, @p fallback is returned.
         *
         * @param fullKey  The authored input key, possibly prefixed (e.g. "env:PATH").
         * @param fallback Returned when no provider resolves the key.
         * @return The resolved value, or @p fallback.
         */
        IDTX_API std::string Resolve(const std::string& fullKey,
                                     const std::string& fallback = std::string()) const;

    private:
        EnvironmentProviderRegistry() = default;
        EnvironmentProviderRegistry(const EnvironmentProviderRegistry&) = delete;
        EnvironmentProviderRegistry& operator=(const EnvironmentProviderRegistry&) = delete;

        // prefix -> provider. The empty-string key holds the default provider.
        std::map<std::string, IEnvironmentProvider*> providers_;
        mutable std::mutex mutex_;
    };

} // namespace idtx