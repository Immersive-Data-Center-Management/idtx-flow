#pragma once

/**
 * @file GodotTicker.h
 * @brief IFrameTicker driven by SceneTree::process_frame.
 *
 * The tick exists only to drain outbound coalescing once per frame. The host
 * object exposes a bound method (named by `frame_method`) connected to the
 * scene tree's per-frame signal; that method calls fire(), which runs the
 * registered tick. process_frame emits every frame in the editor as well as at
 * runtime, so this drives poll() in edit mode too (unlike Node::_process, which
 * an out-of-tree engine singleton never receives).
 *
 * The collaboration singleton is created at module init, before any SceneTree
 * exists, so the connection is made lazily: set_tick stores the callback and
 * tries to connect; try_connect() is idempotent and safe to re-invoke every
 * frame / from a deferred bootstrap until the tree is up. clear_tick()
 * disconnects and forgets it.
 */

#include <functional>
#include <utility>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <idtxflow/net/ports/IFrameTicker.h>

namespace idtxflow
{
namespace collab_godot
{
    class GodotTicker : public net::ports::IFrameTicker
    {
    public:
        GodotTicker(godot::Object* host, godot::StringName frame_method)
            : host_(host), frame_method_(std::move(frame_method)) {}

        void set_tick(Tick fn) override
        {
            tick_ = std::move(fn);
            try_connect();
        }

        void clear_tick() override
        {
            disconnect();
            tick_ = nullptr;
        }

        /// Invoked from the host's per-frame method; runs the registered tick.
        void fire()
        {
            if (tick_) tick_();
        }

        /// Connect to process_frame if not already connected. Idempotent and
        /// cheap; safe to call repeatedly (e.g. from a deferred bootstrap) until
        /// the SceneTree exists, since it does not at module-init time.
        void try_connect()
        {
            if (connected_ || !host_ || !tick_)
            {
                return;
            }
            godot::SceneTree* tree = scene_tree();
            if (!tree)
            {
                return;
            }
            const godot::Callable cb(host_, frame_method_);
            if (!tree->is_connected("process_frame", cb))
            {
                tree->connect("process_frame", cb);
            }
            connected_ = true;
        }

        bool is_connected() const { return connected_; }

    private:
        void disconnect()
        {
            if (!connected_ || !host_)
            {
                connected_ = false;
                return;
            }
            godot::SceneTree* tree = scene_tree();
            const godot::Callable cb(host_, frame_method_);
            if (tree && tree->is_connected("process_frame", cb))
            {
                tree->disconnect("process_frame", cb);
            }
            connected_ = false;
        }

        static godot::SceneTree* scene_tree()
        {
            godot::Engine* engine = godot::Engine::get_singleton();
            if (!engine)
            {
                return nullptr;
            }
            return godot::Object::cast_to<godot::SceneTree>(engine->get_main_loop());
        }

        godot::Object*    host_;
        godot::StringName frame_method_;
        Tick              tick_;
        bool              connected_ = false;
    };

} // namespace collab_godot
} // namespace idtxflow
