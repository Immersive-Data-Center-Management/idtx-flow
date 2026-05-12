#pragma once

#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shape3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>

#include <pxr/base/tf/token.h>

#include <idtxflow_godot/nodes/IUsdNode3D.h>

#include "idtxflow/utils/Logger.h"

namespace godot{
    class CollisionObject3D;
}

class UsdStaticBodyNode3D: public godot::StaticBody3D, public IUsdNode3D
{
    IDTX_LOG_CATEGORY("UsdStaticBodyNode3D")
    GDCLASS(UsdStaticBodyNode3D, StaticBody3D)
    IUSDNODE(UsdStaticBodyNode3D)
    
public:
    
    enum ShapeType {
        SHAPE_CUBE,
        SHAPE_SPHERE,
        SHAPE_CAPSULE,
        SHAPE_CYLINDER
    };
    
    enum InteractionType
    {
        COLLIDE,
        SELECT,
    };
    
    enum CollisionRole {
        ROLE_COLLIDE = 1 << 0,
        ROLE_SELECT  = 1 << 1,
    };

    
    // Wrapper methods
    void set_collision_shape_int(int shape);
    int get_collision_shape_int() const;
    
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
     * Sets the height value used by the collision shape.
     * @param height Height of the shape.
     */
    void set_height(const float& height) { height_ = height; }
    /**
     * Returns the current height value.
     * @return Height of the shape.
     */
    float get_height() const { return height_; }
    
    /**
     * Sets the radius value used by the collision shape.
     * @param radius Radius of the shape.
     */
    void set_radius(const float& radius) { radius_ = radius; }
    /**
     * Returns the current radius value.
     * @return Radius of the shape.
     */
    float get_radius() const { return radius_; }
    
    /**
     * Sets the axis direction for axis-dependent shapes
     * (e.g. capsules or cylinders).
     * @param axis Axis vector to use.
     */
    void set_axis(const godot::Vector3& axis) { axis_ = axis; } 
    /**
     * Returns the currently configured axis.
     * @return Axis vector.
     */
    godot::Vector3 get_axis() const { return axis_; }
    
    /**
     * Sets the collision interaction type using a string array definition.
     * @param type Collision type definition (e.g. collide, select, etc.).
     */
    void set_collision_type (const godot::PackedStringArray& type);
    /**
     * Returns the collision interaction type identifier.
     * @return Collision interaction type.
     */
    const int& get_collision_type() const { return collision_interaction_type; }
    
    /**
     * Sets the collision shape type using a string identifier.
     * @param shape Shape name to assign.
     */
    void set_collision_shape(const std::string& shape);
    /**
     * Returns the currently assigned collision shape type.
     * @return Collision shape enum value.
     */
    ShapeType get_collision_shape() const { return collision_shape_; }
    
    /**
     * Creates and returns a Godot collision shape resource
     * based on the given ShapeType.
     * @param shape Shape type to create.
     * @return Newly created Shape3D resource.
     */
    godot::Ref<godot::Shape3D> create_collision_shape(const ShapeType& shape) const;
        
    /**
     * Sets collision layers and masks according to the collision type.
     * @param type Collision interaction type identifier.
     */
    void apply_collision_type(const int& type);
    
    
    /**
     * Creates a static collision object using the given transform.
     * @param trans World transform for the static collision.
     */
    void create_collision_static(const godot::Transform3D& trans);
    
private:
    
protected:
    static void _bind_methods();
    
    godot::Transform3D transform_data_;
    float height_;
    float radius_;
    godot::Vector3 axis_;
    godot::Color collider_color_;
    ShapeType collision_shape_;
    int collision_interaction_type;
};

