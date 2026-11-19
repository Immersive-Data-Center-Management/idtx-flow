#pragma once

/**
 * @file PayloadPlacement.h
 * @brief Keep a built-in Godot payload representation consistent with its raw USD placement.
 *
 * The built-in converter represents an unloaded payload with a carrier UsdStageNode3D and a
 * separately converted asset root. Editable placement belongs to the carrier so moving the
 * asset cannot apply its source or creation transform a second time through the nested root.
 *
 * For supported roots, the nested root retains only its fixed coordinate conversion.
 * Raw/display helpers keep Godot's up-axis and unit adjustments out of authored USD transforms.
 * Placement state and nested USD overrides follow local and external edits. Deferred conversion
 * completion uses the holder's current transform in its containing USD stage when available.
 *
 * Placement handling supports one nonanimated built-in UsdXformNode3D matching the source stage's
 * Xform default prim; that root may contain many descendants. Other root representations keep
 * their ordinary conversion behavior. Synchronizing edits to individual payload descendants is
 * outside this module's scope.
 *
 * Operations borrow nodes on the main thread; serialized representation state stays on the node.
 * StageBridge coordinates edits, while the owning node supplies loading and attachment timing.
 * Initialization and external presentation updates suppress automatic transform authoring;
 * local edits record state without consuming Godot's pending transform notifications.
 */

#include <vector>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/transform3d.hpp>

class UsdStageNode3D;
namespace godot
{
class Node3D;
}

namespace idtxflow::collab::payload
{
/// Serialized state belongs only to a built-in payload carrier, not an arbitrary nested stage.
inline constexpr const char* metadata_key = "_idtx_payload_placement";
inline constexpr int representation_version = 1;

/// Suppress automatic node-transform authoring during coordinated payload presentation updates.
class PresentationScope
{
  public:
    PresentationScope();
    ~PresentationScope();
    PresentationScope(const PresentationScope&) = delete;
    PresentationScope& operator=(const PresentationScope&) = delete;
};

/// All affected owning stages share the guard during a coordinated presentation update.
bool presentation_update_active();

/// Return carrier metadata with the supported version, or an empty dictionary when absent or incompatible.
/// The version check does not establish conversion readiness.
godot::Dictionary state(const godot::Node3D* node);

/// Mark a built-in payload candidate with identity stage basis before source conversion.
void mark_carrier(godot::Node3D* node);

/// Capture the containing stage's fixed bake during conversion, before user placement is applied.
void record_stage_basis(godot::Node3D* node);

// Payload presentation helpers run on the main thread.
// Raw placement uses Godot Transform3D values without scene presentation.
/// Identify a versioned built-in payload carrier, including one awaiting source conversion.
/// This tests metadata presence/version, not completion of the converted root.
bool is_builtin_payload_carrier(const UsdStageNode3D& node);
/// Read the stored raw placement after supported default-root initialization.
/// Return false without changing raw when placement is unavailable.
bool get_payload_placement(const UsdStageNode3D& node, godot::Transform3D& raw);
/// Convert raw USD placement to this carrier's fixed coordinate presentation.
/// Return raw unchanged when placement is unavailable.
godot::Transform3D payload_to_display(const UsdStageNode3D& node, const godot::Transform3D& raw);
/// Remove only fixed presentation; user scale is never inverted.
/// Return display unchanged when placement is unavailable.
godot::Transform3D payload_from_display(const UsdStageNode3D& node, const godot::Transform3D& display);
/// Record raw metadata and the optional nested USD override without setting or flushing node transforms.
/// Write the override only when author_override is true and placement is ready; otherwise update metadata only.
void record_payload_placement(UsdStageNode3D& node, const godot::Transform3D& raw, bool author_override = true);
/// Apply carrier/root display only, without writing USD, metadata or consuming transform notifications.
/// The caller consumes affected notifications later under PresentationScope at a safe boundary.
void apply_payload_presentation(UsdStageNode3D& node, const godot::Transform3D& raw);
/// Apply guarded presentation and record raw state; author_override=false leaves the nested USD override unchanged.
/// Use for external updates and initialization; local transform notifications use record_payload_placement.
void update_payload_placement(UsdStageNode3D& node, const godot::Transform3D& raw, bool author_override = true);

/// Normalize one nonanimated built-in Xform default root, preferring the current containing-stage opinion.
/// Fall back to composed source placement when that opinion is unavailable.
/// Unsupported root representations lose the carrier marker.
void finalize_payload_placement(UsdStageNode3D& node, const std::vector<godot::Node3D*>& roots);

} // namespace idtxflow::collab::payload
