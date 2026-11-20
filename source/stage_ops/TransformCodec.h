#pragma once

/**
 * @file TransformCodec.h
 * @brief Godot-side adapter between godot::Transform3D and the engine-agnostic
 *        wire model (model::PrimEdit / model::Mat4).
 *
 * This is the engine-specific counterpart to model::ConventionMath: it marshals
 * Godot's Basis rows and origin into the layout the wire helpers expect and back,
 * delegating the actual layout/transpose to ConventionMath so the row-vs-column
 * convention lives in exactly one place.
 *
 * Transform edits and create placement share this conversion. Structural
 * request routing remains outside the transform adapter.
 *
 * Boundary: Godot types are allowed here, but this must stay free of OpenUSD (pxr).
 * StageBridge copies its protocol matrices directly into GfMatrix4d.
 */

#include <string>

#include <godot_cpp/variant/transform3d.hpp>

#include <idtxflow/net/model/Types.h>

namespace idtxflow
{
namespace collab
{
namespace xform
{
/// Godot Transform3D -> row-major wire PrimEdit (matrix form). Basis column i
/// maps to wire row i; translation goes on the bottom row.
net::model::PrimEdit transform_to_prim_edit(const std::string& prim_path, const godot::Transform3D& t);

/// In-place variant of transform_to_prim_edit for callers that fill an
/// existing PrimEdit.
void transform_to_prim_edit(const std::string& prim_path, const godot::Transform3D& t, net::model::PrimEdit& out);

/// Row-major wire Mat4 -> Godot Transform3D. Inverse of the matrix packing
/// above (basis columns from wire rows, translation from the bottom row).
godot::Transform3D mat4_to_transform(const net::model::Mat4& mm);

/// SeparateXform (T/R/S, rotation in Euler degrees USD convention) -> Godot
/// Transform3D.
/// The column-vector basis for USD rotateXYZ is Rz * Ry * Rx * Scale.
godot::Transform3D separate_to_transform(const net::model::SeparateXform& s);

/// Resolve an explicit (is_matrix, matrix, separate) triple to a Godot
/// Transform3D, choosing the matrix or separate branch by `is_matrix`. The
/// single composition point reused by both a transform edit and a create's
/// placement, so the matrix/separate decode stays single-sourced.
godot::Transform3D to_transform(bool is_matrix, const net::model::Mat4& matrix,
                                const net::model::SeparateXform& separate);

/// PrimEdit -> Godot Transform3D, choosing the matrix or separate branch by
/// PrimEdit::is_matrix.
godot::Transform3D prim_edit_to_transform(const net::model::PrimEdit& e);

/// The spine axis a USD Cone/Cylinder/Capsule "grows" along. Mirrors the load-time
/// bake in UsdGodotTypeConverter::toTransform: the converter post-rotates the
/// Godot basis so the mesh's local +Y points along the authored USD axis, because these Godot primitive
/// meshes are Y-spined while USD's default is Z but authored prims may
/// pick X or Y. This enum lets the collaboration path invert that same bake.
/// Collaboration applies this correction only to marked built-in visuals.
/// None denotes no built-in spine-axis correction.
enum class SpineAxis
{
    None,
    X,
    Y,
    Z
};

/// Re-apply the load-time spine-axis rotation to a Godot basis (the presentation
/// rotation UsdGodotTypeConverter::toTransform bakes in). Identity for Y/None.
/// Used on inbound: the wire/USD basis is raw (unrotated), and the live
/// Godot node must carry the presentation rotation to match the imported prim.
godot::Basis apply_spine_axis(const godot::Basis& basis, SpineAxis axis);

/// Strip the load-time spine-axis rotation from a Godot basis (inverse of
/// apply_spine_axis). Identity for Y/None. Used on outbound / before authoring
/// to USD: the Godot node basis carries the presentation rotation, which must
/// be removed so USD stores the raw (unrotated) orientation.
godot::Basis strip_spine_axis(const godot::Basis& basis, SpineAxis axis);

} // namespace xform
} // namespace collab
} // namespace idtxflow
