#pragma once

/**
 * @file BuiltinGeometryPresentation.h
 * @brief Shared Godot mesh and fallback-material rules for built-in USD geometry.
 *
 * Used by UsdGodotStageConverter during initial conversion and by
 * StageAttributeController when refreshing existing nodes, so both apply the
 * same dimension, orientation and colour rules. Does not author USD or send edits.
 */

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/capsule_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/plane_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>

namespace idtxflow::converter::builtin
{
/// Marker identifying converter fallback materials; explicit/user materials are never refreshed.
inline constexpr const char* fallback_meta = "USD_BUILTIN_FALLBACK_MATERIAL";
/// Configure a detached mesh using the same rules for initial conversion and authoritative refresh.
void configure(godot::BoxMesh& mesh, double size);
/// Set both cylinder radii, or a zero top radius for a cone.
void configure(godot::CylinderMesh& mesh, double radius, double height, bool cone);
/// Preserve the converter's minimum radius protection and diameter-based height.
void configure(godot::SphereMesh& mesh, double radius);
/// USD capsule height excludes the two hemispherical caps.
void configure(godot::CapsuleMesh& mesh, double radius, double height);
/// Plane orientation belongs to the resource rather than the node transform.
void configure(godot::PlaneMesh& mesh, double width, double length, const pxr::TfToken& axis);
/// Create the constant display-colour/opacity fallback used when material conversion has no result.
godot::Ref<godot::StandardMaterial3D> fallback_material(const pxr::VtArray<pxr::GfVec4f>& colors,
                                                        const pxr::TfToken& interpolation);
} // namespace idtxflow::converter::builtin
