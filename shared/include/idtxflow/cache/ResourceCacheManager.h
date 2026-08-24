#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "ResourceCache.h"

namespace idtxflow::cache
{
    template<typename TargetEngine> requires types::ValidTargetEngine<TargetEngine>
    class UsdResourceCacheManager {
    public:
        static UsdResourceCacheManager& Instance()
        {
            // Meyers singleton: thread-safe initialization guaranteed by the C++ standard.
            static UsdResourceCacheManager instance;
            return instance;
        }

        static std::string MakeStageCacheKey(const std::string& uri, const std::string& overrideContent = std::string())
        {
            const std::hash<std::string> hasher;
            if (overrideContent.empty())
                return std::to_string(hasher(uri));
            return std::to_string(hasher(uri)) + "_" + std::to_string(hasher(overrideContent));
        }

        // Returns an existing cache for `key` if one is still alive, else creates one.
        std::shared_ptr<UsdResourceCache<TargetEngine>> Acquire(const std::string& key) {
            std::lock_guard lock(mutex_);
            auto& weak = caches_[key];
            if (auto existing = weak.lock()) return existing;
            auto created = std::make_shared<UsdResourceCache<TargetEngine>>();
            weak = created;
            return created;
        }

        // Erase expired weak_ptr entries (optional housekeeping).
        void Sweep()
        {
            std::lock_guard lock(mutex_);
            for (auto it = caches_.begin(); it != caches_.end(); )
            {
                if (it->second.expired())
                    it = caches_.erase(it);
                else
                    ++it;
            }
        }
    private:
        UsdResourceCacheManager() = default;
        ~UsdResourceCacheManager() = default;
        UsdResourceCacheManager(const UsdResourceCacheManager&) = delete;
        UsdResourceCacheManager& operator=(const UsdResourceCacheManager&) = delete;

        std::mutex mutex_;
        std::map<std::string, std::weak_ptr<UsdResourceCache<TargetEngine>>> caches_;
    };
}
