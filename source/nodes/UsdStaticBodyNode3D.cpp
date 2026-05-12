#include "UsdStaticBodyNode3D.h"

#include "pxr/pxr.h"

#include <unordered_map>
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/sphere_shape3d.hpp>
#include <godot_cpp/classes/cylinder_shape3d.hpp>
#include <godot_cpp/classes/capsule_shape3d.hpp>
#include <godot_cpp/classes/collision_object3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <pxr/base/tf/token.h>

#include "idtxflow/utils/Logger.h"


using namespace godot;

// Mappings
static const std::unordered_map<std::string, UsdStaticBodyNode3D::CollisionRole> kInteractionTypeMap = {
    {"Collide",    UsdStaticBodyNode3D::CollisionRole::ROLE_COLLIDE},
    {"Select",    UsdStaticBodyNode3D::CollisionRole::ROLE_SELECT},
};

static const std::unordered_map<std::string, UsdStaticBodyNode3D::ShapeType> kShapeTypeMap = {
    {"Cube", UsdStaticBodyNode3D::ShapeType::SHAPE_CUBE},
    {"Sphere", UsdStaticBodyNode3D::ShapeType::SHAPE_SPHERE},
    {"Capsule", UsdStaticBodyNode3D::ShapeType::SHAPE_CAPSULE},
    {"Cylinder", UsdStaticBodyNode3D::ShapeType::SHAPE_CYLINDER},
    // At the moment only simple primitives are supported. Mesh colliders are currently out of scope.
};

// Wrapper shape for inspector display
void UsdStaticBodyNode3D::set_collision_shape_int(const int shape) {
    switch (shape) {
        case 0: set_collision_shape("Cube"); break;
        case 1: set_collision_shape("Sphere"); break;
        case 2: set_collision_shape("Capsule"); break;
        case 3: set_collision_shape("Cylinder"); break;
        default: IDTX_LOG(IDTX_WARN, "Could not create collision shape since it looks to be unsupported!");
    }
}

// Wrapper type for inspector display
int UsdStaticBodyNode3D::get_collision_shape_int() const {
    return collision_shape_;
}


void UsdStaticBodyNode3D::_enter_tree()
{
    Node3D::_enter_tree();
    
    set_position(transform_data_.get_origin());
    create_collision_static(transform_data_);
}

void UsdStaticBodyNode3D::set_collision_type(const godot::PackedStringArray& type)
{
    for (const godot::String& entry : type)
    {
        if (auto it = kInteractionTypeMap.find( entry.utf8().get_data() ); it != kInteractionTypeMap.end())
        {
            // Add value 
            collision_interaction_type |= it->second; 
        }
        else
        {
            IDTX_LOG(IDTX_WARN, "Unknown collision interaction type: {}",  entry.utf8().get_data());
        }
    }
}

void UsdStaticBodyNode3D::set_collision_shape(const std::string& shape)
{
    if (auto it = kShapeTypeMap.find(shape); it != kShapeTypeMap.end())
    {
        collision_shape_ = it->second;
    }
    else
    {
        IDTX_LOG(IDTX_WARN, "Unknown collision shape type: {}", shape.c_str());
    }
}

Ref<Shape3D> UsdStaticBodyNode3D::create_collision_shape(const ShapeType& shape) const
{
    Vector3 scale = transform_data_.basis.get_scale_abs();

    switch (shape)
    {
        case SHAPE_CUBE:
        {
            Ref<BoxShape3D> box_shape;
            box_shape.instantiate();
            box_shape->set_size(scale);
            box_shape->set_local_to_scene(true);
            box_shape->set_name(box_shape->get_scene_unique_id());
            return box_shape;
        }

        case SHAPE_SPHERE:
        {
            Ref<SphereShape3D> sphere_shape;
            sphere_shape.instantiate();
            sphere_shape->set_radius(radius_);
            sphere_shape->set_local_to_scene(true);
            sphere_shape->set_name(sphere_shape->get_scene_unique_id());
            return sphere_shape;
        }
            
        case SHAPE_CAPSULE:
        {
            Ref<CapsuleShape3D> capsule_shape;
            capsule_shape.instantiate();
            capsule_shape->set_radius(radius_);
            capsule_shape->set_height(height_ +  (2.0f * radius_)); // OpenUsd defines total capsule height like this
            capsule_shape->set_local_to_scene(true);
            capsule_shape->set_name(capsule_shape->get_scene_unique_id());
            return capsule_shape;
        }
            
        case SHAPE_CYLINDER:
        {
            Ref<CylinderShape3D> cylinder_shape;
            cylinder_shape.instantiate();
            cylinder_shape->set_radius(radius_);
            cylinder_shape->set_height(height_);         
            cylinder_shape->set_local_to_scene(true);
            cylinder_shape->set_name(cylinder_shape->get_scene_unique_id());
            return cylinder_shape;
        }
            
        default:
            return {};
    }
}

void UsdStaticBodyNode3D::apply_collision_type(const int& type)
{
    // clear collision mask
    set_collision_mask(0);

    // is collidable
    if (type & ROLE_COLLIDE)
    {
        // Set collision mask 
        set_collision_mask(ROLE_COLLIDE);
    }
    // is selectable
    if (type & ROLE_SELECT)
    {
        set_ray_pickable(true);
    }
    
    set_collision_layer(collision_interaction_type);
}

void UsdStaticBodyNode3D::create_collision_static(const godot::Transform3D& trans)
{
    set_transform(trans);
    set_scale(
        godot::Vector3(1.0, 1.0, 1.0) // Reset scale!
        ); 
            
    // Collider
    CollisionShape3D* collider = memnew(CollisionShape3D);
    add_child(collider);
    apply_collision_type(collision_interaction_type);

    // Get valid owner even in editor
    if (Node *scene_owner = get_tree()->get_edited_scene_root()) {
        collider->set_owner(scene_owner);
    }
    
    // Set collision shape
    Ref<Shape3D> collider_shape = create_collision_shape(collision_shape_);
    collider->set_shape(collider_shape);
    
    // Resolve major axis orientation
    if (axis_ != Vector3(0.0f,0.0f, 0.0f))
    {
        Transform3D t = get_global_transform();
        Vector3 up = Vector3(0.0f,0.0f,1.0f); // Z up in USD 
        t = t.looking_at(t.origin + axis_, up);

        Basis axis_correction = Basis(
                Vector3(1.0f, 0.0f, 0.0f), 
                Math_PI / 2.0f);
        t.basis = t.basis * axis_correction;
        set_global_transform(t);
    }
}

void UsdStaticBodyNode3D::_bind_methods()
{
    // bind methods from the inherited interface here
    IUSDNODE_IMPLEMENT_BINDINGS(UsdStaticBodyNode3D)
    
    // Bind the transform 
    ClassDB::bind_method(D_METHOD("set_transformData", "trans"), &UsdStaticBodyNode3D::set_transformData);
    ClassDB::bind_method(D_METHOD("get_transformData"), &UsdStaticBodyNode3D::get_transformData);
    ADD_PROPERTY(
        PropertyInfo(Variant::TRANSFORM3D, "transform_data_",
            PROPERTY_HINT_NONE, "" ,
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_DEFAULT ),
        "set_transformData", "get_transformData");
    
    // Bind enums via wrapper 
    ClassDB::bind_method(D_METHOD("_set_collision_shape_int", "shape"), &UsdStaticBodyNode3D::set_collision_shape_int);
    ClassDB::bind_method(D_METHOD("_get_collision_shape_int"), &UsdStaticBodyNode3D::get_collision_shape_int);
    ADD_PROPERTY(
        PropertyInfo(Variant::INT,"collision_shape_",
            PROPERTY_HINT_ENUM,"Box,Sphere,Capsule,Cylinder"),
        "_set_collision_shape_int", "_get_collision_shape_int");
    
    ClassDB::bind_method(D_METHOD("set_collision_type", "type"), &UsdStaticBodyNode3D::set_collision_type);
    ClassDB::bind_method(D_METHOD("get_collision_type"), &UsdStaticBodyNode3D::get_collision_type);
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "collision_interaction_type",
            PROPERTY_HINT_FLAGS, "Collide,Select"),
            "set_collision_type", "get_collision_type");
    
    ClassDB::bind_method(D_METHOD("set_axis", "axis"), &UsdStaticBodyNode3D::set_axis);
    ClassDB::bind_method(D_METHOD("get_axis"), &UsdStaticBodyNode3D::get_axis);
    ADD_PROPERTY(
        PropertyInfo(Variant::VECTOR3, "axis",
            PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_READ_ONLY | PROPERTY_USAGE_DEFAULT ),
            "set_axis", "get_axis");
    
    ClassDB::bind_method(D_METHOD("set_height", "height"), &UsdStaticBodyNode3D::set_height);
    ClassDB::bind_method(D_METHOD("get_height"), &UsdStaticBodyNode3D::get_height);
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "height",
            PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_READ_ONLY| PROPERTY_USAGE_DEFAULT ),
            "set_height", "get_height");
    
    ClassDB::bind_method(D_METHOD("set_radius", "radius"), &UsdStaticBodyNode3D::set_radius);
    ClassDB::bind_method(D_METHOD("get_radius"), &UsdStaticBodyNode3D::get_radius);    
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "radius",
            PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_READ_ONLY | PROPERTY_USAGE_DEFAULT ),
            "set_radius", "get_radius");
}
