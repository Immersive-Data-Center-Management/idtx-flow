#pragma once

/**
 * @file Types.h
 * @brief The domain model: plain data describing collaborative USD editing over
 *        a network. Engine-, transport- and JSON-agnostic (C++ standard library
 *        only) so it can be shared across every adapter and binding.
 */

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace idtxflow
{
namespace net
{
namespace model
{
    /// A 4x4 transform stored row-major, so element (row r, col c) is m[r*4 + c].
    /// This mirrors the wire protocol's Matrix4dTransform (m00..m33) one-to-one;
    /// it is deliberately distinct from USD's column-major basis, which the stage
    /// bridge transposes on its own boundary.
    struct Mat4
    {
        std::array<double, 16> m{ {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1} };
    };

    /// Translation / rotation / scale expressed separately, matching the wire
    /// SeparateTransform. Rotation is Euler degrees in USD (XYZ) convention.
    struct SeparateXform
    {
        double translation[3]{0, 0, 0};
        double rotation[3]{0, 0, 0};   ///< Euler degrees, USD convention
        double scale[3]{1, 1, 1};
    };

    /// A single collaborative edit to one prim. Today an edit is always a
    /// transform, carried either as a full matrix or as separate T/R/S; the kind
    /// is modelled explicitly so the shape can grow without changing call sites.
    struct PrimEdit
    {
        enum class Kind { Transform };

        Kind          kind = Kind::Transform;
        std::string   prim_path;
        bool          is_matrix = true;   ///< choose matrix vs. separate representation
        Mat4          matrix;
        SeparateXform separate;
        int64_t       timestamp = 0;      ///< millis, stamped when the edit is sent
    };

    /// One entry from the file listing; `filepath` is the key used for sessions
    /// and downloads, the rest is display/sort metadata.
    struct FileEntry
    {
        std::string filepath;    ///< e.g. "scenes/foo.usda"
        std::string filename;    ///< e.g. "foo.usda"
        std::string directory;   ///< e.g. "scenes"
        int64_t     size = 0;
        int64_t     modified = 0;        ///< raw implementation-defined value from the backend; sort key
        int64_t     modified_epoch = 0;  ///< best-effort decode to Unix seconds, or 0 when uninterpretable
    };

    /// Details of an active collaboration session.
    struct SessionInfo
    {
        std::string session_id;
        std::string usd_file;
        std::string mode;            ///< e.g. "single_edit"
        int64_t     client_count = 0;
        int64_t     created_at = 0;
        std::string ws_url;          ///< e.g. "/ws?sid=..." (relative to base)
        std::string protocol;        ///< e.g. "protobuf-binary"
    };

    /// The credentials returned by a successful login.
    struct LoginResult
    {
        std::string access_token;
        std::string token_type = "Bearer";
        int64_t     expires_in = 0;   ///< seconds
        std::string refresh_token;
        std::string scope;
    };

    /// A backend error: the body's { "error", "message" } plus the HTTP status.
    /// An http_code of 0 denotes a transport failure with no response.
    struct RestError
    {
        int         http_code = 0;
        std::string error_code;   ///< snake_case code from the body, or a transport tag
        std::string message;      ///< human-readable message
    };

    /// The outcome of a health probe against the backend.
    struct HealthResult
    {
        bool        ok = false;      ///< reachable and reporting healthy
        int         http_code = 0;   ///< HTTP status (0 = transport failure)
        std::string status;          ///< reported status string (e.g. "ok")
    };

} // namespace model
} // namespace net
} // namespace idtxflow
