#pragma once

/**
 * @file GodotDispatcher.h
 * @brief IMainThreadDispatcher that runs work on Godot's main thread.
 *
 * Background results are queued and a drain is scheduled via call_deferred on a
 * host object (the collaboration node), so queued callbacks run on the main
 * thread on the next frame. After shutdown, posted callbacks are dropped so
 * nothing touches a torn-down engine or a freed object.
 *
 * The host object owns a bound method (named by `drain_method`) that calls
 * drain(); this keeps the dispatcher decoupled from the concrete node type.
 */

#include <functional>
#include <mutex>
#include <queue>
#include <utility>

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <idtxflow/net/ports/IMainThreadDispatcher.h>

namespace idtxflow
{
namespace collab_godot
{
    class GodotDispatcher : public net::ports::IMainThreadDispatcher
    {
    public:
        GodotDispatcher(godot::Object* host, godot::StringName drain_method)
            : host_(host), drain_method_(std::move(drain_method)) {}

        void post(std::function<void()> fn) override
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!active_ || !host_)
                {
                    return;
                }
                queue_.push(std::move(fn));
            }
            // Ask the host to drain on the main thread next frame.
            host_->call_deferred(drain_method_);
        }

        /// Run all queued callbacks. Invoked on the main thread by the host's
        /// bound drain method.
        void drain()
        {
            for (;;)
            {
                std::function<void()> fn;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (queue_.empty())
                    {
                        return;
                    }
                    fn = std::move(queue_.front());
                    queue_.pop();
                }
                if (fn) fn();
            }
        }

        /// Stop accepting work and discard anything still queued.
        void shutdown()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_ = false;
            std::queue<std::function<void()>> empty;
            queue_.swap(empty);
        }

    private:
        godot::Object*     host_;
        godot::StringName  drain_method_;
        std::mutex         mutex_;
        std::queue<std::function<void()>> queue_;
        bool               active_ = true;
    };

} // namespace collab_godot
} // namespace idtxflow
