#pragma once
/**
 * @file ExecBridgeHandler.h
 * @brief 
 * 
 **/
#include <vector>

#include <idtxflow/idtxflow_api.h>

#include "ExecComputeResult.h"

/// Interface for objects that want to be notified when OpenExec computations
/// produce new values.
///
/// Register implementations with ExecComputeBridge to receive callbacks
/// after each ComputeAndDispatch() cycle.
class IDTXFLOW_API IExecBridgeHandler
{
public:
    virtual ~IExecBridgeHandler();

    /// Called once after all individual OnComputedValue() calls for a single
    /// ComputeAndDispatch() cycle have been issued.
    ///
    /// The default implementation is a no-op; override when batch-level
    /// processing (e.g. a single UI refresh) is preferred.
    virtual void OnComputeComplete(
        const std::vector<ExecComputeResult>& results) {}
};
