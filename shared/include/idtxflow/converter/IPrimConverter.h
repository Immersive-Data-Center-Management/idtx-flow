#pragma once

/**
 * @file IPrimConverter.h
 * @brief Engine-agnostic interface for USD prim type converters.
 *
 * Inspired by OpenUSD's plugInfo.json / TfType registry pattern, this interface allows third-party
 * developers to register converters for custom USD prim types without modifying the core IDTXFlow
 * library. Implementations are parameterized by TargetEngine, allowing the same plugin architecture
 * to work across Godot, Unreal, Unity, or any other engine that specializes TargetEngineTypes.
 *
 * Usage:
 *   1. Subclass IPrimConverter<YourTargetEngine>
 *   2. Implement GetSupportedPrimTypes(), GetConverterName(), and Convert()
 *   3. Register your converter via PrimConverterRegistry::Register()
 *   4. Ship alongside a primConverterInfo.json for automatic discovery (optional)
 */

#include <pxr/usd/usd/prim.h>
#include <pxr/base/tf/token.h>

#include "../types/TargetTypes.h"
#include "AnimationConverter.h"

#include <string>
#include <vector>
#include <optional>

namespace idtxflow
{
namespace converter
{
    /**
     * Engine-agnostic interface for converting a specific USD prim type into a target engine entity.
     *
     * This follows the same pattern as OpenUSD's schema-based type system: each prim type name
     * (TfToken) maps to a converter that knows how to read USD attributes and produce the
     * corresponding engine-native representation.
     *
     * @tparam TargetEngine A type satisfying ValidTargetEngine, used to select engine-specific types.
     */
    template<typename TargetEngine>
        requires types::ValidTargetEngine<TargetEngine>
    class IPrimConverter
    {
    public:
        using Types = types::TargetEngineTypes<TargetEngine>;
        using ConvertedEntity = typename Types::ConvertedEntity;
        using ConvertedEntityHandle = typename Types::ConvertedEntityHandle;

        virtual ~IPrimConverter() = default;

        /**
         * The USD prim type name(s) this converter handles.
         * Examples: {"Camera"}, {"UsdGeomCamera"}, {"MyCustomPrim", "MyCustomPrimV2"}
         *
         * A converter may handle multiple type tokens. If two converters register for the
         * same token, the one with higher priority wins.
         *
         * @return Vector of TfTokens representing supported prim type names.
         */
        virtual std::vector<pxr::TfToken> GetSupportedPrimTypes() const = 0;

        /**
         * Human-readable name for logging and debugging.
         * Also used as the unique key for Unregister().
         *
         * @return A descriptive name, e.g. "UsdGeomCamera Converter"
         */
        virtual std::string GetConverterName() const = 0;

        /**
         * Priority for this converter. Higher values are checked first.
         * Built-in converters use priority 0. Third-party extensions should use 100+.
         * This allows extensions to override built-in behavior if desired.
         *
         * @return Priority value (default: 100)
         */
        virtual int GetPriority() const { return 100; }

        /**
         * Convert a USD prim into the target engine's entity type.
         *
         * The converter receives the raw UsdPrim, a pre-computed transform in engine-native format,
         * and optional animation data. It is responsible for reading any additional USD attributes
         * it needs from the prim.
         *
         * @param prim The USD prim to convert         
         * @return A stable handle to the newly allocated engine entity, or an
         *         empty handle if conversion failed. The converter must create
         *         the handle in the same module as the entity wrapper. For
         *         any TargetEngine, use Types::GetConvertedEntityHandle(node) and never
         *         return a raw engine class pointer across a DLL boundary.
         *         Ownership is transferred to the caller (StageConverter).
         */
        virtual ConvertedEntityHandle Convert(const pxr::UsdPrim& prim) = 0;

        /**
         * Optional post-processing hook called after parent-child relationships are established.
         *
         * Override this to perform setup that depends on the converted entity's position in the
         * scene hierarchy (e.g., setting up constraints, LOD groups, etc.).
         *
         * @param prim The original USD prim
         * Handles are used here as well because the converted entity and its
         * parent may have been created by different shared libraries. Resolve
         * them with Types::ResolveConvertedEntity() before using them.
         *
         * @param converted Handle to the entity produced by Convert()
         * @param parent Handle to the converted parent entity (empty for roots)
         * @return The converted handle as is or an adjusted entity handle
         */
        virtual ConvertedEntityHandle PostProcess(
            const pxr::UsdPrim& prim,
            ConvertedEntityHandle converted,
            ConvertedEntityHandle parent
        ) {
            // Default: no-op. Override in subclass if needed.
            return converted;
        }
    };

} // namespace converter
} // namespace idtxflow
