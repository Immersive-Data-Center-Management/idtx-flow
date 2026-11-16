#pragma once

/**
 * @file StageBridge.h
 * @brief IStageBridge over a live USD stage and its converted Godot nodes.
 *
 * Where Godot and OpenUSD types meet for the net binding's transform
 * authoring/reading. It writes edits to USD prims in the live stage,
 * reads prim transforms, and reports stage changes through a TfNotice listener.
 * The collaboration engine decides whether to send those changes.
 * File export is a separate operation. Each authored edit carries an EditOrigin:
 * a Local edit reports changed tracked transforms for broadcast; an External
 * (peer) edit updates the stage without being reported back as a local change.
 *
 * Reported transforms cross the port as model::PrimEdit (matrix form, row-major,
 * matching the wire convention). Protocol and USD row-vector cells are
 * numerically identical. The Godot adapter transposes the basis and removes
 * only the presentation owned by the converted representation.
 */

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <pxr/base/tf/weakBase.h>
#include <pxr/base/tf/notice.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/notice.h>

#include <idtxflow/net/ports/IStageBridge.h>
#include <idtxflow/utils/Logger.h>

#include "TransformCodec.h"

class UsdStageNode3D;

namespace idtxflow
{
namespace collab
{
/// Main-thread USD/Godot adapter owned by one stage node; reports completed raw transform changes.
class StageBridge : public net::ports::IStageBridge, public pxr::TfWeakBase
{
  public:
    /// Which composed layer author_to_usd writes into. SessionLayer is the
    /// in-memory override slot (non-destructive); RootLayer edits the opened
    /// root layer directly. OverrideLayer behaves as SessionLayer.
    enum class EditTarget
    {
        SessionLayer,
        RootLayer,
        OverrideLayer
    };

    /// Borrow the owning node and retain its USD stage for the selected authoring layer.
    StageBridge(UsdStageNode3D* stage_node, pxr::UsdStageRefPtr stage,
                EditTarget edit_target = EditTarget::SessionLayer);
    /// Stop listening to USD changes and cancel remaining change callbacks.
    ~StageBridge() override;

    StageBridge(const StageBridge&) = delete;
    StageBridge& operator=(const StageBridge&) = delete;

    /// The UsdStageNode3D this bridge is bound to (non-owning). Used by the
    /// binding to match a prim node back to its session's bridge.
    UsdStageNode3D* stage_node() const
    {
        return stage_node_;
    }

    /// Whether `prim_path` is this stage's placement root (its defaultPrim,
    /// e.g. "/World"). The root carries display-only placement (MPU scale +
    /// up-axis rotation), so it is excluded from the transform broadcast index.
    /// The node's separate placement-root policy controls explicit local authoring.
    /// Resolved from defaultPrim, so it survives the .scn cache reload.
    bool is_stage_root(const std::string& prim_path) const override;

    // IStageBridge
    /// Rebuild the prim-to-node lookup, keeping the last recorded transforms
    /// for nodes that still have the same ObjectID.
    void build_index() override;
    /// Read the raw local transform in wire matrix form; marked payload carriers
    /// may supply stored placement when no USD operation exists. Return false if unavailable.
    bool read_prim(const std::string& prim_path, net::model::PrimEdit& out) const override;
    /// Read current USD as this node's display transform, including its fixed presentation.
    /// This does not write USD, update placement metadata, or deliver node notifications.
    bool read_display_transform(const std::string& prim_path, godot::Transform3D& out) const;
    /// Finish USD and Godot updates before reporting changed local transforms;
    /// External edits do not produce those callbacks.
    void author_edit(const net::model::PrimEdit& edit, net::ports::EditOrigin origin) override;
    /// Install the callback and stop any remaining deliveries to the old callback.
    void set_on_changed(OnChanged sink) override
    {
        ++dispatch_state_->sink_revision;
        on_changed_ = std::move(sink);
    }
    /// Return whether a change callback is installed.
    bool has_on_changed() const override
    {
        return static_cast<bool>(on_changed_);
    }

  private:
    IDTX_LOG_CATEGORY("StageBridge")

    // Store the node's Godot ObjectID (a stable POD), not a raw pointer.
    // Remember each node's last recorded transform so repeated USD notices
    // do not report the same value again.
    struct Tracked
    {
        uint64_t node_id = 0; // Godot ObjectID; 0 == none
        net::model::Mat4 observed;
        bool has_observation = false;
    };

    // This shared state stays valid if a callback destroys the bridge,
    // allowing remaining callbacks to stop safely.
    struct DispatchState
    {
        bool alive = true;
        bool authoring = false;
        uint64_t sink_revision = 0;
    };

    // Prevent node notifications from starting another edit before
    // the current USD write and Godot node updates finish.
    struct AuthoringScope
    {
        explicit AuthoringScope(std::shared_ptr<DispatchState> state) : state(std::move(state))
        {
            this->state->authoring = true;
        }
        ~AuthoringScope() { state->authoring = false; }
        AuthoringScope(const AuthoringScope&) = delete;
        AuthoringScope& operator=(const AuthoringScope&) = delete;
        std::shared_ptr<DispatchState> state;
    };

    void register_listener();
    void revoke_listener();
    void _on_objects_changed(const pxr::UsdNotice::ObjectsChanged& notice, const pxr::UsdStageWeakPtr& sender);

    // author_edit dispatches on edit.kind to one of these. `external` is true for
    // peer-originated edits (transform re-applies marked visual presentation or
    // payload placement). author_edit prevents external changes from being reported
    // back as local edits; the per-kind helpers do not control this guard.
    // Collect notices until the write finishes so node updates and transform
    // reads use the completed USD state.
    void author_transform(const net::model::PrimEdit& edit, bool external);
    void author_create(const net::model::PrimEdit& edit);
    void author_set_active(const net::model::PrimEdit& edit);

    // Create missing Godot nodes and reconcile retained activity after the USD write.
    // External edits must also update the scene without being sent back out.
    // Only prim resync paths change the node tree; property paths trigger
    // transform checks instead.
    void reconcile_structural(const std::set<pxr::SdfPath>& paths);
    void flush_transform_notifications(const std::string& prim_path);
    // A queued value belongs to one observed representation, even if a callback replaces that path.
    struct ObservedEdit
    {
        net::model::PrimEdit edit;
        uint64_t node_id;
    };
    std::vector<ObservedEdit> finish_changes();
    void dispatch_changes(const std::vector<ObservedEdit>& edits);

    bool author_to_usd(const std::string& prim_path, const godot::Transform3D& xform);
    bool read_prim_transform(const std::string& prim_path, godot::Transform3D& out) const;

    // The spine axis baked into the Godot basis at load time for this prim's
    // marked built-in Cone/Cylinder/Capsule visual, or SpineAxis::None when no
    // built-in spine-axis presentation is established (Plane, unmarked or missing nodes).
    // Keep the type set in sync with StageConverter's Cylinder/Cone/Capsule branches.
    xform::SpineAxis spine_axis_for(const std::string& prim_path) const;

    UsdStageNode3D* stage_node_ = nullptr; // non-owning
    pxr::UsdStageRefPtr stage_;
    EditTarget edit_target_ = EditTarget::SessionLayer;

    std::shared_ptr<DispatchState> dispatch_state_ = std::make_shared<DispatchState>();
    std::set<pxr::SdfPath> pending_resyncs_;
    std::set<std::string> pending_transforms_;

    pxr::TfNotice::Key notice_key_;
    bool listening_ = false;

    std::unordered_map<std::string, Tracked> tracked_; // prim_path -> node
    OnChanged on_changed_;
};

} // namespace collab
} // namespace idtxflow
