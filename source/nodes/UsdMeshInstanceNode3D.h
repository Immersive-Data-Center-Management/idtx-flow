#pragma once

#include <format>

#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/animation.hpp>

#include <idtxflow/exec/ExecBridgeHandler.h>

#include <idtxflow_godot/nodes/IUsdNode3D.h>

#include "UsdSkeletonNode3D.h"

class UsdMeshInstanceNode3D : public godot::MeshInstance3D, public IUsdNode3D, public IExecBridgeHandler
{
    GDCLASS(UsdMeshInstanceNode3D, MeshInstance3D)
    IUSDNODE(UsdMeshInstanceNode3D)
    
public:
    /******************* Godot lifecycle hooks ***************************/
    void _ready() override;
    void _process(double delta) override;
    
    /******************* ExecBridgeHandler ******************************/
    void OnComputeComplete(const std::vector<ExecComputeResult>& results) override;

    /**
     * Set the skeleton this mesh is bound to
     * @param skeleton 
     */
    void set_skeleton(UsdSkeletonNode3D *skeleton);

    /**
     * Set the animation data
     * @param p_animation 
     */
    void set_animation(const godot::Ref<godot::Animation>& p_animation);

    /**
     * Get the animation data
     * @return 
     */
    godot::Ref<godot::Animation> get_animation() const;

    /**
     * Set the flag whether the animation loops during playback
     * @param loop 
     */
    void set_loop_animation(bool loop);

    /**
     * Get the flag whether the animation is looping
     * @return 
     */
    bool get_loop_animation() const;

protected:
    static void _bind_methods();
    void _notification(int p_what);
    
    // if this node is linked to a skeleton we store the reference here. This is required, as the creation and
    // assignment to the node happens before it is added into the actual scene tree. Thus the godot linkage will
    // not work. We defer this linkage until the "PARENTED" notification is received.
    UsdSkeletonNode3D* skeleton_ = nullptr;
    
    // storing the converted usd animation data of the prim
    godot::Ref<godot::Animation> animation_;
    double current_anim_time_ = 0.0;
    bool loop_animation_ = true;
};
