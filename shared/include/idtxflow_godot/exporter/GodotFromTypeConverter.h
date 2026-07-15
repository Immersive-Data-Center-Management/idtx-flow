#pragma once

/**
 * @file GodotFromTypeConverter.h
 * @brief Godot specializations of FromTypeConverter (godot:: math types -> USD/pxr math types).
 *
 * This is the only export-side place (besides source/) where godot_cpp headers are included on the
 * "from-engine" leaf conversions. It mirrors UsdGodotTypeConverter.h from the import path and must
 * stay the exact inverse of it (especially fromTransform vs toTransform).
 */

#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <idtxflow/exporter/FromTypeConverter.h>
#include <idtxflow_godot/types/GodotTypes.h>

namespace idtxflow
{
namespace exporter
{
    /**
     * Build a USD row-major GfMatrix4d from a Godot Transform3D. The three basis COLUMNS become the
     * first three matrix rows and the origin becomes the fourth row (USD uses row-vector convention:
     * worldPoint = localPoint * M). This is the inverse of UsdGodotTypeConverter's toTransform;
     * validate with a round-trip if geometry appears mirrored/rotated (see docs/EXPORTER_DESIGN.md §6.5).
     */
    template<>
    inline pxr::GfMatrix4d FromTypeConverter<types::TargetEngineGodot>::fromTransform(
        const godot::Transform3D& transform)
    {
        const godot::Basis& b = transform.basis;
        const godot::Vector3& o = transform.origin;
        const godot::Vector3 c0 = b.get_column(0);
        const godot::Vector3 c1 = b.get_column(1);
        const godot::Vector3 c2 = b.get_column(2);

        pxr::GfMatrix4d m;
        m.SetRow(0, pxr::GfVec4d(c0.x, c0.y, c0.z, 0.0));
        m.SetRow(1, pxr::GfVec4d(c1.x, c1.y, c1.z, 0.0));
        m.SetRow(2, pxr::GfVec4d(c2.x, c2.y, c2.z, 0.0));
        m.SetRow(3, pxr::GfVec4d(o.x, o.y, o.z, 1.0));
        return m;
    }

    template<>
    inline pxr::GfVec3f FromTypeConverter<types::TargetEngineGodot>::fromVector3(const godot::Vector3& vec)
    {
        return pxr::GfVec3f(vec.x, vec.y, vec.z);
    }

    template<>
    inline pxr::GfVec4f FromTypeConverter<types::TargetEngineGodot>::fromColor(const godot::Color& color)
    {
        return pxr::GfVec4f(color.r, color.g, color.b, color.a);
    }
}
}
