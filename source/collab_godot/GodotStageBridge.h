#pragma once

/**
 * @file GodotStageBridge.h
 * @brief IStageBridge over a live USD stage and its converted Godot nodes.
 *
 * This is the only place Godot and OpenUSD types meet. It authors edits onto USD
 * prims (the free local save), applies inbound edits with loopback suppression,
 * reads prim transforms, and reports stage-originated changes back to the engine
 * through a TfNotice listener so the engine can gate and coalesce the broadcast.
 *
 * Transforms cross the port as model::PrimEdit (matrix form, row-major, matching
 * the wire convention). USD authoring/reading applies the distinct USD basis
 * convention internally.
 */

#include <string>
#include <unordered_map>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <pxr/base/tf/weakBase.h>
#include <pxr/base/tf/notice.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/notice.h>

#include <idtxflow/net/ports/IStageBridge.h>
#include <idtxflow/utils/Logger.h>

class UsdStageNode3D;

namespace idtxflow
{
namespace collab_godot
{
    class GodotStageBridge : public net::ports::IStageBridge, public pxr::TfWeakBase
    {
    public:
        GodotStageBridge(UsdStageNode3D* stage_node, pxr::UsdStageRefPtr stage);
        ~GodotStageBridge() override;

        GodotStageBridge(const GodotStageBridge&) = delete;
        GodotStageBridge& operator=(const GodotStageBridge&) = delete;

        // IStageBridge
        void build_index() override;
        bool read_prim(const std::string& prim_path, net::model::PrimEdit& out) const override;
        void author_local_edit(const net::model::PrimEdit& edit) override;
        void apply_remote_edit(const net::model::PrimEdit& edit) override;
        void set_on_changed(OnChanged sink) override { on_changed_ = std::move(sink); }

    private:
        IDTX_LOG_CATEGORY("GodotStageBridge")

        struct Tracked { godot::Node3D* node = nullptr; };

        void register_listener();
        void revoke_listener();
        void _on_objects_changed(const pxr::UsdNotice::ObjectsChanged& notice,
                                 const pxr::UsdStageWeakPtr& sender);

        bool author_to_usd(const std::string& prim_path, const godot::Transform3D& xform);
        bool read_prim_transform(const std::string& prim_path, godot::Transform3D& out) const;

        UsdStageNode3D*     stage_node_ = nullptr;   // non-owning
        pxr::UsdStageRefPtr stage_;

        // Set while authoring programmatically (local author or remote apply) so the
        // resulting TfNotice is not reported back as a local change.
        bool suppress_broadcast_ = false;

        pxr::TfNotice::Key notice_key_;
        bool               listening_ = false;

        std::unordered_map<std::string, Tracked> tracked_;   // prim_path -> node
        OnChanged                                on_changed_;
    };

} // namespace collab_godot
} // namespace idtxflow
