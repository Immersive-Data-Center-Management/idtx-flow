#pragma once

/**
 * @file PrimConverterRegistry.h
 * @brief Engine-agnostic singleton registry for USD prim type converters.
 *
 * Modeled after OpenUSD's TfType registry and MDL SDK's module discovery, this registry maps
 * USD prim type tokens to IPrimConverter instances. Converters are sorted by priority so that
 * third-party plugins can override or extend built-in conversion behavior.
 *
 * The registry is a per-TargetEngine singleton: PrimConverterRegistry<TargetEngineGodot> is
 * independent of a hypothetical PrimConverterRegistry<TargetEngineUnreal>.
 *
 * Thread safety: Registration is expected to happen during module initialization (single-threaded).
 * Lookup during conversion is read-only and safe for concurrent use after initialization.
 */

#include "IPrimConverter.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <pxr/base/tf/token.h>
#include <idtxflow/utils/Logger.h>

namespace idtxflow
{
namespace converter
{
template <typename TargetEngine>
    requires types::ValidTargetEngine<TargetEngine>
class PrimConverterRegistry
{
    IDTX_LOG_CATEGORY("PrimConverterRegistry")
  public:
    using ConverterPtr = std::shared_ptr<IPrimConverter<TargetEngine>>;

    /**
     * Access the singleton instance for this TargetEngine.
     */
    static PrimConverterRegistry& Instance()
    {
        static PrimConverterRegistry instance;
        return instance;
    }

    /**
     * Register a converter. It will be indexed under each token returned by
     * GetSupportedPrimTypes(). Multiple converters for the same token are sorted
     * by descending priority; the highest-priority converter wins on lookup.
     *
     * @param converter Shared pointer to the converter instance.
     */
    void Register(ConverterPtr converter)
    {
        for (const auto& token: converter->GetSupportedPrimTypes())
        {
            auto& entry = converters_[token];
            entry.push_back(converter);
            std::sort(entry.begin(), entry.end(),
                      [](const ConverterPtr& a, const ConverterPtr& b) { return a->GetPriority() > b->GetPriority(); });
            IDTX_LOG(IDTX_INFO, "Registered prim converter '{}' for type '{}' (priority {})",
                     converter->GetConverterName(), token.GetString(), converter->GetPriority());
        }
    }

    /**
     * Unregister all entries associated with a given converter name.
     *
     * @param converter_name The name returned by GetConverterName().
     */
    void Unregister(const std::string& converter_name)
    {
        for (auto& [token, vec]: converters_)
        {
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                     [&](const ConverterPtr& c) { return c->GetConverterName() == converter_name; }),
                      vec.end());
        }
    }

    /**
     * Check whether any converter is registered for the given prim type token.
     */
    bool Has(const pxr::TfToken& prim_type) const
    {
        auto it = converters_.find(prim_type);
        return it != converters_.end() && !it->second.empty();
    }

    /**
     * Get the highest-priority converter for a prim type, or nullptr if none registered.
     */
    IPrimConverter<TargetEngine>* Get(const pxr::TfToken& prim_type) const
    {
        auto it = converters_.find(prim_type);
        if (it != converters_.end() && !it->second.empty()) return it->second.front().get();
        return nullptr;
    }

    /**
     * List all registered prim type tokens (useful for debugging / editor introspection).
     */
    std::vector<pxr::TfToken> GetRegisteredTypes() const
    {
        std::vector<pxr::TfToken> result;
        result.reserve(converters_.size());
        for (const auto& [token, _]: converters_)
            result.push_back(token);
        return result;
    }

    /**
     * Remove all registered converters. Useful for teardown / testing.
     */
    void Clear()
    {
        converters_.clear();
    }

  private:
    PrimConverterRegistry() = default;
    PrimConverterRegistry(const PrimConverterRegistry&) = delete;
    PrimConverterRegistry& operator=(const PrimConverterRegistry&) = delete;

    // Map from prim type token to a priority-sorted list of converters
    std::map<pxr::TfToken, std::vector<ConverterPtr>> converters_;
};

} // namespace converter
} // namespace idtxflow