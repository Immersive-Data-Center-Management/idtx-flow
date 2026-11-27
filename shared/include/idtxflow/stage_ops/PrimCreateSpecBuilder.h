#pragma once

/**
 * @file PrimCreateSpecBuilder.h
 * @brief Read one USD prim into owned, reusable creation data.
 *
 * Preserves raw local placement and typed defaults, with layer-relative asset
 * provenance and explicit representability checks. Does not author or load USD.
 */
#include <idtxflow/net/model/Types.h>
#include <pxr/usd/usd/stage.h>

namespace idtxflow::stage_ops
{
/// Creation fields detached from the source prim and its stage lifetime.
struct PrimCreateSpec
{
    std::string type;
    std::string reference_path;
    bool payload = false;
    bool has_transform = false;
    net::model::Mat4 transform;
    std::vector<net::model::PrimAttribute> attributes;
};

/// Local capture outcome. Failure leaves spec empty; warnings describe exclusions.
struct PrimCreateSpecResult
{
    bool ok = false;
    std::string error;
    std::string message;
    PrimCreateSpec spec;
    std::vector<std::string> warnings;
};

/// Stateless single-prim inspection, independent of session and clipboard state.
class PrimCreateSpecBuilder
{
  public:
    /// Capture an active defined prim at default time without mutation or payload loading.
    /// Asset identities are prepared for reuse within the same stage root context.
    static PrimCreateSpecResult Build(const pxr::UsdStageRefPtr& stage, const pxr::SdfPath& path);
};
} // namespace idtxflow::stage_ops
