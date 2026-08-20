#include "UsdMockDatasourceFloatNode3D.h"

#include <format>

#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/sdf/path.h>

#include <idtx/tokens.h>

#include <idtxflow_godot/nodes/UsdStageNode3D.h>
#include <idtxflow/exec/ExecBridgeManager.h>

void UsdMockDatasourceFloatNode3D::_process(double delta)
{
    Node3D::_process(delta);
    time_accumulator_ += delta;
    if (time_accumulator_ < static_cast<double>(refresh_interval_)) return;
    time_accumulator_ = 0.0;
    if (!stage_node_) return;
    auto bridge = idtxflow::exec::ExecBridgeManager::Instance().FindExecBridgeForStage(stage_node_->get_stage());
    if (!bridge) return;
    const double value = 10.0 + ((90.0 * rand()) / RAND_MAX);
    std::string data = std::format("{{ \"data\": {{ \"value\": {:.2f} }} }}", value);
    idtxflow::exec::StageAttributeUpdate update{pxr::SdfPath(prim_path_.utf8().get_data()), pxr::IDTXTokens->outputsData, pxr::VtValue(std::move(data)), 0, "mock"};
    if (!bridge->EnqueueAttributeUpdate(std::move(update))) godot::print_verbose("Mock datasource update rejected");
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
