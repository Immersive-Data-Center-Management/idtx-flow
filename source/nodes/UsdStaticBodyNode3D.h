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
    void set_collision_type_int(int type);
    int get_collision_type_int() const;
    
    void _enter_tree() override;

    void set_transformData(const godot::Transform3D& trans) { transform_data_ = trans; }
    const godot::Transform3D& get_transformData() const { return transform_data_; }
    
    void set_height(const float& height) { height_ = height; }
    float get_height() const { return height_; }
    
    void set_radius(const float& radius) { radius_ = radius; }
    float get_radius() const { return radius_; }
    
    void set_axis(const godot::Vector3& axis) { axis_ = axis; } 
    godot::Vector3 get_axis() const { return axis_; }
    
    void set_collision_type (const godot::PackedStringArray& type);
    const int& get_collision_type() const { return collision_interaction_type; }
    
    void set_collision_shape(const std::string& shape);
    ShapeType get_collision_shape() const { return collision_shape_; }
    
    godot::Ref<godot::Shape3D> create_collision_shape(const ShapeType& shape) const;
    
    void apply_collision_type(const int& type);
    
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

