#include "BuiltinGeometryPresentation.h"
#include <algorithm>
#include <pxr/usd/usdGeom/tokens.h>

namespace idtxflow::converter::builtin
{
void configure(godot::BoxMesh& mesh, double size)
{
    mesh.set_size(godot::Vector3(size, size, size));
}
void configure(godot::CylinderMesh& mesh, double radius, double height, bool cone)
{
    mesh.set_top_radius(cone ? 0.0 : radius);
    mesh.set_bottom_radius(radius);
    mesh.set_height(height);
}
void configure(godot::SphereMesh& mesh, double radius)
{
    mesh.set_radius(std::max(radius, 1e-6));
    mesh.set_height(radius * 2.0);
}
void configure(godot::CapsuleMesh& mesh, double radius, double height)
{
    mesh.set_radius(std::max(radius, 1e-6));
    mesh.set_height(height + radius * 2.0);
}
void configure(godot::PlaneMesh& mesh, double width, double length, const pxr::TfToken& axis)
{
    mesh.set_size(godot::Vector2(width, length));
    mesh.set_orientation(axis == pxr::UsdGeomTokens->x   ? godot::PlaneMesh::FACE_X
                         : axis == pxr::UsdGeomTokens->y ? godot::PlaneMesh::FACE_Y
                                                         : godot::PlaneMesh::FACE_Z);
}
godot::Ref<godot::StandardMaterial3D> fallback_material(const pxr::VtArray<pxr::GfVec4f>& colors,
                                                        const pxr::TfToken& interpolation)
{
    godot::Ref<godot::StandardMaterial3D> material;
    material.instantiate();
    material->set_meta(fallback_meta, true);
    if (!colors.empty() && interpolation == pxr::UsdGeomTokens->constant)
    {
        const auto& c = colors[0];
        material->set_albedo(godot::Color(c[0], c[1], c[2], c[3]));
        material->set_transparency(c[3] < 1.0f ? godot::BaseMaterial3D::TRANSPARENCY_ALPHA
                                               : godot::BaseMaterial3D::TRANSPARENCY_DISABLED);
    }
    else
        material->set_flag(godot::BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
    return material;
}
} // namespace idtxflow::converter::builtin
