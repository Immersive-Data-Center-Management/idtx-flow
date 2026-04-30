#include "UsdMockDatasourceFloatNode3D.h"

#include <format>

#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/sdf/path.h>

#include <idtx/tokens.h>

#include <idtxflow_godot/nodes/UsdStageNode3D.h>

void UsdMockDatasourceFloatNode3D::_process(double delta)
{
    Node3D::_process(delta);
    time_accumulator_ += delta;
    if (time_accumulator_ >= static_cast<double>(refresh_interval_))
    {
        time_accumulator_ = 0.0;
        if (stage_node_)
        {
            // Update input attribute values on the stage (simulated data).
            double v = 10.0 + ((90.0 * rand()) / RAND_MAX);
            std::string data = std::format("{{ \"data\": {{ \"value\": {:.2f} }} }}", v);
            godot::print_verbose(godot::String("Set new data value to: ") + data.c_str());
            if (pxr::UsdAttribute attribute = stage_node_->get_stage()->GetPrimAtPath(pxr::SdfPath(prim_path_.utf8().get_data()))
                .GetAttribute(pxr::IDTXTokens->outputsData))
            {
                if (!attribute.Set(data.c_str()))
                    godot::print_error(std::format("unable to set data input value for json at '{}'", prim_path_.utf8().get_data()).c_str());
                else
                    godot::print_verbose(std::format("set data input value for json at '{}'", prim_path_.utf8().get_data()).c_str());
            }
        }
    }
}

void UsdMockDatasourceFloatNode3D::_enter_tree()
{
    Node3D::_enter_tree();
    //set_process_mode(PROCESS_MODE_ALWAYS);
}

void UsdMockDatasourceFloatNode3D::_bind_methods()
{
    // bind methods from the inherited IUsdNode3D interface
    IUSDNODE_IMPLEMENT_BINDINGS(UsdMockDatasourceFloatNode3D)
}
