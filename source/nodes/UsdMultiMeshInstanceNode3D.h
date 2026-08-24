#pragma once

#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>

#include <idtxflow_godot/nodes/IUsdNode3D.h>

class UsdMultiMeshInstanceNode3D : public godot::MultiMeshInstance3D, public IUsdNode3D
{
    GDCLASS(UsdMultiMeshInstanceNode3D, MultiMeshInstance3D)
    IUSDNODE(UsdMultiMeshInstanceNode3D, false)

public:
    /**
     * Add a new mesh instance into the multimesh array. Use this method to keep existing previous
     * instance transforms intact
     * @param global_instance_transform 
     * @return index of the added instance
     */
    int add_instance(const godot::Transform3D& global_instance_transform) const;

    /**
     * Set the global base transform of the multimesh instance
     * @param transform 
     */
    void set_global_base_transform(const godot::Transform3D& transform) { global_base_transform_ = transform; }

    /**
     * Get the global base transform of the mutlimesh instance
     * @return 
     */
    const godot::Transform3D& get_global_base_transform() const { return global_base_transform_; }

protected:
    static void _bind_methods();
    
    // to allow calculation of local transforms for added instances we store the global transform of the first
    // and initial instance here as any other should be relative it.
    godot::Transform3D global_base_transform_;
};
