#include "UsdXFormNode3D.h"

using namespace godot;

void UsdXformNode3D::_get_property_list(godot::List<godot::PropertyInfo>* p_list) const
{
    populate_variant_properties(p_list);
}

void UsdXformNode3D::_bind_methods()
{
    // bind methods from the inherited interface here
    IUSDNODE_IMPLEMENT_BINDINGS(UsdXformNode3D)
    
    // bind methods for this specific node
    ClassDB::bind_method(D_METHOD("set_animation", "p_animation"), &UsdXformNode3D::set_animation);
    ClassDB::bind_method(D_METHOD("get_animation"), &UsdXformNode3D::get_animation);
    ADD_PROPERTY(
        PropertyInfo(Variant::OBJECT, "animation",
            PROPERTY_HINT_NONE, "" ,
            PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY ),
        "set_animation", "get_animation");

    ClassDB::bind_method(D_METHOD("set_loop_animation", "p_loop"), &UsdXformNode3D::set_loop_animation);
    ClassDB::bind_method(D_METHOD("get_loop_animation"), &UsdXformNode3D::get_loop_animation);
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "loop_animation",
            PROPERTY_HINT_NONE, "" ,
            PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_EDITOR ),
        "set_loop_animation", "get_loop_animation");
}
