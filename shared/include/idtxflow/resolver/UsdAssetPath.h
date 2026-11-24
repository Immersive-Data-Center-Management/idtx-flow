#pragma once

/**
 * @file UsdAssetPath.h
 * @brief Engine-independent USD asset-target parsing and identifier anchoring.
 *
 * Produces an authorable asset identifier using the layer that gives relative
 * paths their meaning. This supports authoring into a different layer, including
 * an anonymous session layer that has no asset directory of its own.
 *
 * The caller supplies the anchor layer and resolver context. The helper preserves
 * already-anchored identifiers and delegates relative identifier construction to
 * OpenUSD. Asset loading and cache policy remain with the registered resolvers.
 *
 * Separates an optional <absolute-prim> suffix from an asset identifier so only
 * the asset is anchored. The source prim is supplied separately to the USD
 * reference or payload arc.
 *
 * Uses only OpenUSD and the C++ standard library so different engine adapters can
 * reuse the same anchoring behavior.
 */

#include <string>

#include <pxr/base/tf/pathUtils.h>
#include <pxr/usd/ar/resolverContext.h>
#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/sdf/path.h>

namespace idtxflow::resolver
{

/** The asset identifier and optional source prim for reference or payload authoring. */
struct ParsedAssetTarget
{
    std::string asset_identifier;
    pxr::SdfPath target_prim; ///< Empty when the caller omitted the target suffix.
};

/**
 * Parse an asset identifier with an optional <absolute-prim> suffix.
 * Input format: asset_path</Prim/Path>, without USDA @ delimiters.
 * Parsing does not resolve assets or modify USD.
 *
 * The optional suffix must be an absolute prim path; an absent suffix preserves
 * USD's defaultPrim selection. Leading and trailing whitespace is trimmed from
 * the asset identifier and target. A trailing '>' introduces a suffix; other
 * identifier contents remain the resolver's concern.
 *
 * @param value Combined asset identifier and optional absolute prim target.
 * @param result Parsed fields on success; cleared on failure.
 * @return Whether the input has an asset and a valid optional target.
 */
inline bool ParseAssetTarget(const std::string& value, ParsedAssetTarget& result)
{
    result = {};
    const auto trim = [](const std::string& text) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return std::string();
        return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    };

    std::string asset = trim(value);
    pxr::SdfPath target;
    if (!asset.empty() && asset.back() == '>')
    {
        const auto opening = asset.rfind('<');
        if (opening == std::string::npos) return false;
        const std::string prim = trim(asset.substr(opening + 1, asset.size() - opening - 2));
        if (!pxr::SdfPath::IsValidPathString(prim)) return false;
        target = pxr::SdfPath(prim);
        if (!target.IsAbsolutePath() || !target.IsPrimPath()) return false;
        asset = trim(asset.substr(0, opening));
    }
    if (asset.empty()) return false;

    result.asset_identifier = asset;
    result.target_prim = target;
    return true;
}

namespace detail
{
// Absolute filesystem paths and resolver URIs do not depend on an authoring layer.
inline bool IsAnchoredAssetIdentifier(const std::string& path)
{
    return !path.empty() && (!pxr::TfIsRelativePath(path) || path.find("://") != std::string::npos);
}
} // namespace detail

/**
 * Obtain an authorable asset identifier relative to the supplied layer.
 *
 * Absolute filesystem paths and scheme:// identifiers are preserved verbatim.
 * Relative inputs require a non-anonymous layer with a resolved path. The scoped
 * resolver context preserves the caller's binding; no layers or edit targets are
 * modified. An anchored identifier does not guarantee that the asset exists.
 *
 * @param asset_path Authored asset path or an already-anchored identifier.
 * @param anchor_layer Layer whose location gives relative paths their meaning;
 *                     only required for relative inputs.
 * @param resolver_context Context used for the anchoring operation.
 * @return The anchored identifier, or an empty string for empty input, an unusable
 *         anchor, or a result that remains unanchored.
 */
inline std::string AnchorAssetIdentifier(const std::string& asset_path, const pxr::SdfLayerHandle& anchor_layer,
                                         const pxr::ArResolverContext& resolver_context)
{
    if (detail::IsAnchoredAssetIdentifier(asset_path)) return asset_path;
    if (asset_path.empty() || !anchor_layer || anchor_layer->IsAnonymous() || anchor_layer->GetResolvedPath().IsEmpty())
        return {};

    const pxr::ArResolverContextBinder context(resolver_context);
    const std::string anchored = pxr::SdfComputeAssetPathRelativeToLayer(anchor_layer, asset_path);
    return detail::IsAnchoredAssetIdentifier(anchored) ? anchored : std::string();
}

} // namespace idtxflow::resolver
