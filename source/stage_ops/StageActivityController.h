#pragma once

/**
 * @file StageActivityController.h
 * @brief Retain converted runtime content while USD activity controls local interaction.
 *
 * One controller belongs to each stage node. It evaluates USD and containing-payload
 * blockers, owns reversible runtime settings, and corrects rejected presentation
 * edits without authoring USD. Nested stages own their contents independently.
 *
 * The owning stage wires conversion, USD notices and transform notifications into
 * this policy; runtime use requires no GDScript setup or editor plugin. Script
 * consumers can query activity and observe its signal when updating their own UI.
 * StageEditController and StageBridge consult the policy before local transform
 * authoring and outgoing callbacks, while external USD updates remain accepted.
 *
 * Inactive content keeps its nodes while visibility, processing and collision-object
 * ray picking are suspended. Collision objects leave the physics simulation, and
 * descendants using PROCESS_MODE_ALWAYS are disabled explicitly. Restoration reads
 * current live USD before restoring saved runtime settings, without requesting a
 * snapshot or reloading a file. Display corrections run at a deferred boundary
 * and validate stage generation and node identity before touching retained content.
 * This policy does not intercept arbitrary editor Scene-tree operations.
 *
 * All operations run on the Godot main thread.
 */

#include <cstdint>
#include <memory>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

class UsdStageNode3D;
namespace godot
{
class Node;
class Node3D;
} // namespace godot

namespace idtxflow::collab
{
/// Stage-owned runtime activity policy; Godot presentation and USD mutation stay separate.
class StageActivityController
{
  public:
    /// Borrow the stage through its ObjectID; no converted node pointer is retained.
    explicit StageActivityController(UsdStageNode3D& owner);
    /// Revoke pending work without retaining or deleting any converted nodes.
    ~StageActivityController();
    StageActivityController(const StageActivityController&) = delete;
    StageActivityController& operator=(const StageActivityController&) = delete;

    /// Return status, blocker_stage_id, blocker_path and reason without loading or writing USD.
    /// require_tree=false also inspects retained USD during temporary tree exit for authoritative delivery.
    godot::Dictionary query(const godot::String& prim_path, bool require_tree = true) const;
    /// Only an active, available prim is eligible for a supported local transform edit.
    bool allows_local(const godot::String& prim_path) const;
    /// Correct a rejected edit at a deferred boundary after validating its current representation.
    void reject_node(godot::Node3D* node);
    /// Remember accepted display-only placement; authoritative=true adopts USD even when its value is unchanged.
    /// Neither mode writes USD or raw payload state.
    void confirm_node(godot::Node3D* node, bool authoritative = false);
    /// Suspend blocked content now and reconcile presentation/restoration before deferred notification.
    void reconcile();
    /// Seed converted display and suspend inherited inactive content before tree entry.
    void prepare_subtree(godot::Node* root, godot::Node* parent);
    /// Suspend newly attached descendants before their first processing frame.
    void node_added(godot::Node* node);
    /// Revalidate retained state after normal tree entry without discarding saved runtime settings.
    void enter_tree();
    /// Revoke pending deliveries while retaining original settings for normal tree re-entry.
    void exit_tree();
    /// Discard obsolete state when the underlying stage is replaced or reloaded.
    void reset();
    /// Complete owner-deferred work only when its generation still belongs to this stage lifetime.
    void flush(uint64_t generation);

  private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace idtxflow::collab
