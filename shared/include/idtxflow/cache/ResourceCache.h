#pragma once

#include <map>
#include <optional>
#include <shared_mutex>

#include <pxr/usd/sdf/path.h>

#include "../types/TargetTypes.h"

namespace idtxflow
{
namespace cache
{
    template<typename TargetEngine> requires types::ValidTargetEngine<TargetEngine>
    class UsdResourceCache
    {
    public:
        using Types = types::TargetEngineTypes<TargetEngine>;
        
        void CacheTexture(const std::string& path, const typename Types::Texture& texture)
        {
            TextureCache.emplace(path, std::move(texture));
        }
        
        std::optional<const typename Types::Texture> GetCachedTexture(const std::string& path)
        {
            std::shared_lock lock(mutex_);
            if (TextureCache.contains(path))
                return TextureCache.at(path); // copy, not move
            return std::nullopt;
        }
        
        void CacheMaterial(const std::string& path, const typename Types::Material& material)
        {
            MaterialCache.emplace(path, std::move(material));
        }
        
        bool HasCachedMaterial(const std::string& path)
        {
            return MaterialCache.contains(path);
        }
        
        std::optional<const typename Types::Material> GetCachedMaterial(const std::string& path)
        {
            std::shared_lock lock(mutex_);
            if (MaterialCache.contains(path))
                return MaterialCache.at(path);
            
            return std::nullopt;
        }
        
        void ClearCache()
        {
            TextureCache.clear();
            MaterialCache.clear();
        }     
    private:
        std::shared_mutex mutex_;
        std::map<std::string, typename Types::Texture> TextureCache;
        std::map<std::string, typename Types::Material> MaterialCache;
    };
}
}