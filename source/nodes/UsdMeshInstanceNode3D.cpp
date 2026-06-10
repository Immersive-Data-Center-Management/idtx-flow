#include "UsdMeshInstanceNode3D.h"

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>

#include <pxr/usd/usdGeom/tokens.h>

#include "idtxflow_godot//converter/UsdGodotTypeConverter.h"

using namespace godot;

constexpr float MIN_SPHERE_RADIUS = 1e-6f;

void UsdMeshInstanceNode3D::_ready()
{
    MeshInstance3D::_ready();

    // ensure that whenever the transform of this node changes (editor/runtime)
    // the _notify method is invoked
    set_notify_transform(true);

    // if there is an animation attached to this node we ensure, that the _process
    // method is invoked while "ticking" the scene and driving the animation forward
    if (animation_.is_valid())
    {
        set_process(true);
        set_process_mode(ProcessMode::PROCESS_MODE_ALWAYS);
    } else
    {
        set_process(false);
    }
}

void UsdMeshInstanceNode3D::_process(double delta)
{
    MeshInstance3D::_process(delta);

    if (!animation_.is_valid()) return;

    current_anim_time_ += delta;
    if (current_anim_time_ > animation_->get_length())
    {
        if (loop_animation_)
        {
            current_anim_time_ = Math::fmod(current_anim_time_, static_cast<double>(animation_->get_length()));
        } else
        {
            current_anim_time_ = animation_->get_length();
        }
    }

    // get all the tracks of the animation attached to this node and drive the
    // properties from them.
    int tracks = animation_->get_track_count();
    for (int t_idx = 0; t_idx < tracks; ++t_idx)
    {
        if (!animation_->track_is_enabled(t_idx))
            continue;
            
        if (animation_->track_get_key_count(t_idx) == 0)
            continue;

        switch (animation_->track_get_type(t_idx))
        {
        case Animation::TYPE_POSITION_3D:
            set_position(animation_->position_track_interpolate(t_idx, current_anim_time_));
            break;
        case Animation::TYPE_ROTATION_3D:
            set_quaternion(animation_->rotation_track_interpolate(t_idx, current_anim_time_));
            break;
        case Animation::TYPE_SCALE_3D:
            set_scale(animation_->scale_track_interpolate(t_idx, current_anim_time_));
            break;
        default:
            break;
        }                
    }
}

void UsdMeshInstanceNode3D::OnComputeComplete(const std::vector<ExecComputeResult>& results)
{
    for (const auto& result : results)
    {
        if (result.primAttribute == pxr::UsdGeomTokens->size.GetString() &&
            result.value.IsHolding<double>() &&
            get_mesh()->is_class("BoxMesh"))
        {
            float size = static_cast<float>(result.value.Get<double>());
            Ref<BoxMesh> box_mesh(Object::cast_to<BoxMesh>(get_mesh().ptr()));
            // this method is invoked on a worker thread, thus we delegate the call to the game thread
            // using call_deferred
            box_mesh->call_deferred("set_size", godot::Vector3(size, size, size));
            continue;
        }
        
        if (result.primAttribute == pxr::UsdGeomTokens->radius.GetString() &&
            result.value.IsHolding<double>())
        {
            float radius = static_cast<float>(result.value.Get<double>());
            if (get_mesh()->is_class("SphereMesh"))
            {
                Ref<SphereMesh> sphere_mesh(Object::cast_to<SphereMesh>(get_mesh().ptr()));
                // ensure that the sphere radius never get 0.0 as this would cause the following error downstream
                // servers/rendering/renderer_scene_cull.cpp:991 - Condition "!v.is_finite()" is true.
                sphere_mesh->call_deferred("set_radius", std::max(radius, MIN_SPHERE_RADIUS));
                sphere_mesh->call_deferred("set_height", radius * 2.0);
            }
            continue;
        }
    }
}

void UsdMeshInstanceNode3D::set_skeleton(UsdSkeletonNode3D* skeleton)
{
    skeleton_ = skeleton;
}

void UsdMeshInstanceNode3D::set_animation(const Ref<Animation>& p_animation)
{
    animation_ = p_animation;
}

Ref<Animation> UsdMeshInstanceNode3D::get_animation() const
{
    return animation_;
}

void UsdMeshInstanceNode3D::set_loop_animation(bool loop)
{
    loop_animation_ = loop;
    if (loop) current_anim_time_ = 0.0;
}

bool UsdMeshInstanceNode3D::get_loop_animation() const
{
    return loop_animation_;
}

void UsdMeshInstanceNode3D::_bind_methods()
{
    // bind methods from the inherited interface here
    IUSDNODE_IMPLEMENT_BINDINGS(UsdMeshInstanceNode3D)
    
    ClassDB::bind_method(D_METHOD("set_animation", "p_animation"), &UsdMeshInstanceNode3D::set_animation);
    ClassDB::bind_method(D_METHOD("get_animation"), &UsdMeshInstanceNode3D::get_animation);
    ADD_PROPERTY(
        PropertyInfo(Variant::OBJECT, "animation",
            PROPERTY_HINT_NONE, "" ,
            PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY ),
        "set_animation", "get_animation");

    ClassDB::bind_method(D_METHOD("set_loop_animation", "p_loop"), &UsdMeshInstanceNode3D::set_loop_animation);
    ClassDB::bind_method(D_METHOD("get_loop_animation"), &UsdMeshInstanceNode3D::get_loop_animation);
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "loop_animation",
            PROPERTY_HINT_NONE, "" ,
            PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR ),
        "set_loop_animation", "get_loop_animation");
}

void UsdMeshInstanceNode3D::_notification(int p_what)
{
    if (p_what == NOTIFICATION_PARENTED && skeleton_)
    {
        set_skeleton_path(get_path_to(skeleton_));
    }
}
