#include "StageAttributeController.h"
#include "converter/BuiltinGeometryPresentation.h"
#include "nodes/UsdMeshInstanceNode3D.h"
#include "nodes/UsdStaticBodyNode3D.h"
#include <godot_cpp/classes/primitive_mesh.hpp>
#include <godot_cpp/classes/capsule_shape3d.hpp>
#include <godot_cpp/classes/cylinder_shape3d.hpp>
#include <godot_cpp/classes/sphere_shape3d.hpp>
#include <godot_cpp/core/object.hpp>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usd/attribute.h>

namespace idtxflow::collab
{
namespace
{
double number(const pxr::UsdPrim& prim, const char* name, double fallback)
{
    double value = fallback;
    prim.GetAttribute(pxr::TfToken(name)).Get(&value);
    return value;
}
bool contains_any(const std::set<std::string>& names, std::initializer_list<const char*> candidates)
{
    for (auto name: candidates)
        if (names.count(name)) return true;
    return false;
}
} // namespace
void StageAttributeController::refresh(const pxr::UsdPrim& prim, godot::Node3D* node,
                                       const std::set<std::string>& names)
{
    if (!prim || !node) return;
    const bool dimensions = contains_any(names, {"size", "radius", "height", "width", "length", "axis"});
    const bool colors = contains_any(names, {"primvars:displayColor", "primvars:displayOpacity"});
    if (!dimensions && !colors) return;
    if (auto* body = godot::Object::cast_to<UsdStaticBodyNode3D>(node))
    {
        // IDTXCollisionAPI creates a collision-only representation. Preserve its child and masks.
        // Box extents are derived from placement by that converter, not from the Cube size attribute.
        if (!contains_any(names, {"radius", "height"})) return;
        const auto radius = number(prim, "radius", body->get_radius());
        const auto height = number(prim, "height", body->get_height());
        body->set_radius(radius);
        body->set_height(height);
        for (int i = 0; i < body->get_child_count(); ++i)
        {
            auto* collider = godot::Object::cast_to<godot::CollisionShape3D>(body->get_child(i));
            if (!collider || collider->get_shape().is_null()) continue;
            godot::Ref<godot::Shape3D> shape = collider->get_shape()->duplicate();
            if (auto* sphere = godot::Object::cast_to<godot::SphereShape3D>(shape.ptr()))
                sphere->set_radius(radius);
            else if (auto* capsule = godot::Object::cast_to<godot::CapsuleShape3D>(shape.ptr()))
            {
                capsule->set_radius(radius);
                capsule->set_height(height + 2.0 * radius);
            }
            else if (auto* cylinder = godot::Object::cast_to<godot::CylinderShape3D>(shape.ptr()))
            {
                cylinder->set_radius(radius);
                cylinder->set_height(height);
            }
            else
                continue;
            collider->set_shape(shape);
            return;
        }
        return;
    }
    auto* visual = godot::Object::cast_to<UsdMeshInstanceNode3D>(node);
    if (!visual) return;
    godot::Ref<godot::PrimitiveMesh> current = visual->get_mesh();
    if (current.is_null()) return;
    // Never mutate a resource that may be shared by another prim or a PackedScene cache.
    godot::Ref<godot::PrimitiveMesh> mesh = current->duplicate();
    if (mesh.is_null()) return;
    namespace presentation = converter::builtin;
    const auto type = prim.GetTypeName().GetString();
    const auto radius = number(prim, "radius", type == "Sphere" || type == "Capsule" ? 0.5 : 1.0);
    const auto height = number(prim, "height", 1.0);
    pxr::TfToken axis = pxr::UsdGeomTokens->z;
    prim.GetAttribute(pxr::UsdGeomTokens->axis).Get(&axis);
    if (dimensions)
    {
        if (auto* box = godot::Object::cast_to<godot::BoxMesh>(mesh.ptr()); box && type == "Cube")
            presentation::configure(*box, number(prim, "size", 1.0));
        else if (auto* sphere = godot::Object::cast_to<godot::SphereMesh>(mesh.ptr()); sphere && type == "Sphere")
            presentation::configure(*sphere, radius);
        else if (auto* cylinder = godot::Object::cast_to<godot::CylinderMesh>(mesh.ptr());
                 cylinder && (type == "Cylinder" || type == "Cone"))
            presentation::configure(*cylinder, radius, height, type == "Cone");
        else if (auto* capsule = godot::Object::cast_to<godot::CapsuleMesh>(mesh.ptr()); capsule && type == "Capsule")
            presentation::configure(*capsule, radius, height);
        else if (auto* plane = godot::Object::cast_to<godot::PlaneMesh>(mesh.ptr()); plane && type == "Plane")
            presentation::configure(*plane, number(prim, "width", 1.0), number(prim, "length", 1.0), axis);
    }
    auto material = mesh->get_material();
    if (colors && material.is_valid() && material->has_meta(presentation::fallback_meta))
    {
        const pxr::UsdGeomGprim gprim(prim);
        pxr::VtArray<pxr::GfVec3f> rgb;
        pxr::VtArray<float> alpha;
        gprim.GetDisplayColorAttr().Get(&rgb);
        gprim.GetDisplayOpacityAttr().Get(&alpha);
        pxr::VtArray<pxr::GfVec4f> rgba;
        if (!rgb.empty() || !alpha.empty())
        {
            const auto c = rgb.empty() ? pxr::GfVec3f(1.0f) : rgb[0];
            rgba.push_back(pxr::GfVec4f(c[0], c[1], c[2], alpha.empty() ? 1.0f : alpha[0]));
        }
        mesh->set_material(presentation::fallback_material(rgba, gprim.GetDisplayColorPrimvar().GetInterpolation()));
    }
    // Last operation: observers of the assignment can free the node or its stage.
    visual->set_mesh(mesh);
}
} // namespace idtxflow::collab
