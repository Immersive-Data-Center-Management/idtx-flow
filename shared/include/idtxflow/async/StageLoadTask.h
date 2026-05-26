#pragma once

/**
 * @file StageLoadTask.h
 * @brief Engine-agnostic asynchronous USD stage loading using standard C++ threading.
 *
 * This header-only module provides:
 *   - `StageLoadRequest` — parameters for opening a USD stage
 *   - `StageLoadResult` — the outcome (stage pointer or error)
 *   - `StageLoadTask` — manages a worker thread that opens a USD stage and invokes
 *     a callback when complete
 *
 * ## Design principles
 *
 * - **Standard C++ only**: Uses `std::thread`, `std::mutex`, `std::atomic` — no `std::jthread`
 *   or `std::stop_token` to maximize platform compatibility (consoles, mobile, desktop).
 * - **Callback on worker thread**: The callback fires on the worker thread. The engine
 *   integration layer is responsible for marshalling to the main/render thread
 *   (e.g., Godot `call_deferred`, Unreal `AsyncTask(GameThread, ...)`).
 * - **Best-effort cancellation**: Since OpenUSD does not support cancelling a stage open,
 *   `Cancel()` sets a flag that suppresses the callback invocation.
 *
 * ## Usage
 *
 * ```cpp
 * auto task = std::make_unique<idtxflow::async::StageLoadTask>();
 *
 * StageLoadRequest request;
 * request.uri = "https://server.com/models/chair.usd";
 *
 * task->LoadAsync(request, [](StageLoadResult result) {
 *     if (result.success()) {
 *         // Use result.stage — but marshal to main thread first!
 *     }
 * });
 * ```
 */

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/stage.h>

// Diagnostic logging -- Android only, compiles to no-op elsewhere.
#ifdef __ANDROID__
#  include <android/log.h>
#  define IDTX_STAGE_DIAG(fmt, ...) \
       __android_log_print(ANDROID_LOG_INFO, "IDTXFlow", "[StageLoad] " fmt, ##__VA_ARGS__)
#else
#  define IDTX_STAGE_DIAG(fmt, ...) ((void)0)
#endif

namespace idtxflow
{
namespace async
{

    // -----------------------------------------------------------------------
    // StageLoadRequest
    // -----------------------------------------------------------------------

    /**
     * Parameters for opening a USD stage on a worker thread.
     */
    struct StageLoadRequest
    {
        /// The URI or file path of the root layer to open.
        std::string uri;

        /// Optional: SdfLayer content string for an override/session layer.
        /// Used when a prim with a payload needs a stronger opinion layer composed in.
        std::string override_layer_content;

        /// Optional: The identifier to set on the override layer for correct
        /// asset path anchoring within the composed stage.
        std::string override_layer_id;

        /// How much of the stage to load initially. Defaults to LoadNone
        /// (deferred payload loading).
        pxr::UsdStage::InitialLoadSet load_set = pxr::UsdStage::LoadNone;

    };

    // -----------------------------------------------------------------------
    // StageLoadResult
    // -----------------------------------------------------------------------

    /**
     * The outcome of an async stage load operation.
     */
    struct StageLoadResult
    {
        /// The opened stage, or nullptr on failure.
        pxr::UsdStageRefPtr stage;

        /// Human-readable error message on failure; empty on success.
        std::string error_message;

        /// Convenience check.
        bool success() const { return static_cast<bool>(stage); }
    };

    // -----------------------------------------------------------------------
    // Callback type
    // -----------------------------------------------------------------------

    /**
     * Invoked when the stage load completes (on the worker thread).
     * The engine is responsible for marshalling to the main thread before
     * performing any scene-tree operations.
     */
    using StageLoadCallback = std::function<void(StageLoadResult)>;

    // -----------------------------------------------------------------------
    // StageLoadTask
    // -----------------------------------------------------------------------

    /**
     * Manages asynchronous loading of a USD stage on a background thread.
     *
     * Instances are single-use: each `StageLoadTask` handles one load operation.
     * Create a new instance for each stage you want to load.
     */
    class StageLoadTask
    {
    public:
        StageLoadTask() = default;

        ~StageLoadTask()
        {
            // Ensure the worker thread is joined before destruction
            if (worker_.joinable())
            {
                worker_.join();
            }
        }

        // Non-copyable
        StageLoadTask(const StageLoadTask&) = delete;
        StageLoadTask& operator=(const StageLoadTask&) = delete;

        // Movable
        StageLoadTask(StageLoadTask&& other) noexcept
            : worker_(std::move(other.worker_))
            , cancelled_(other.cancelled_.load())
            , complete_(other.complete_.load())
        {
        }

        StageLoadTask& operator=(StageLoadTask&& other) noexcept
        {
            if (this != &other)
            {
                if (worker_.joinable())
                {
                    worker_.join();
                }
                worker_ = std::move(other.worker_);
                cancelled_.store(other.cancelled_.load());
                complete_.store(other.complete_.load());
            }
            return *this;
        }

        /**
         * Open a USD stage asynchronously on a worker thread.
         *
         * The callback is invoked on the worker thread when loading completes
         * (or fails). If `Cancel()` was called before the load finished, the
         * callback is suppressed.
         *
         * @param request   The stage load parameters.
         * @param callback  Called with the result on the worker thread.
         */
        inline void LoadAsync(StageLoadRequest request, StageLoadCallback callback)
        {
            cancelled_.store(false);
            complete_.store(false);

            // Ensure any previous worker is joined
            if (worker_.joinable())
            {
                worker_.join();
            }

            worker_ = std::thread([this, req = std::move(request), cb = std::move(callback)]()
            {
                StageLoadResult result = LoadSync(req);
                complete_.store(true);

                if (!cancelled_.load() && cb)
                {
                    cb(std::move(result));
                }
            });
        }

        /**
         * Open a USD stage synchronously (blocking the calling thread).
         *
         * This is useful when you are already on a worker thread and don't
         * need the additional indirection of spawning another thread.
         *
         * @param request  The stage load parameters.
         * @return         The load result with stage pointer or error.
         */
        static inline StageLoadResult LoadSync(const StageLoadRequest& request)
        {
            StageLoadResult result;

            if (request.uri.empty())
            {
                result.error_message = "Empty URI provided";
                return result;
            }

            try
            {
                IDTX_STAGE_DIAG("SdfLayer::FindOrOpen start -- uri=%s", request.uri.c_str());
                pxr::SdfLayerRefPtr root_layer = pxr::SdfLayer::FindOrOpen(request.uri);
                IDTX_STAGE_DIAG("SdfLayer::FindOrOpen done -- layer=%s", root_layer ? "valid" : "null");

                if (!root_layer)
                {
                    result.error_message = "Failed to open root layer: " + request.uri;
                    return result;
                }

                if (!request.override_layer_content.empty())
                {
                    // Create an override/session layer from the provided content string.
                    // This is used for payload prims that need a stronger opinion layer.
                    IDTX_STAGE_DIAG("SdfLayer::CreateAnonymous (override)");
                    pxr::SdfLayerRefPtr override_layer = pxr::SdfLayer::CreateAnonymous("override_layer");
                    override_layer->ImportFromString(request.override_layer_content);

                    if (!request.override_layer_id.empty())
                    {
                        override_layer->SetIdentifier(request.override_layer_id);
                    }

                    IDTX_STAGE_DIAG("UsdStage::Open start (with override layer, load_set=%d)", (int)request.load_set);
                    result.stage = pxr::UsdStage::Open(root_layer, override_layer, request.load_set);
                    IDTX_STAGE_DIAG("UsdStage::Open done -- stage=%s", result.stage ? "valid" : "null");
                }
                else
                {
                    IDTX_STAGE_DIAG("UsdStage::Open start (load_set=%d)", (int)request.load_set);
                    result.stage = pxr::UsdStage::Open(root_layer, request.load_set);
                    IDTX_STAGE_DIAG("UsdStage::Open done -- stage=%s", result.stage ? "valid" : "null");
                }

                if (!result.stage)
                {
                    result.error_message = "UsdStage::Open returned null for: " + request.uri;
                }
            }
            catch (const std::exception& e)
            {
                result.error_message = std::string("Exception opening stage: ") + e.what();
            }
            catch (...)
            {
                result.error_message = "Unknown exception opening stage: " + request.uri;
            }

            return result;
        }

        /**
         * Request cancellation of the pending load.
         *
         * Since OpenUSD does not support interrupting `SdfLayer::FindOrOpen` or
         * `UsdStage::Open`, this is best-effort: the callback will be suppressed
         * if the load completes after cancellation is requested.
         */
        inline void Cancel()
        {
            cancelled_.store(true);
        }

        /**
         * Check if a load is currently in progress (started but not yet complete).
         */
        inline bool IsPending() const
        {
            return worker_.joinable() && !complete_.load();
        }

        /**
         * Check if the load has completed (successfully or with an error).
         */
        inline bool IsComplete() const
        {
            return complete_.load();
        }

        /**
         * Check if cancellation has been requested.
         */
        inline bool IsCancelled() const
        {
            return cancelled_.load();
        }

    private:
        std::thread worker_;
        std::atomic<bool> cancelled_{false};
        std::atomic<bool> complete_{false};
    };

} // namespace async
} // namespace idtxflow