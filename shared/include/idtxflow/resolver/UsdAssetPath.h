#pragma once

/**
 * @file UsdAssetPath.h
 * @brief Engine-independent USD identifier anchoring with an explicit layer context.
 *
 * Produces an authorable asset identifier using the layer that gives relative
 * paths their meaning. This supports authoring into a different layer, including
 * an anonymous session layer that has no asset directory of its own.
 *
 * The caller supplies the anchor layer and resolver context. The helper preserves
 * already-anchored identifiers and delegates relative identifier construction to
 * OpenUSD. Asset loading and cache policy remain with the registered resolvers.
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

namespace idtxflow::resolver
{

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
inline std::string AnchorAssetIdentifier(const std::string& asset_path,
                                         const pxr::SdfLayerHandle& anchor_layer,
                                         const pxr::ArResolverContext& resolver_context)
{
    if (detail::IsAnchoredAssetIdentifier(asset_path)) return asset_path;
    if (asset_path.empty() || !anchor_layer || anchor_layer->IsAnonymous() ||
        anchor_layer->GetResolvedPath().IsEmpty()) return {};

    const pxr::ArResolverContextBinder context(resolver_context);
    const std::string anchored = pxr::SdfComputeAssetPathRelativeToLayer(anchor_layer, asset_path);
    return detail::IsAnchoredAssetIdentifier(anchored) ? anchored : std::string();
}

} // namespace idtxflow::resolver
