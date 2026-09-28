#pragma once

/**
 * @file IFrameTicker.h
 * @brief Port that drives the core's per-frame poll, whose only job is to drain
 *        the outbound per-prim coalescing map. The engine wires the registered
 *        callback to its native per-frame source; clearing it stops the tick.
 */

#include <functional>

namespace idtxflow
{
namespace net
{
namespace ports
{
    struct IFrameTicker
    {
        virtual ~IFrameTicker() = default;

        using Tick = std::function<void()>;

        /// Run fn once per frame on the main thread.
        virtual void set_tick(Tick fn) = 0;

        /// Stop ticking (called from shutdown).
        virtual void clear_tick() = 0;
    };

} // namespace ports
} // namespace net
} // namespace idtxflow
