#include <vector>

#include <godot_cpp/classes/multi_mesh.hpp>
#include "UsdMultiMeshInstanceNode3D.h"

using namespace godot;

int UsdMultiMeshInstanceNode3D::add_instance(const Transform3D& global_instance_transform) const
{
    Ref<MultiMesh> multi_mesh = get_multimesh();
    std::vector<Transform3D> instance_transforms;
    instance_transforms.resize(multi_mesh->get_instance_count());

    for (int i = 0; i < multi_mesh->get_instance_count(); ++i)
    {
        instance_transforms[i] = multi_mesh->get_instance_transform(i);
    }

    // this call is very likely to re-allocate underlying buffer and thus wipes all previous instance
    // transforms
    multi_mesh->set_instance_count(multi_mesh->get_instance_count() + 1);
    for (size_t i = 0; i < instance_transforms.size(); ++i)
    {
        multi_mesh->set_instance_transform(static_cast<int32_t>(i), instance_transforms[i]);
    }

    // translate global instance transform into local space of the MultiMeshInstance
    Transform3D instance_transform = global_base_transform_.affine_inverse() * global_instance_transform;
    multi_mesh->set_instance_transform(static_cast<int32_t>(instance_transforms.size()), instance_transform);

    return multi_mesh->get_instance_count() - 1;
}

void UsdMultiMeshInstanceNode3D::_bind_methods()
{
    // bind methods from the inherited interface here
    IUSDNODE_IMPLEMENT_BINDINGS(UsdMultiMeshInstanceNode3D)

    ClassDB::bind_method(D_METHOD("set_base_transform", "p_animation"),
                         &UsdMultiMeshInstanceNode3D::set_global_base_transform);
    ClassDB::bind_method(D_METHOD("get_base_transform"), &UsdMultiMeshInstanceNode3D::get_global_base_transform);
    ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "global_base_transform", PROPERTY_HINT_NONE, "",
                              PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY),
                 "set_base_transform", "get_base_transform");
}
