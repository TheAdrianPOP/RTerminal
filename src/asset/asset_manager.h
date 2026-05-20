#pragma once
#ifndef ASSET_MANAGER_H
#define ASSET_MANAGER_H

// Reference-counted asset manager.
// Assets are loaded by path, cached by StringID(path).
// Unloaded when ref-count drops to zero.
//
// Usage:
//   auto tex = assets.load<Texture>("sprites/hero.png");
//   // tex is a Handle<Texture> — smart ref-counted wrapper
//   // asset unloaded automatically when last handle is destroyed

#include "../core/string_id.h"
#include "../core/log.h"
#include "../core/ref_handle.h"
#include "../math/vector.h"

// ── Asset handle ─────────────────────────────────────────────────────────────

template<typename T>
using AssetHandle = RefHandle<T>;

// Null handle helper
template<typename T>
AssetHandle<T> nullHandle() { return {}; }

// ── Loader trait — specialise for each asset type ────────────────────────────
// template<> struct AssetLoader<Texture> {
//     static AssetHandle<Texture> load(const char* path);
// };

template<typename T>
struct AssetLoader {
    static AssetHandle<T> load(const char* path) {
        (void)path;
        static_assert(sizeof(T)==0, "Specialise AssetLoader<T> for your asset type");
        return {};
    }
};

// ── Asset manager ─────────────────────────────────────────────────────────────
// Note: cache_ holds a strong reference to each asset — assets stay alive as
// long as they are in the cache.  Call collectGarbage() to evict assets that
// are only held by the cache itself (use_count == 1).

class AssetManager {
public:
    // Load asset by path. Returns cached copy if already loaded.
    template<typename T>
    AssetHandle<T> load(const char* path) {
        StringID id = SID_RT(path);
        // Check cache
        for (auto& e : cache_) {
            if (e.id == id && e.handle)
                return ref_static_cast<T>(e.handle);
        }
        // Load new
        auto handle = AssetLoader<T>::load(path);
        if (!handle) {
            LOG_WARN("AssetManager: failed to load '%s'", path);
            return {};
        }
        // Store strong reference in cache
        CacheEntry ce; ce.id = id; ce.handle = handle;
        cache_.push_back(rt_move(ce));
        return handle;
    }

    // Preload — load and pin until explicitly released
    template<typename T>
    AssetHandle<T> preload(const char* path) {
        auto h = load<T>(path);
        if (h) pinned_.push_back(RefHandle<void>(h));
        return h;
    }

    // Unpin pinned assets (cache entry may still hold a reference)
    void releasePinned() { pinned_.clear(); }

    // Purge cache entries whose only remaining holder is the cache itself
    void collectGarbage() {
        for (size_t i = 0; i < cache_.size(); ) {
            if (cache_[i].handle.use_count() <= 1)
                cache_.erase(cache_.begin() + i);
            else
                ++i;
        }
    }

    // Hot-reload: replace cached asset with a freshly loaded version
    template<typename T>
    void reload(const char* path) {
        StringID id = SID_RT(path);
        auto fresh = AssetLoader<T>::load(path);
        if (!fresh) return;
        for (auto& e : cache_) {
            if (e.id == id) e.handle = fresh;
        }
    }

    size_t cacheSize() const { return cache_.size(); }

private:
    struct CacheEntry {
        StringID       id;
        RefHandle<void> handle;
    };
    PodVector<CacheEntry>       cache_;
    PodVector<RefHandle<void>>  pinned_;
};

// Global singleton asset manager
inline AssetManager& globalAssets() { static AssetManager am; return am; }

// ── Texture specialisation (uses existing texture.h) ─────────────────────────
// Uncomment after including texture.h in your translation unit:
//
// #include "texture.h"
// template<> struct AssetLoader<Texture> {
//     static AssetHandle<Texture> load(const char* path) {
//         auto t = std::make_shared<Texture>();
//         if (!t->Load(path)) return nullptr;
//         return t;
//     }
// };

#endif
