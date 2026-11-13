#pragma once

/**
 * @file ConventionMath.h
 * @brief The two distinct 4x4 matrix conventions used by the client, expressed
 *        as pure functions on model::Mat4 so the relationship between them has a
 *        single source of truth.
 *
 * Two conventions coexist and must stay distinct:
 *   - Wire: row-major, stored exactly as sent/received (the codec copies it
 *     verbatim). This is model::Mat4's own layout.
 *   - Engine: an engine Basis is typically column-vector (v' = M * v), while
 *     OpenUSD's GfMatrix4d and the wire use row-vector math (v' = v * M).
 *     The wire/USD matrix that reproduces a given orientation has the engine's
 *     rotation/scale 3x3 block transposed; the translation values are unchanged.
 *
 * These helpers hold that transpose relationship independently of OpenUSD and
 * the engine, so the transform codec reuses them and tests can verify wire, USD,
 * and cross-convention round-trips are all identity without a live stage.
 */

#include <idtxflow/net/model/Types.h>

namespace idtxflow
{
namespace net
{
namespace model
{
/// Preserve every protocol cell when supplying a USD row-vector matrix.
inline Mat4 usd_from_wire(const Mat4& wire)
{
    return wire;
}

/// Preserve every USD cell when reporting a protocol row-vector matrix.
/// Kept as a named function for call-site clarity.
inline Mat4 wire_from_usd(const Mat4& usd)
{
    return usd;
}

/// Compose a wire matrix from an engine transform's basis rows and origin.
/// `basis_rows` is 9 values in row order (row0 xyz, row1 xyz, row2 xyz), each
/// column placed on the matching wire row; `origin` (xyz) goes on the bottom row
/// (m30..m32), which is the translation cell the backend consumes. This is the
/// single source of truth for the engine<->wire layout so every binding shares
/// one placement instead of re-deriving it.
inline Mat4 wire_from_basis_origin(const double (&basis_rows)[9], const double (&origin)[3])
{
    Mat4 out;
    auto& m = out.m;
    m[0] = basis_rows[0];
    m[1] = basis_rows[3];
    m[2] = basis_rows[6];
    m[3] = 0.0;
    m[4] = basis_rows[1];
    m[5] = basis_rows[4];
    m[6] = basis_rows[7];
    m[7] = 0.0;
    m[8] = basis_rows[2];
    m[9] = basis_rows[5];
    m[10] = basis_rows[8];
    m[11] = 0.0;
    m[12] = origin[0];
    m[13] = origin[1];
    m[14] = origin[2];
    m[15] = 1.0;
    return out;
}

/// Inverse of wire_from_basis_origin: read the basis rows and the bottom-row
/// translation back out of a wire matrix.
/// Transposition requires no matrix inverse, including for mirrored or zero scale.
inline void basis_origin_from_wire(const Mat4& wire, double (&basis_rows)[9], double (&origin)[3])
{
    const auto& m = wire.m;
    basis_rows[0] = m[0];
    basis_rows[1] = m[4];
    basis_rows[2] = m[8];
    basis_rows[3] = m[1];
    basis_rows[4] = m[5];
    basis_rows[5] = m[9];
    basis_rows[6] = m[2];
    basis_rows[7] = m[6];
    basis_rows[8] = m[10];
    origin[0] = m[12];
    origin[1] = m[13];
    origin[2] = m[14];
}

} // namespace model
} // namespace net
} // namespace idtxflow
