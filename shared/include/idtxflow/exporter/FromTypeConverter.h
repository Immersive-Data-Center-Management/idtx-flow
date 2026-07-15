#pragma once

/**
 * @file FromTypeConverter.h
 * @brief Inverse leaf conversions: engine types -> USD/pxr math types.
 *
 * This is the mirror image of UsdTypeConverter (TypeConverter.h), which converts USD types into
 * engine types on import. The methods here are only DECLARED for the generic TargetEngine; each
 * engine provides explicit template specializations (for Godot: GodotFromTypeConverter.h).
 *
 * Keep this header engine-agnostic: never include godot_cpp/* here.
 */

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>

#include "../types/TargetTypes.h"

namespace idtxflow
{
namespace exporter
{
    template<typename TargetEngine> requires idtxflow::types::ValidTargetEngine<TargetEngine>
    class FromTypeConverter
    {
    public:
        using Types = idtxflow::types::TargetEngineTypes<TargetEngine>;

        /**
         * Convert an engine transform into a USD row-major GfMatrix4d. Must be the exact inverse of
         * UsdTypeConverter::toTransform for the same engine, otherwise round-trips break. The basis /
         * row convention (columns vs rows) is the most common source of bugs — validate by round-trip.
         */
        inline static pxr::GfMatrix4d fromTransform(const typename Types::Transform& transform);

        /** Convert an engine 3-component vector into a USD GfVec3f. */
        inline static pxr::GfVec3f fromVector3(const typename Types::Vector3& vec);

        /** Convert an engine colour into a USD GfVec4f (r, g, b, a). */
        inline static pxr::GfVec4f fromColor(const typename Types::Color& color);
    };
}
}
