#pragma once

/**
 * @file GodotTransformCodec.h
 * @brief Godot-side adapter between godot::Transform3D and the engine-agnostic
 *        wire model (model::PrimEdit / model::Mat4).
 *
 * This is the engine-specific counterpart to model::ConventionMath: it marshals
 * Godot's Basis rows and origin into the layout the wire helpers expect and back,
 * delegating the actual layout/transpose to ConventionMath so the row-vs-column
 * convention lives in exactly one place.
 *
 * Scope: transform edits only. A new edit kind would get its own sibling codec rather than
 * being folded in here.
 *
 * Boundary: Godot types are allowed here, but this must stay free of OpenUSD (pxr). The only 
 * place Godot and pxr meet is GodotStageBridge, which builds its GfMatrix4d on top of these helpers.
 */

#include <string>

#include <godot_cpp/variant/transform3d.hpp>

#include <idtxflow/net/model/Types.h>

namespace idtxflow
{
namespace collab_godot
{
namespace xform
{
    /// Godot Transform3D -> row-major wire PrimEdit (matrix form). Basis row i
    /// maps to wire row i; translation goes on the bottom row.
    net::model::PrimEdit transform_to_prim_edit(const std::string& prim_path,
                                                const godot::Transform3D& t);

    /// In-place variant of transform_to_prim_edit for callers that fill an
    /// existing PrimEdit.
    void transform_to_prim_edit(const std::string& prim_path, const godot::Transform3D& t,
                                net::model::PrimEdit& out);

    /// Row-major wire Mat4 -> Godot Transform3D. Inverse of the matrix packing
    /// above (basis rows from wire rows, translation from the bottom row).
    godot::Transform3D mat4_to_transform(const net::model::Mat4& mm);

    /// SeparateXform (T/R/S, rotation in Euler degrees USD convention) -> Godot
    /// Transform3D.
    godot::Transform3D separate_to_transform(const net::model::SeparateXform& s);

    /// PrimEdit -> Godot Transform3D, choosing the matrix or separate branch by
    /// PrimEdit::is_matrix.
    godot::Transform3D prim_edit_to_transform(const net::model::PrimEdit& e);

} // namespace xform
} // namespace collab_godot
} // namespace idtxflow
