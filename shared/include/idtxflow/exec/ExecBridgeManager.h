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

#include <algorithm>
#include <assert.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>
#include <map>

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/exec/execUsd/system.h>
#include <pxr/exec/execUsd/valueKey.h>
#include <pxr/exec/execUsd/request.h>
#include <pxr/exec/execUsd/cacheView.h>

#include <idtxflow/utils/Logger.h>

#include "ExecBridgeHandler.h"
#include "ExecComputeResult.h"

namespace idtxflow
{
namespace exec
{
    struct StageAttributeUpdate
    {
        pxr::SdfPath prim_path;
        pxr::TfToken attribute_name;
        pxr::VtValue value;
        uint64_t sequence = 0;
        std::string source;
    };

    enum class ExecBridgeLifecycle { Created, Active, Stopping, Stopped };
    /**
     * @class ExecBridge
     * @brief This class provides the functional glue between the openExec computation system and the game engine
     * specific notification handlers.
     */
    class ExecBridge
    {
    public:
        explicit ExecBridge(const pxr::UsdStageRefPtr& stage)
            : stage_(stage), exec_system_(std::make_unique<pxr::ExecUsdSystem>(stage)) {}
        ~ExecBridge() = default;

        ExecBridgeLifecycle GetLifecycle() const { return lifecycle_.load(std::memory_order_acquire); }
        bool Activate() { ExecBridgeLifecycle expected = ExecBridgeLifecycle::Created; return lifecycle_.compare_exchange_strong(expected, ExecBridgeLifecycle::Active, std::memory_order_acq_rel); }
        void BeginStopping() { ExecBridgeLifecycle state = GetLifecycle(); while (state == ExecBridgeLifecycle::Created || state == ExecBridgeLifecycle::Active) { if (lifecycle_.compare_exchange_weak(state, ExecBridgeLifecycle::Stopping, std::memory_order_acq_rel)) return; } }
        void FinishStopping() { { std::lock_guard<std::mutex> lock(queue_mutex_); pending_updates_.clear(); pending_update_order_.clear(); } { std::lock_guard<std::mutex> lock(handler_mutex_); result_handlers_.clear(); } lifecycle_.store(ExecBridgeLifecycle::Stopped, std::memory_order_release); }

        bool EnqueueAttributeUpdate(StageAttributeUpdate update)
        {
            if (GetLifecycle() != ExecBridgeLifecycle::Active || update.prim_path.IsEmpty() || update.attribute_name.IsEmpty() || update.value.IsEmpty()) { rejected_updates_.fetch_add(1); return false; }
            const std::string key = update.prim_path.GetString() + "." + update.attribute_name.GetString();
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (GetLifecycle() != ExecBridgeLifecycle::Active) { rejected_updates_.fetch_add(1); return false; }
            auto it = pending_updates_.find(key);
            if (it != pending_updates_.end()) { it->second = std::move(update); coalesced_updates_.fetch_add(1); return true; }
            if (pending_updates_.size() >= kMaxPendingUpdates) { rejected_updates_.fetch_add(1); return false; }
            pending_update_order_.push_back(key);
            pending_updates_.emplace(key, std::move(update));
            accepted_updates_.fetch_add(1);
            return true;
        }

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
                pxr::VtValue(),
                attribute.GetPrimPath(),
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
                        //IDTX_LOG(IDTX_DEBUG, "Invalidation Callback with {} indices called", indices.size());
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
         * Register a borrowed compute-result handler.
         *
         * ExecBridge never owns or deletes the handler. The StageHandle must
         * deactivate and destroy this bridge before converted entities are
         * destroyed, so no callback can observe a dangling handler pointer.
         */
        void RegisterComputeResultHandler(const pxr::SdfPath& primPath, IExecBridgeHandler* handler)
        {
            if (!handler || GetLifecycle() != ExecBridgeLifecycle::Created) return;
            std::lock_guard<std::mutex> lock(handler_mutex_);
            auto& handlers = result_handlers_[primPath];
            if (std::find(handlers.begin(), handlers.end(), handler) == handlers.end())
                handlers.push_back(handler);
        }

        void UnregisterComputeResultHandler(const pxr::SdfPath& primPath, IExecBridgeHandler* handler)
        {
            if (!handler) return;
            std::lock_guard<std::mutex> lock(handler_mutex_);
            auto it = result_handlers_.find(primPath);
            if (it == result_handlers_.end()) return;
            auto& handlers = it->second;
            handlers.erase(std::remove(handlers.begin(), handlers.end(), handler), handlers.end());
            if (handlers.empty()) result_handlers_.erase(it);
        }
        /**
         * Executes the computation for this stage and dispatches the results to the registered handlers
         */
        void ComputeAndDispatch()
        {
            if (GetLifecycle() != ExecBridgeLifecycle::Active) return;
            ApplyPendingUpdates();
            // if the request is not set-up properly, there is nothing to do 
            if (!exec_request_ || !exec_request_->IsValid())
                return;

            // Advance the Exec evaluation time before computing. This is the trigger that makes
            // time-dependent computations (e.g. Compute_VarReplacements, which declares a dependency
            // on the stage's builtin computeTime) get invalidated and re-run — so that volatile tokens
            // like ${var:CURRENT_TIME} are re-resolved on every cycle. Computations that do NOT declare
            // a time dependency are unaffected: ChangeTime only re-resolves and invalidates inputs that
            // actually depend on time, so their cached results are still served without recomputation.
            //
            // We use a simple monotonically increasing frame counter as the time code. The absolute value
            // is irrelevant to our providers (they read wall-clock/host state directly); all that matters
            // is that the time changes each cycle so Exec performs its time-based invalidation. Advancing
            // by a whole frame each tick keeps the values distinct and avoids any floating point stalling.
            exec_time_frame_ += 1.0;
            exec_system_->ChangeTime(pxr::UsdTimeCode(exec_time_frame_));

            // Run the Exec computation.
            // This will always publish an entry in the CacheView for all value keys passed to BuildRequest. However, the ones
            // that did not require recomputation, where provided from the cache. We could use the invalidation callback
            // to collect all the indices that has been really recalculated.
            pxr::ExecUsdCacheView view = exec_system_->Compute(*exec_request_);
            
            //collect the computation results grouped by their primPath to dispatch them in one batch to their
            // respective handler
            std::unordered_map<pxr::SdfPath, std::vector<ExecComputeResult>, pxr::SdfPath::Hash> computationResults;
    
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
        
                // Keep this code commented for easy re-activation if error analysis is required.
                //IDTX_LOG(IDTX_DEBUG, "Compute result of Attribute {} using Computation {} = {}",
                //     metadata.attributePath.GetText(), metadata.computationName.GetText(), pxr::TfStringify(computedValue).c_str());
        
                ExecComputeResult result = {
                    metadata.attributePath.GetPrimPath(),
                    metadata.attributePath.GetName(),
                    metadata.computationName,
                    computedValue,
                    static_cast<int>(i)
                };
                
                computationResults[metadata.handlerPath].push_back(result);
            }
            
            // once all updated attributes have been collected, invoke the registered handler for them
            std::lock_guard<std::mutex> handler_lock(handler_mutex_);
            for (const auto& result: computationResults)
            {
                auto it = result_handlers_.find(result.first);
                if (it != result_handlers_.end())
                {
                    for (const auto& handler: it->second)
                        handler->OnComputeComplete(result.second);
                } else
                {
                    IDTX_LOG(IDTX_DEBUG, "No handler found for {}", result.first.GetText());
                }
            }
        }

    private:
        void ApplyPendingUpdates()
        {
            std::vector<StageAttributeUpdate> updates;
            { std::lock_guard<std::mutex> lock(queue_mutex_); for (const auto& key : pending_update_order_) { auto it = pending_updates_.find(key); if (it != pending_updates_.end()) updates.push_back(std::move(it->second)); } pending_updates_.clear(); pending_update_order_.clear(); }
            for (const auto& update : updates) {
                pxr::UsdPrim prim = stage_->GetPrimAtPath(update.prim_path);
                pxr::UsdAttribute attribute = prim ? prim.GetAttribute(update.attribute_name) : pxr::UsdAttribute();
                const pxr::TfToken type = attribute ? attribute.GetTypeName().GetAsToken() : pxr::TfToken();
                const bool compatible = attribute && ((type == pxr::TfToken("string") && update.value.IsHolding<std::string>()) || (type == pxr::TfToken("float") && update.value.IsHolding<float>()) || (type == pxr::TfToken("double") && update.value.IsHolding<double>()));
                if (!compatible || !attribute.Set(update.value)) { failed_updates_.fetch_add(1); continue; }
                applied_updates_.fetch_add(1);
            }
        }

    protected:
        // The usd execution system only stores ValueKeys and uses an index into an array of the same when providing the
        // results of a computation. We need to maintain metadata for those ValueKeys in a parallel array to be able to
        // bridge the results back to their original prim attribute
        struct ValueKeyMetadata
        {
            // The origin of the computation. This is the complete path to the attribute in the stage including its
            // prim path
            pxr::SdfPath attributePath;
            // The name of the computation. This is in most cases the pure attribute name
            pxr::TfToken computationName; 
            // The last computed value for change detection and dispatch optimization
            pxr::VtValue lastComputedValue; 
            // The prim path at which the handler for this attribute computation result will be invoked.
            // This is usually the prim path of the one whos converted entity will handle the changed value
            pxr::SdfPath handlerPath; 
        };
        
        pxr::UsdStageRefPtr stage_;
        // The Execution system used to build the execution request and pull the execution results
        std::unique_ptr<pxr::ExecUsdSystem> exec_system_;
        // The Execution request, this "owns" the ValueKey's that will be registerd for computation
        std::unique_ptr<pxr::ExecUsdRequest> exec_request_;
        // The list of value keys that will be consumed by BuildRequest via move
        std::vector<pxr::ExecUsdValueKey> exec_value_keys_;
        // The parallel list, having the same indices for their matching exec_value_keys_ entry
        std::vector<ValueKeyMetadata> value_key_metas_;
        // The list of handlers that will be invoked to handle the updated computation results
        std::unordered_map<pxr::SdfPath, std::vector<IExecBridgeHandler*>, pxr::SdfPath::Hash> result_handlers_;
        mutable std::mutex handler_mutex_;
        static constexpr std::size_t kMaxPendingUpdates = 1024;
        std::unordered_map<std::string, StageAttributeUpdate> pending_updates_;
        std::vector<std::string> pending_update_order_;
        mutable std::mutex queue_mutex_;
        std::atomic<ExecBridgeLifecycle> lifecycle_{ExecBridgeLifecycle::Created};
        std::atomic<uint64_t> accepted_updates_{0}, coalesced_updates_{0}, rejected_updates_{0}, applied_updates_{0}, failed_updates_{0};

        // Monotonically increasing time code fed to ExecUsdSystem::ChangeTime() on every
        // ComputeAndDispatch() cycle. This drives Exec's time-based invalidation so that
        // time-dependent computations (those that declare a dependency on the builtin computeTime,
        // e.g. Compute_VarReplacements) are re-run each cycle. Its absolute value carries no meaning
        // for our providers; it only needs to change monotonically so Exec sees a new time each tick.
        double exec_time_frame_ = 0.0;

    private:
        // disallow copy construct on the ExecBridge 
        ExecBridge(const ExecBridge&) = delete;
        ExecBridge& operator=(const ExecBridge&) = delete;
        
        IDTX_LOG_CATEGORY("ExecBridge");
    };

    /**
     * @class ExecBridgeManager
     * @brief This singleton instance class manages the execution bridges for each stage. It provides a convenient way
     * to retrieve the ExecBridge instance for a stage.
     */
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
         * @return The ExecBridge
         */
        std::shared_ptr<ExecBridge> GetExecBridgeForStage(const pxr::UsdStageRefPtr& stage) {
            std::lock_guard<std::mutex> lock(registry_mutex_);
            auto [it, created] = stage_exec_bridges_.try_emplace(stage);
            if (created) it->second = std::make_shared<ExecBridge>(stage);
            return it->second;
        }

        std::shared_ptr<ExecBridge> FindExecBridgeForStage(const pxr::UsdStageRefPtr& stage)
        {
            std::lock_guard<std::mutex> lock(registry_mutex_);
            auto it = stage_exec_bridges_.find(stage);
            return it == stage_exec_bridges_.end() ? nullptr : it->second;
        }
        /**
         * Destroy and remove an existing execBridge for the given stage. This will unregister it from being considered
         * in the computation thread. This has to be called before the the stage and it's converted prims are destroyed.
         * Otherwise it may find dangling pointers when dispatching any updates.
         * @param stage The stage the exec bridge should be destroyed for.
         */
        void DestroyExecBridgeForStage(const pxr::UsdStageRefPtr& stage)
        {
            std::shared_ptr<ExecBridge> bridge;
            { std::lock_guard<std::mutex> registry_lock(registry_mutex_); auto it = stage_exec_bridges_.find(stage); if (it == stage_exec_bridges_.end()) return; bridge = it->second; }
            DeactivateBridge(bridge);
            std::lock_guard<std::mutex> registry_lock(registry_mutex_);
            stage_exec_bridges_.erase(stage);
        }

        /**
         * Activate an ExecBridge of a stage to be considered in the worker thread to trigger computations.
         * @param bridge The ExecBridge that shall be activated and run it's ComputeAndDispatch cycles
         */
        void ActivateBridge(std::shared_ptr<ExecBridge> bridge)
        {
            if (!bridge || bridge->GetLifecycle() != ExecBridgeLifecycle::Created) return;
            bridge->BuildRequest();
            if (!bridge->Activate()) return;
            std::lock_guard<std::mutex> lock(activation_mutex_);
            if (std::find(active_bridges_.begin(), active_bridges_.end(), bridge) == active_bridges_.end()) active_bridges_.push_back(std::move(bridge));
        }

        /**
         * Deactivate an ExecBridge of a stage. This is required to be called if a stage got "unloaded" to ensure
         * there is no computation being dispatched for the bridge any longer 
         * @param bridge 
         */
        void DeactivateBridge(std::shared_ptr<ExecBridge> bridge)
        {
            if (!bridge) return;
            bridge->BeginStopping();
            std::lock_guard<std::mutex> lock(activation_mutex_);
            active_bridges_.erase(std::remove(active_bridges_.begin(), active_bridges_.end(), bridge), active_bridges_.end());
            bridge->FinishStopping();
        }

        /**
         * This spawns the thread that runs the computations on all the registered and active computations through their
         * ExecBridges. The computation interval is actually hard-coded to run every 100ms.
         */
        void Start()
        {
            cancelled_.store(false);
            
            if (worker_.joinable()) worker_.join();
            
            worker_ = std::thread([this]()
            {
                while (!cancelled_.load())
                {
                    {
                        std::lock_guard<std::mutex> lock(activation_mutex_);
                        for (const auto& bridge : active_bridges_)
                        {
                            bridge->ComputeAndDispatch();
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
        }
        
        void Cancel()
        {
            cancelled_.store(true);
            if (worker_.joinable()) worker_.join();
        }
        
    private:
        // don't allow manual construction or copy assignments of this singleton instance
        ExecBridgeManager() = default;
        ExecBridgeManager(const ExecBridgeManager&) = delete;
        ExecBridgeManager& operator=(const ExecBridgeManager&) = delete;
        
        // the ExecBridgeManager manages the ExecBridges for individual stages
        std::map<pxr::UsdStageRefPtr, std::shared_ptr<ExecBridge>> stage_exec_bridges_;
        mutable std::mutex registry_mutex_;
        
        // The ExecBridgeManager will spawn a worker thread that runs the Compute&Dispatch for
        // all stages that has been "activated"
        std::thread worker_;
        std::atomic<bool> cancelled_{false};
        std::vector<std::shared_ptr<ExecBridge>> active_bridges_;
        mutable std::mutex activation_mutex_;
    };
}
}
