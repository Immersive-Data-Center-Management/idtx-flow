/**
 * @file StageHandle.h
 * @brief 
 * 
 **/
#pragma once

#include <pxr/usd/usd/stage.h>

#include <idtxflow/exec/ExecBridgeManager.h>

namespace idtxflow::converter
{
    /**
     * The StageHandle class is a container class storing the reference to a UsdStage that has been converted. It will
     * be handed out as part of the convertion result and allows to provide logic that is bound to the live time of the
     * convertion results. As soon as the handle is created it will register the stages ExecBridge to run computations
     * and will cleanup/destroy the ExecBridge registration as soon it is destroyed
     * 
     * The handle is move-only. Ownership of the cleanup code of the handle can be transferred but never duplicated. 
     */
    class StageHandle
    {
    public:
        StageHandle() = delete;
        explicit StageHandle(pxr::UsdStageRefPtr stage) : stage_(std::move(stage))
        {
            if (stage_)
            {
                auto bridge = exec::ExecBridgeManager::Instance().GetExecBridgeForStage(stage_);
                if (bridge && bridge->GetValueKeyCount() > 0)
                {
                    exec::ExecBridgeManager::Instance().ActivateBridge(bridge);
                }
            }
        }
        
        ~StageHandle()
        {
            if (stage_)
            {
                exec::ExecBridgeManager::Instance().DestroyExecBridgeForStage(stage_);
                stage_.Reset();
            }
        }
        
        //Move-only !!
        StageHandle(const StageHandle&) = delete;
        StageHandle& operator=(const StageHandle&) = delete;
        StageHandle(StageHandle&&) noexcept = default;
        StageHandle& operator=(StageHandle&&) noexcept = default;
        
        const pxr::UsdStageRefPtr& Stage() const { return stage_; }
        
    private:
        pxr::UsdStageRefPtr stage_; 
    };
}
