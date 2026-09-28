#pragma once

/**
 * @file IMainThreadDispatcher.h
 * @brief Port for hopping work back onto the engine's main thread.
 *
 * The core does its networking on background threads but must never touch engine
 * objects off the main thread. Any result destined for the engine is handed to
 * this single seam, so observers always run where the engine expects them.
 */

#include <functional>

namespace idtxflow
{
namespace net
{
namespace ports
{
    struct IMainThreadDispatcher
    {
        virtual ~IMainThreadDispatcher() = default;

        /// Run fn on the engine main thread soon; delivery by the next frame is fine.
        virtual void post(std::function<void()> fn) = 0;
    };

} // namespace ports
} // namespace net
} // namespace idtxflow
