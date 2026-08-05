/**
 * @file EnvironmentProviderRegistry.cpp
 * @brief Implementation of the process-wide EnvironmentProviderRegistry.
 *
 * This translation unit is compiled INTO the IDTX USD shared library
 * (`libidtx_usd`). It defines the single, exported singleton instance and the
 * prefix-based routing logic used by the `Compute_Environment` computation to
 * resolve an input key string into a value string via a host-registered provider.
 *
 * The registry is host-agnostic: it only knows about the abstract
 * `IEnvironmentProvider` interface. The host application (e.g. the Godot
 * GDExtension) implements that interface and registers instances here.
 */

#include "./EnvironmentProvider.h"

namespace idtx
{
    EnvironmentProviderRegistry& EnvironmentProviderRegistry::Instance()
    {
        // Meyers singleton - guaranteed single instance within this DLL.
        static EnvironmentProviderRegistry instance;
        return instance;
    }

    void EnvironmentProviderRegistry::RegisterProvider(const std::string& prefix,
                                                       IEnvironmentProvider* provider)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (provider == nullptr)
        {
            providers_.erase(prefix);
        }
        else
        {
            providers_[prefix] = provider;
        }
    }

    void EnvironmentProviderRegistry::UnregisterProvider(const std::string& prefix)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        providers_.erase(prefix);
    }

    void EnvironmentProviderRegistry::UnregisterAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        providers_.clear();
    }

    std::string EnvironmentProviderRegistry::Resolve(const std::string& fullKey,
                                                     const std::string& fallback) const
    {
        // Take a snapshot of the two candidate providers under the lock, then release the
        // lock before invoking Resolve() on them. This keeps the (potentially slow) host
        // callback out of the critical section and avoids holding the registry lock across
        // a call into host code.
        IEnvironmentProvider* prefixed = nullptr; // provider matching an explicit prefix
        IEnvironmentProvider* fallbackProvider = nullptr; // default (empty-prefix) provider
        std::string routedKey = fullKey; // key passed to the prefixed provider

        {
            std::lock_guard<std::mutex> lock(mutex_);

            // default provider (registered under the empty prefix), if any
            if (const auto defIt = providers_.find(std::string()); defIt != providers_.end())
            {
                fallbackProvider = defIt->second;
            }

            // explicit "<prefix>:<name>" routing
            if (const std::string::size_type sep = fullKey.find(':');
                sep != std::string::npos)
            {
                const std::string prefix = fullKey.substr(0, sep);
                if (const auto it = providers_.find(prefix); it != providers_.end())
                {
                    prefixed = it->second;
                    routedKey = fullKey.substr(sep + 1);
                }
            }
        }

        std::string out;

        // 1) try the prefixed provider with the de-prefixed key
        if (prefixed != nullptr && prefixed->Resolve(routedKey, out))
        {
            return out;
        }

        // 2) fall back to the default provider with the full (unmodified) key
        if (fallbackProvider != nullptr && fallbackProvider->Resolve(fullKey, out))
        {
            return out;
        }

        // 3) nothing resolved
        return fallback;
    }

} // namespace idtx