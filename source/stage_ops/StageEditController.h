#pragma once

#include <godot_cpp/classes/node3d.hpp>

namespace idtxflow
{
namespace net { namespace ports { struct IStageBridge; } }

namespace collab
{
    /// Authors a Godot transform change on a converted node into a USD stage via
    /// its authoring bridge. The owning stage + its bridge are resolved by the
    /// caller (the UsdStageNode3D); this type applies the author decision: author
    /// the node's local transform when a session drives the stage or local authoring is enabled
    class StageEditController
    {
    public:
        explicit StageEditController(net::ports::IStageBridge* bridge);

        /// Author `child`'s transform into the stage. No-op when `child` has no
        /// prim path, or when neither a session drives the stage nor
        /// `authoring_enabled` is set. The placement root is skipped unless
        /// `author_root` is set.
        void author_from_node(godot::Node3D* child, bool authoring_enabled, bool author_root);

    private:
        net::ports::IStageBridge* bridge_ = nullptr;   // non-owning; the node owns it
    };

} // namespace collab
} // namespace idtxflow

