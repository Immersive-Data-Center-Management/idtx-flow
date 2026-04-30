#pragma once

#include <godot_cpp/classes/node3d.hpp>

#include <idtxflow_godot/nodes/IUsdNode3D.h>

class UsdMockDatasourceFloatNode3D : public godot::Node3D
    , public IUsdNode3D
{
    GDCLASS(UsdMockDatasourceFloatNode3D, Node3D)
    IUSDNODE(UsdMockDatasourceFloatNode3D)
    
public:
    void _process(double delta) override;
    void _enter_tree() override;
    
protected:
    static void _bind_methods();
    
    public:
    float refresh_interval_ = 5.0f;
    double time_accumulator_ = 0.0;
};

