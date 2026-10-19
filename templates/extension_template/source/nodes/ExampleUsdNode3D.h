#pragma once

#include <godot_cpp/classes/node3d.hpp>

#include <idtxflow_godot/nodes/IUsdNode3D.h>

/**
 * Minimal custom node returned by ExamplePrimConverter.
 *
 * IDTXFlow's prim post-processing requires converted nodes to expose
 * IUsdNode3D. The false flag means this example does not implement
 * IExecBridgeHandler.
 */
class ExampleUsdNode3D : public godot::Node3D, public IUsdNode3D
{
    GDCLASS(ExampleUsdNode3D, Node3D)
    IUSDNODE(ExampleUsdNode3D, false)

protected:
    static void _bind_methods()
    {
        IUSDNODE_IMPLEMENT_BINDINGS(ExampleUsdNode3D)
    }
};
