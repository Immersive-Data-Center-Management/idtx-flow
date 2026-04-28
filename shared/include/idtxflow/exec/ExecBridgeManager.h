#pragma once
/**
 * @file ExecBridgeManager.h
 * @brief The openUSD execution / compute model (openExec) allows to compute attributes of a specific schema (HasAPI or IsA)
 * based on other attributes of the same or other prims. This model uses some sort of pull mechanism that
 * requires to trigger computation and then requesting the computation results.
 * 
 * On the contrary the immersive experience would use some sort of push mechanics to update data that shall be
 * visualized (eg. data visualized in a HUD, or 3D widget, material shader color changes).
 * 
 * The ExecBridgeManager is kind of glue functionality that bridges those two worlds. If a prim is converted and
 * contains computed attributes, it will register them for computation with openExec and also registers the handler of
 * the converted prim to react to updates of values on those attributes. As the openExec system requires preparation
 * and registration for individual stages, the manager will take care of this and use individual ExecBridge's that are
 * tight to a stage.
 * 
 */

#include <map>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/exec/execUsd/system.h>
#include <pxr/exec/execUsd/valueKey.h>
#include <pxr/exec/execUsd/request.h>
#include <pxr/exec/execUsd/cacheView.h>

#include <idtxflow/utils/Logger.h>

namespace idtxflow
{
namespace exec
{
    class ExecBridge;

    class ExecBridgeManager
    {
    public:
        /**
         * Singleton Accessor
         * @return The ExecBridgeManager
         */
        static ExecBridgeManager& Instance() {
            static ExecBridgeManager instance;
            return instance;
        }

        /**
         * Get or instantiate the execution bridge for the stage provided. To actually run a computation
         * first the requested attributes that are connected to compute-nodes need to be registered using
         * ExecBridge->RegisterAttributeWithConnection, once all attributes are registered ExecBridge->BuildRequest()
         * need to be called. Then ExecBridge->ComputeAndDispatch() can be invoked (e.g. repeatedly time based) to run
         * computation and dispatch the result to the respective registered handlers.
         * @param stage The stage the usd exec system shall be instantiated and the exec bridge provided for.
         * @return 
         */
        ExecBridge& GetExecBridge(const pxr::UsdStageRefPtr& stage) {
            auto [it, bridge] = stage_exec_bridges_.try_emplace(stage);
            if (bridge) it->second = std::make_shared<ExecBridge>(stage);
        
            return *it->second;
        }
        
    private:
        // don't allow manual construction or copy assignments of this singleton instance
        ExecBridgeManager() = default;
        ExecBridgeManager(const ExecBridgeManager&) = delete;
        ExecBridgeManager& operator=(const ExecBridgeManager&) = delete;
        
        // the ExecBridgeManager manages the ExecBridges for individual stages
        std::map<pxr::UsdStageRefPtr, std::shared_ptr<ExecBridge>> stage_exec_bridges_;
    };

    class ExecBridge
    {
    public:
        explicit ExecBridge(const pxr::UsdStageRefPtr& stage)
            : exec_system_(std::make_unique<pxr::ExecUsdSystem>(stage)) {}
        ~ExecBridge() = default;

        /**
         * Register a prims attribute with an authored connection to be considered during Exec Computations.
         * The connection authored for the attribute (e.g. myAttribute.connect = </Scene/SomeComputeNode.outputs:value>)
         * must point to an attribute on a prim that has a registered PrimComputation with the matching computation name.
         * If multiple connections has been authored, only the first one will be used.
         * @param attribute The UsAttribute with authored connections
         * @return The index of the registered computation
         */
        int RegisterAttributeWithConnection(const pxr::UsdAttribute& attribute)
        {
            pxr::SdfPathVector connections;
            if (!attribute.GetConnections(&connections) || connections.empty())
                return -1;
        
            const pxr::SdfPath& connection = connections[0];
            // Extract the prim path and the property name (used as computation token).
            const pxr::SdfPath primPath = connection.GetPrimPath();
            const std::string propName = connection.GetName();
            if (primPath.IsEmpty() || propName.empty())
                return -1;
        
            pxr::UsdPrim sourcePrim = attribute.GetPrim().GetStage()->GetPrimAtPath(primPath);
            if (!sourcePrim.IsValid())
            {
                IDTX_LOG(IDTX_ERROR, "RegisterAttributeWithConnection: source prim '{}' not found for connection on '{}'",
                                primPath.GetText(),
                                attribute.GetPath().GetText());
                return -1;
            }
        
            const pxr::TfToken computationToken(propName);
            const int index = static_cast<int>(exec_value_keys_.size());
            // store the requested computation key
            exec_value_keys_.emplace_back(sourcePrim, computationToken);
            // store the corresponding metadata
            value_key_metas_.push_back({
                attribute.GetPath(),
                computationToken,
                pxr::VtValue()
            });
        
            IDTX_LOG(IDTX_DEBUG, "AddValueKeyFromConnection: resolved connection '{}' -> prim '{}', computation '{}'",
                        attribute.GetPath().GetText(),
                        primPath.GetText(),
                        propName);
        
            return index;
        }
        
        /// Return the number of value keys registered.
        std::size_t GetValueKeyCount() const { return exec_value_keys_.size(); }

        /**
         * Build the ExecUsdRequest required for computations for all registered value keys.
         */
        void BuildRequest()
        {
            assert(exec_system_ && "ExecBridge::BuildRequest called without a valid ExecUsdSystem");
            assert(!exec_value_keys_.empty() && "ExecBridge::BuildRequest called with no value keys");
    
            // Value invalidation callback — records which indices became invalid and would thus not be picked up
            // from the compute result cache. This could help, optimizing the result dispatching
            // for the time being we only note down how many values have been invalidated and will be re-calculated 
            auto invalidation_callback = 
                    [&](const pxr::ExecRequestIndexSet& indices, const pxr::EfTimeInterval& /* timeInterval*/ )
                    {
                        IDTX_LOG(IDTX_DEBUG, "Invalidation Callback with {} indices called", indices.size());
                    };
            exec_request_ = std::make_unique<pxr::ExecUsdRequest>(
                exec_system_->BuildRequest(
                    std::move(exec_value_keys_),
                    invalidation_callback
                )
            );
    
            // finish compute request preparation for execution. This might be a heavy task, as this will "compile" the compute
            // tree for performance optimized execution
            exec_system_->PrepareRequest(*exec_request_);
        }

        /**
         * Executes the computation for this stage and dispatches the results to the registered handlers
         */
        void ComputeAndDispatch()
        {
            // if the request is not set-up properly, there is nothing to do 
            if (!exec_request_ || !exec_request_->IsValid())
                return;
    
            // Run the Exec computation.
            // This will always publish an entry in the CacheView for all value keys passed to BuildRequest. However, the ones
            // that did not require recomputation, where provided from the cache. We could use the invalidation callback
            // to collect all the indices that has been really recalculated.
            pxr::ExecUsdCacheView view = exec_system_->Compute(*exec_request_);
    
            // as the cache view does not allow any traversal and only index access, we use the metadata array whose index
            // matches the val key array passed to the computation
            size_t count = value_key_metas_.size();
            for (size_t i = 0; i < count; ++i)
            {
                pxr::VtValue computedValue = view.Get(i);
                ValueKeyMetadata& metadata = value_key_metas_[i];
        
                // if the computed (cached) value is the same as the last one, we skip dispatching
                if (computedValue == metadata.lastComputedValue) continue;
                metadata.lastComputedValue = computedValue;
        
                IDTX_LOG(IDTX_DEBUG, "Compute result of Attribute {} using Computation {} = {}",
                     metadata.attributePath.GetText(), metadata.computationName.GetText(), pxr::TfStringify(computedValue).c_str());
        
                // TODO: with the new value in place, dispatch the data to the registered handler
            }
        }

    protected:
        /// The usd execution system only stores ValueKeys and uses an index into an array of the same when providing the
        /// result of a computation. We need to maintain metadata for those ValueKeys in a parallel array to be able to
        /// bridge the results back to their original prim attribute
        struct ValueKeyMetadata
        {
            pxr::SdfPath attributePath; // the origin of the computation
            pxr::TfToken computationName; // the name of the computation
            pxr::VtValue lastComputedValue; // the last computed value for change detection and dispatch optimization
        };
        
        /// The Execution system used to build the execution request and pull the execution results
        std::unique_ptr<pxr::ExecUsdSystem> exec_system_;
        /// The Execution request, this "owns" the ValueKey's that will be registerd for computation
        std::unique_ptr<pxr::ExecUsdRequest> exec_request_;
        /// The list of value keys that will be consumed by BuildRequest via move
        std::vector<pxr::ExecUsdValueKey> exec_value_keys_;
        /// The parallel list, having the same indices for their matching exec_value_keys_ entry
        std::vector<ValueKeyMetadata> value_key_metas_;
        
    private:
        // disallow copy construct on the ExecBridge 
        ExecBridge(const ExecBridge&) = delete;
        ExecBridge& operator=(const ExecBridge&) = delete;
        
        IDTX_LOG_CATEGORY("ExecBridge");
    };
}
}
