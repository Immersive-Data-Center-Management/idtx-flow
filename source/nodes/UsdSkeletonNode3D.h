#pragma once

#include <vector>

#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>

#include <idtxflow_godot/nodes/IUsdNode3D.h>

class UsdSkeletonNode3D : public godot::Skeleton3D, public IUsdNode3D
{
    GDCLASS(UsdSkeletonNode3D, Skeleton3D)
    IUSDNODE(UsdSkeletonNode3D, false)
    
public:
    /********************* Godot Lifecycle hooks *****************/
    void _ready() override;
    void _process(double delta) override;

    /**
     * Set the mapping of usd joints to godot bones
     * @param map the map
     */
    void set_joint_to_bone_map(const godot::Dictionary &map) { joint_bone_map_ = map; }

    /**
     * Get the actual mapping of usd joints to godot bones
     * @return 
     */
    const godot::Dictionary& get_joint_to_bone_map() const { return joint_bone_map_; }

    /**
     * Set the bone bind transforms
     * @param transforms Bind transform for each bone
     */
    void set_bone_bind_transforms(const std::vector<godot::Transform3D>& transforms) { bone_bind_transforms_ = transforms; }

    /**
     * Get the bone bind transforms
     * @return 
     */
    const std::vector<godot::Transform3D>& get_bone_bind_transforms() const { return bone_bind_transforms_; }

    /**
     * Set the bone animation data
     * @param animation 
     */
    void set_animation(const godot::Ref<godot::Animation>& animation);

    /**
     * Get th bone animation data
     * @return 
     */
    godot::Ref<godot::Animation> get_animation() const { return animation_; }

    /**
     * Set the flag whether to loop or not to loop the animation playback
     * @param loop 
     */
    void set_loop_animation(bool loop);

    /**
     * Get whether the animation is currently looping or not
     * @return 
     */
    bool get_loop_animation() const { return loop_animation_; }

public:
    void set_animations(const godot::Dictionary& p_clips) { animations_ = p_clips; }
    godot::Dictionary get_animations() const { return animations_; }

protected:
    static void _bind_methods();
    // mapping of usd joint names to godot bone index
    godot::Dictionary joint_bone_map_;
    // list of bind tronsforms for each bone. The index in this list equals the bone index
    std::vector<godot::Transform3D> bone_bind_transforms_;
    // bone animation data
    godot::Ref<godot::Animation> animation_;
    godot::Dictionary animations_;
    double current_anim_time_ = 0.0;
    bool loop_animation_ = true;
};
