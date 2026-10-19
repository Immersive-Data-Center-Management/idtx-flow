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
    std::array<double, 16> m{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
};

/// Translation / rotation / scale expressed separately, matching the wire
/// SeparateTransform. Rotation is Euler degrees in USD (XYZ) convention.
struct SeparateXform
{
    double translation[3]{0, 0, 0};
    double rotation[3]{0, 0, 0}; ///< Euler degrees, USD convention
    double scale[3]{1, 1, 1};
};

/// A single collaborative edit to one prim. An edit is either a transform
/// (carried as a full matrix or separate T/R/S), the creation of a built-in
/// prim, or an active-state change (soft delete / undo). The kind is modelled
/// explicitly so the shape can grow without changing call sites.
struct PrimEdit
{
    enum class Kind
    {
        Transform,
        CreatePrim, ///< author a built-in prim of `prim_type` at `prim_path`
        SetActive   ///< author `active` metadata on `prim_path`
    };

    Kind kind = Kind::Transform;
    std::string prim_path;

    // --- Transform ---
    bool is_matrix = true; ///< choose matrix vs. separate representation
    Mat4 matrix;
    SeparateXform separate;
    int64_t timestamp = 0; ///< millis, stamped when the edit is sent

    // --- CreatePrim ---
    /// USD schema type token, e.g. "Cube"/"Sphere"/"Cone"/"Cylinder"/
    /// "Capsule"/"Xform". Empty for non-create edits.
    std::string prim_type;
    // Optional built-in shape defaults the server authored (has-field semantics
    // mirror the wire's proto3 optional fields). Only the ones that apply to the
    // type are set: Cube -> size; Sphere -> radius; Cone/Cylinder/Capsule ->
    // height, radius, axis; Xform -> none.
    bool has_size = false;
    double size = 0.0;
    bool has_height = false;
    double height = 0.0;
    bool has_radius = false;
    double radius = 0.0;
    bool has_axis = false;
    std::string axis; ///< "X"/"Y"/"Z"

    // --- SetActive ---
    bool active = true; ///< target active state (false = soft delete)
};

/// One entry from the file listing; `filepath` is the key used for sessions
/// and downloads, the rest is display/sort metadata.
struct FileEntry
{
    std::string filepath;  ///< e.g. "scenes/foo.usda"
    std::string filename;  ///< e.g. "foo.usda"
    std::string directory; ///< e.g. "scenes"
    int64_t size = 0;
    int64_t modified = 0;       ///< raw implementation-defined value from the backend; sort key
    int64_t modified_epoch = 0; ///< best-effort decode to Unix seconds, or 0 when uninterpretable
};

/// Details of an active collaboration session.
struct SessionInfo
{
    std::string session_id;
    std::string usd_file;
    std::string mode; ///< e.g. "single_edit"
    int64_t client_count = 0;
    int64_t created_at = 0;
    std::string ws_url;   ///< e.g. "/ws?sid=..." (relative to base)
    std::string protocol; ///< e.g. "protobuf-binary"
};

/// The outcome of committing a session's overrides back to the USD file.
struct CommitResult
{
    std::string session_id;
    bool committed = false;
};

/// The credentials returned by a successful login.
struct LoginResult
{
    std::string access_token;
    std::string token_type = "Bearer";
    int64_t expires_in = 0; ///< seconds
    std::string refresh_token;
    std::string scope;
};

/// A backend error: the body's { "error", "message" } plus the HTTP status.
/// An http_code of 0 denotes a transport failure with no response.
struct RestError
{
    int http_code = 0;
    std::string error_code; ///< snake_case code from the body, or a transport tag
    std::string message;    ///< human-readable message
};

/// The outcome of a health probe against the backend.
struct HealthResult
{
    bool ok = false;    ///< reachable and reporting healthy
    int http_code = 0;  ///< HTTP status (0 = transport failure)
    std::string status; ///< reported status string (e.g. "ok")
};

/// The bytes and content-type of a fetched thumbnail image.
struct ThumbnailResult
{
    std::string bytes;        ///< raw image data (PNG/JPEG)
    std::string content_type; ///< e.g. "image/png" (may be empty)
};

} // namespace model
} // namespace net
} // namespace idtxflow
