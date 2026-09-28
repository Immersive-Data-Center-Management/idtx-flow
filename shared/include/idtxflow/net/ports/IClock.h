#pragma once

/**
 * @file IClock.h
 * @brief Port that makes "now" an injected dependency, so timestamps and any
 *        time-based logic in the core are deterministic under test.
 */

#include <cstdint>

namespace idtxflow
{
namespace net
{
namespace ports
{
    struct IClock
    {
        virtual ~IClock() = default;

        /// Milliseconds since an arbitrary but monotonic-enough epoch, used to
        /// stamp outbound edits.
        virtual int64_t now_millis() const = 0;
    };

} // namespace ports
} // namespace net
} // namespace idtxflow
