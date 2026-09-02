#include "UsdSkeletonNode3D.h"

using namespace godot;

void UsdSkeletonNode3D::_ready()
{
    Skeleton3D::_ready();

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

void UsdSkeletonNode3D::_process(double delta)
{
    Skeleton3D::_process(delta);

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

    int tracks = animation_->get_track_count();
    for (int t_idx = 0; t_idx < tracks; ++t_idx)
    {
        // from the track path we can retrieve the bone index we want to animate
        NodePath boneKey = animation_->track_get_path(t_idx);
        int bone_idx = joint_bone_map_.get(boneKey, -1);
        if (bone_idx < 0) continue;

        Animation::TrackType t_type = animation_->track_get_type(t_idx);
        if (t_type == Animation::TYPE_POSITION_3D)
            set_bone_pose_position(bone_idx, animation_->position_track_interpolate(t_idx, current_anim_time_));
        else if (t_type == Animation::TYPE_ROTATION_3D)
            set_bone_pose_rotation(bone_idx, animation_->rotation_track_interpolate(t_idx, current_anim_time_));
        else if(t_type == Animation::TYPE_SCALE_3D)
            set_bone_pose_scale(bone_idx, animation_->scale_track_interpolate(t_idx, current_anim_time_));
    }

    force_update_all_bone_transforms();
}

void UsdSkeletonNode3D::set_animation(const Ref<Animation>& p_animation)
{
    animation_ = p_animation;

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

void UsdSkeletonNode3D::set_loop_animation(bool loop)
{
    loop_animation_ = loop;
    if (loop) current_anim_time_ = 0.0;
}

void UsdSkeletonNode3D::_bind_methods()
{
    // bind methods from the inherited interface here
    IUSDNODE_IMPLEMENT_BINDINGS(UsdSkeletonNode3D)
    
    ClassDB::bind_method(D_METHOD("set_joint_map", "p_map"), &UsdSkeletonNode3D::set_joint_to_bone_map);
    ClassDB::bind_method(D_METHOD("get_joint_map"), &UsdSkeletonNode3D::get_joint_to_bone_map);
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "joint_bone_map",
            PROPERTY_HINT_NONE, "" ,
            PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY ),
        "set_joint_map", "get_joint_map");
    
    ClassDB::bind_method(D_METHOD("set_animation", "p_animation"), &UsdSkeletonNode3D::set_animation);
    ClassDB::bind_method(D_METHOD("get_animation"), &UsdSkeletonNode3D::get_animation);
    ClassDB::bind_method(D_METHOD("set_animations", "p_clips"), &UsdSkeletonNode3D::set_animations);
    ClassDB::bind_method(D_METHOD("get_animations"), &UsdSkeletonNode3D::get_animations);
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "animations",
            PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY),
        "set_animations", "get_animations");
    ADD_PROPERTY(
        PropertyInfo(Variant::OBJECT, "animation",
            PROPERTY_HINT_NONE, "" ,
            PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY ),
        "set_animation", "get_animation");

    ClassDB::bind_method(D_METHOD("set_loop_animation", "p_loop"), &UsdSkeletonNode3D::set_loop_animation);
    ClassDB::bind_method(D_METHOD("get_loop_animation"), &UsdSkeletonNode3D::get_loop_animation);
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "loop_animation",
            PROPERTY_HINT_NONE, "" ,
            PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR ),
        "set_loop_animation", "get_loop_animation");
}
