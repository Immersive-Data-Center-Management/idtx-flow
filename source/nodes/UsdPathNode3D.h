#pragma once

#include <idtxflow_godot/nodes/IUsdNode3D.h>
#include <godot_cpp/classes/path3d.hpp>
#include <godot_cpp/classes/curve3d.hpp>

class UsdPathNode3D: public godot::Path3D, public IUsdNode3D
{
        GDCLASS(UsdPathNode3D, godot::Path3D)
        IUSDNODE(UsdPathNode3D)
        
public:
        void _enter_tree() override;
        
        /**
        * Sets the world transform data for this object.
        * @param trans World-space transform to assign.
        */
        void set_transformData(const godot::Transform3D& trans) { transform_data_ = trans; }
        /**
         * Returns the current world transform data.
         * @return Reference to the stored Transform3D.
         */
        const godot::Transform3D& get_transformData() const { return transform_data_; }
        
        /**
         * Converts points to a Godot curve. Curves are treated as bezier curves and can be open or closed.
         * @param curve the curve that the data is written to.
         * @param vertexCount amount of curve points (this includes handles).
         * @param points actual point position data.
         * @param closed is the curve closed or open?
         */
        static void convert_points(
                godot::Ref<godot::Curve3D>& curve,
                int& vertexCount,
                godot::PackedVector3Array& points, 
                bool closed);
        
private:
        

protected:
        static void _bind_methods();
        
        godot::Transform3D transform_data_;
};
