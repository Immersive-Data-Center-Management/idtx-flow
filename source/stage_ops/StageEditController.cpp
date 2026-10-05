#include "StageEditController.h"

#include <string>

#include <idtxflow_godot/nodes/IUsdNode3D.h>
#include <idtxflow/net/model/Types.h>
#include <idtxflow/net/ports/IStageBridge.h>
#include <idtxflow/utils/Logger.h>

#include "TransformCodec.h"

namespace gxform = idtxflow::collab::xform;

using namespace godot;

namespace
{
    IDTX_LOG_CATEGORY("StageEditController")
}

namespace idtxflow
{
namespace collab
{
    StageEditController::StageEditController(net::ports::IStageBridge* bridge)
        : bridge_(bridge)
    {
    }

    void StageEditController::author_from_node(Node3D* child, bool authoring_enabled, bool author_root)
    {
        if (bridge_ == nullptr || child == nullptr)
            return;

        const bool has_sink = bridge_->has_on_changed();
        IUsdNode3D* usd = IUsdNode3D::from_node(child);
        const std::string prim_path =
            usd ? std::string(usd->get_prim_path().utf8().get_data()) : std::string();
        IDTX_LOG(IDTX_DEBUG, "author_from_node prim='{}' authoring_enabled={} has_on_changed={}",
                 prim_path, authoring_enabled, has_sink);

        // Author when a session drives this stage (a change-report sink is
        // installed) OR local authoring is opted in. Broadcast, if any, is decided
        // downstream by whether that sink is present.
        if (!has_sink && !authoring_enabled)
        {
            IDTX_LOG(IDTX_DEBUG, "skip: gated (no session, local authoring off)");
            return;
        }

        if (usd == nullptr || prim_path.empty())
        {
            IDTX_LOG(IDTX_DEBUG, "skip: node has no prim path");
            return;
        }

        // The placement root carries display-only transform; skip it unless the
        // node opted into authoring the root.
        if (!author_root && bridge_->is_stage_root(prim_path))
        {
            IDTX_LOG(IDTX_DEBUG, "skip: placement root '{}'", prim_path);
            return;
        }

        // Author the node's local transform (not its world transform): a parent
        // move changes a child's world transform but not its local.
        bridge_->author_local_edit(gxform::transform_to_prim_edit(prim_path, child->get_transform()));
    }

} // namespace collab
} // namespace idtxflow
