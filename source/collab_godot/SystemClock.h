#pragma once

/**
 * @file SystemClock.h
 * @brief IClock backed by the system wall clock, used to stamp outbound edits.
 */

#include <chrono>
#include <cstdint>

#include <idtxflow/net/ports/IClock.h>

namespace idtxflow
{
namespace collab_godot
{
    class SystemClock : public net::ports::IClock
    {
    public:
        int64_t now_millis() const override
        {
            using namespace std::chrono;
            return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        }

        /// Process-wide instance; shared freely since it holds no state.
        static SystemClock& instance()
        {
            static SystemClock clock;
            return clock;
        }
    };

} // namespace collab_godot
} // namespace idtxflow
