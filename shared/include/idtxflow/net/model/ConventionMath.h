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
 *   - USD: OpenUSD's GfMatrix4d uses row-vector math (v' = v * M) while an engine
 *     Basis is typically column-vector (v' = M * v), so the USD matrix that
 *     reproduces a given orientation is the wire matrix with its rotation/scale
 *     3x3 block transposed; the translation row is unchanged.
 *
 * These helpers hold that transpose relationship independently of OpenUSD and
 * the engine, so the stage bridge reuses them and tests can verify wire, USD,
 * and cross-convention round-trips are all identity without a live stage.
 */

#include <idtxflow/net/model/Types.h>

namespace idtxflow
{
namespace net
{
namespace model
{
    /// Convert a wire matrix (row-major) to the USD convention: transpose the
    /// upper-left 3x3 block; leave the translation row (indices 12..14) as is.
    inline Mat4 usd_from_wire(const Mat4& wire)
    {
        const auto& w = wire.m;
        Mat4 out;
        auto& u = out.m;
        // Transposed 3x3 rotation/scale block.
        u[0] = w[0]; u[1] = w[4]; u[2]  = w[8];  u[3]  = w[3];
        u[4] = w[1]; u[5] = w[5]; u[6]  = w[9];  u[7]  = w[7];
        u[8] = w[2]; u[9] = w[6]; u[10] = w[10]; u[11] = w[11];
        // Translation row and homogeneous row unchanged.
        u[12] = w[12]; u[13] = w[13]; u[14] = w[14]; u[15] = w[15];
        return out;
    }

    /// Inverse of usd_from_wire: transpose the 3x3 block back. (The transpose is
    /// its own inverse, so this mirrors usd_from_wire; kept as a named function
    /// for call-site clarity.)
    inline Mat4 wire_from_usd(const Mat4& usd)
    {
        const auto& u = usd.m;
        Mat4 out;
        auto& w = out.m;
        w[0] = u[0]; w[1] = u[4]; w[2]  = u[8];  w[3]  = u[3];
        w[4] = u[1]; w[5] = u[5]; w[6]  = u[9];  w[7]  = u[7];
        w[8] = u[2]; w[9] = u[6]; w[10] = u[10]; w[11] = u[11];
        w[12] = u[12]; w[13] = u[13]; w[14] = u[14]; w[15] = u[15];
        return out;
    }

    /// Compose a wire matrix from an engine transform's basis rows and origin.
    /// `basis_rows` is 9 values in row order (row0 xyz, row1 xyz, row2 xyz), each
    /// row placed on the matching wire row; `origin` (xyz) goes on the bottom row
    /// (m30..m32), which is the translation cell the backend consumes. This is the
    /// single source of truth for the engine<->wire layout so every binding shares
    /// one placement instead of re-deriving it.
    inline Mat4 wire_from_basis_origin(const double (&basis_rows)[9], const double (&origin)[3])
    {
        Mat4 out;
        auto& m = out.m;
        m[0]  = basis_rows[0]; m[1]  = basis_rows[1]; m[2]  = basis_rows[2]; m[3]  = 0.0;
        m[4]  = basis_rows[3]; m[5]  = basis_rows[4]; m[6]  = basis_rows[5]; m[7]  = 0.0;
        m[8]  = basis_rows[6]; m[9]  = basis_rows[7]; m[10] = basis_rows[8]; m[11] = 0.0;
        m[12] = origin[0];     m[13] = origin[1];     m[14] = origin[2];     m[15] = 1.0;
        return out;
    }

    /// Inverse of wire_from_basis_origin: read the basis rows and the bottom-row
    /// translation back out of a wire matrix.
    inline void basis_origin_from_wire(const Mat4& wire, double (&basis_rows)[9], double (&origin)[3])
    {
        const auto& m = wire.m;
        basis_rows[0] = m[0]; basis_rows[1] = m[1]; basis_rows[2] = m[2];
        basis_rows[3] = m[4]; basis_rows[4] = m[5]; basis_rows[5] = m[6];
        basis_rows[6] = m[8]; basis_rows[7] = m[9]; basis_rows[8] = m[10];
        origin[0] = m[12]; origin[1] = m[13]; origin[2] = m[14];
    }

} // namespace model
} // namespace net
} // namespace idtxflow
