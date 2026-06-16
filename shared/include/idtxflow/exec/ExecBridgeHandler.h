#pragma once
/**
 * @file ExecBridgeHandler.h
 * @brief 
 * 
 **/
#include <vector>

#include "ExecComputeResult.h"

/// Interface for objects that want to be notified when OpenExec computations
/// produce new values.
///
/// Register implementations with ExecComputeBridge to receive callbacks
/// after each ComputeAndDispatch() cycle.
class IExecBridgeHandler
{
public:
    virtual ~IExecBridgeHandler() = default;

    /// Called once after all individual OnComputedValue() calls for a single
    /// ComputeAndDispatch() cycle have been issued.
    ///
    /// The default implementation is a no-op; override when batch-level
    /// processing (e.g. a single UI refresh) is preferred.
    virtual void OnComputeComplete(
        const std::vector<ExecComputeResult>& results) {}
};