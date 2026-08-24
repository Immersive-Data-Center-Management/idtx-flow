#pragma once

/**
 * @file AssetConverter.h
 * @brief Utility for loading USD-referenced assets (e.g. textures) using the openUSD asset resolver,
 * which handles complex cases like .usdz packages and custom resolver plugins.
 */
#include <filesystem>

#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolverContext.h>
#include <pxr/usd/ar/resolverContextBinder.h>
#include <pxr/usd/ar/packageUtils.h>
#include <pxr/usd/usd/stage.h>

#include "../types/TargetTypes.h"
#include "../cache/ResourceCache.h"
#include "TypeConverter.h"

namespace idtxflow
{
namespace converter
{
	template<typename TargetEngine> requires idtxflow::types::ValidTargetEngine<TargetEngine>
	class UsdAssetConverter
	{
	public:
		using Types = idtxflow::types::TargetEngineTypes<TargetEngine>;
		using TypeConverter = UsdTypeConverter<TargetEngine>;

		// Use the openUSD asset resolver to resolve a given asset path. This is a helper that allows us to
		// construct unresolved asset path's at runtime and get them properly resolved in the context of a
		// given stage.
		static pxr::ArResolvedPath ResolvePath(const pxr::SdfAssetPath& assetPath, const pxr::UsdStageWeakPtr& stage)
		{
			pxr::ArResolver &resolver = pxr::ArGetResolver();
			// Bind the stage’s path resolver context so relative/package paths resolve correctly
			pxr::ArResolverContext context = stage ? stage->GetPathResolverContext() : pxr::ArResolverContext();
			pxr::ArResolverContextBinder binder(context);

			// Try resolved path first, fall back to authored asset path
			std::string resolved = assetPath.GetResolvedPath();
			std::string identifier = !resolved.empty() ? resolved : resolver.CreateIdentifier(assetPath.GetAssetPath(), stage->GetRootLayer()->GetResolvedPath());
			if (identifier.empty())
			{
				return pxr::ArResolvedPath();
			}
			
			pxr::ArResolvedPath resolvedPath = resolver.Resolve(identifier);
			if (resolvedPath.GetPathString().empty()) {
				resolvedPath = resolver.Resolve(assetPath.GetAssetPath());
				if (resolvedPath.GetPathString().empty()) {
					return pxr::ArResolvedPath();
				}
			}

			return resolvedPath;
		}

		static std::shared_ptr<pxr::ArAsset> LoadAssetWithResolver(const pxr::SdfAssetPath& assetPath, const pxr::UsdStageWeakPtr& stage)
		{
			pxr::ArResolvedPath resolvedPath = ResolvePath(assetPath, stage);

			// Open the asset with the resolver. This handles files within .usdz packages transparently.
			pxr::ArResolver &resolver = pxr::ArGetResolver();
			return resolver.OpenAsset(resolvedPath);
		}

		static std::optional<typename Types::Texture> LoadTextureWithResolver(
		    const pxr::SdfAssetPath& assetPath,
		    const pxr::UsdStageWeakPtr& stage,
		    const TexturePurpose purpose,
		    cache::UsdResourceCache<TargetEngine>* resourceCache = nullptr)
		{
		    // first check if we have already cached the texture for this asset file
		    if (resourceCache)
		    {
		        if (auto cachedTexture = resourceCache->GetCachedTexture(assetPath.GetAssetPath()); cachedTexture.has_value())
		        {
		            return std::move(cachedTexture.value());
		        }
		    }
		    
			std::shared_ptr<pxr::ArAsset> asset = LoadAssetWithResolver(assetPath, stage);
			if (!asset)
			{
			    // asset references stored in openUSD properties may contain an addition "../" at the beginning that
				// does not mean "parent path" in the common understanding but is defines as "leave the current package".
				// this has been seen especially in usd files used with the omniverse. It's not yet clear how to properly
				// detect this use case and what exactly "package" means in this context. However, as a workaround we check
				// whether the authored path starts with a "../" if the asset could not be found at the original location
				if (assetPath.GetAuthoredPath().find("../") == 0)
				{
					// remove the first "../" and try again
					std::string checkPath = assetPath.GetAuthoredPath().substr(3);
					//_debug(vformat("Try to load image from: %s", check_path.c_str()));
					pxr::ArResolvedPath rp = ResolvePath(pxr::SdfAssetPath(checkPath), stage);
					pxr::SdfAssetPath resolvedPath(checkPath, rp.GetPathString());
					asset = LoadAssetWithResolver(resolvedPath, stage);
					if (!asset)
					{
						return {};
					}
				} else return {};
			}

			std::vector<uint8_t> buffer;
			if (!ReadAssetToBytebuffer(asset, buffer)) return {};

			// extract the file extension from the asset we just loaded
			// Prefer the authored asset path for extension; fall back to package inner path or resolved path.
			std::string path = assetPath.GetAssetPath();
			std::string resolved = assetPath.GetResolvedPath();
			std::string outer, inner;

			// If either asset or resolved looks like a package path, extract inner path (file inside the package).
			if (!path.empty() && pxr::ArIsPackageRelativePath(path)) {
				std::pair<std::string, std::string> split_path = pxr::ArSplitPackageRelativePathInner(path);
			    outer = split_path.first;
			    inner = split_path.second;
			} else if (!resolved.empty() && pxr::ArIsPackageRelativePath(resolved)) {
				std::pair<std::string, std::string> split_path = pxr::ArSplitPackageRelativePathInner(resolved);
			    outer = split_path.first;
			    inner = split_path.second;
			}

			const std::string &for_ext = inner.empty() ? (!path.empty() ? path : resolved) : inner;
			std::filesystem::path p{for_ext};
			// ensure the extension is lowercase
			std::string ext = p.extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(),
						   [](unsigned char c) { return std::tolower(c); });
			
		    auto texture = TypeConverter::toTexture(buffer, ext, purpose);
		    
		    // if a resource cache is provided cache this texture
		    if (resourceCache && texture.has_value())
		    {
		        resourceCache->CacheTexture(assetPath.GetAssetPath(), texture.value());
		    }
		    
		    return texture;
		}

	protected:
		static bool ReadAssetToBytebuffer(const std::shared_ptr<pxr::ArAsset>& asset, std::vector<uint8_t>& outBuffer)
		{
			if (!asset) return false;
			int64_t sz = static_cast<int64_t>(asset->GetSize());
			if (sz <= 0) return false;

			outBuffer.resize(sz, 0);
			uint8_t *dst = outBuffer.data();

			// If memory-mapped, copy directly
			std::shared_ptr<const char> buffer = asset->GetBuffer();
			if (buffer)
			{
				if (const void *buf = static_cast<const void*>(buffer.get())) {
					memcpy(dst, buf, (size_t)sz);
					return true;
				}
			}

			// Otherwise read in chunks
			const size_t chunk = 1 << 20; // 1MB chunks
			size_t off = 0;
			while (off < (size_t)sz) {
				size_t to_read = std::min(chunk, (size_t)sz - off);
				size_t n = asset->Read(dst + off, to_read, off);
				if (n != to_read) return false;
				off += n;
			}
			return true;
		}
	};
}
}