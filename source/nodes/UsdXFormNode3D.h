#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/animation.hpp>

#include <idtxflow_godot/nodes/UsdStageNode3D.h>

#include <idtxflow_godot/nodes/IUsdNode3D.h>

class UsdXformNode3D : public godot::Node3D, public IUsdNode3D
{
    GDCLASS(UsdXformNode3D, Node3D)
    IUSDNODE(UsdXformNode3D)
    
public:
    /**
     * Set the animation data to this node
     * @param animation The animation data
     */
    void set_animation(const godot::Ref<godot::Animation>& animation) { animation_ = animation; }

    /**
     * Get the animation data of this node
     * @return 
     */
    godot::Ref<godot::Animation> get_animation() const { return animation_; }

    /**
     * Set whether the animation shall loop and play for ever or just once
     * @param loop 
     */
    void set_loop_animation(bool loop) { loop_animation_ = loop; }

    /**
     * Get whether the animation is looping and played for ever or just once
     * @return 
     */
    bool get_loop_animation() const { return loop_animation_; }
    void _get_property_list(godot::List<godot::PropertyInfo>* p_list) const;
    
protected:
    static void _bind_methods();
    
    // animating the transforms of a converted xform node happens in the _process() hook
    // based on the stored animation data in this property
    godot::Ref<godot::Animation> animation_ = nullptr;
    double current_anim_time_ = 0.0;
    bool loop_animation_ = true;
};
