#pragma once

#include <pxr/usd/ar/defaultResolver.h>
#include <pxr/usd/ar/defineResolver.h>
#include <pxr/base/tf/staticTokens.h>

PXR_NAMESPACE_OPEN_SCOPE

TF_DECLARE_PUBLIC_TOKENS(GodotResolverTokens,
    ((resScheme, "res"))
    ((userScheme, "user"))
);

/**
 * The GodotAssetResolver enables openUSD to resolve asset path's that uses the Godot uri-scheme "res://" or "user://".
 * 
 * The custom resolver has to "live" within the pxr namespace and also requires to provide a "plugin.json" file in a
 * subfolder of the one, the usd library is located at.
 */
class UsdGodotAssetResolver: public ArDefaultResolver
{
public:
    UsdGodotAssetResolver() = default;
    ~UsdGodotAssetResolver() = default;
    
protected:
    // Core resolver interface methods
    std::string _GetExtension(const std::string& path) const override;
    std::string _CreateIdentifier(const std::string& assetPath, const ArResolvedPath& anchorAssetPath) const override;
    std::string _CreateIdentifierForNewAsset(const std::string& assetPath, const ArResolvedPath& anchorAssetPath) const override;

    // --- Resolution ---
    // This is the core method. It turns an identifier into a resolved path that the system can use to open the asset.
    // Resolving usually means, replacing place-holders with actual values
    ArResolvedPath _Resolve(const std::string& assetPath) const override;
    ArResolvedPath _ResolveForNewAsset(const std::string& assetPath) const override;

    std::shared_ptr<ArAsset> _OpenAsset(const ArResolvedPath& resolvedPath) const override;
    std::shared_ptr<ArWritableAsset> _OpenAssetForWrite(const ArResolvedPath& resolvedPath, WriteMode writeMode) const override;
    
};

PXR_NAMESPACE_CLOSE_SCOPE
