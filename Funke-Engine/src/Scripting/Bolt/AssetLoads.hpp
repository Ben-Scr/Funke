#pragma once

#include "Assets/AssetKind.hpp"
#include "Assets/AssetLoader.hpp"
#include "Assets/AssetRegistry.hpp"

#include <cstdint>
#include <unordered_map>

namespace Funke::BoltScriptHost::AssetLoads {
    inline std::unordered_map<uint64_t, const void*> s_Owners;

    inline void ReleaseGeneration(const void* owner) {
        std::erase_if(s_Owners, [=](const auto& item) {
            if (owner && item.second != owner) return false;
            AssetLoader::Release(item.first);
            return true;
        });
    }

    enum class Operation { CheckThread, Begin, Status, Release };

    inline uint64_t Call(const void* owner, uint64_t operation, uint64_t id, int expectedKind) noexcept {
        if (!AssetLoader::IsMainThread() || !owner) return 0;
        try {
            switch (static_cast<Operation>(operation)) {
            case Operation::CheckThread:
                return 1;
            case Operation::Begin: {
                if (!id || AssetRegistry::GetKind(id) != static_cast<AssetKind>(expectedKind)) return 0;
                const auto request = AssetLoader::LoadAsync(id);
                if (!request) return 0;
                try { s_Owners.emplace(request, owner); }
                catch (...) { AssetLoader::Release(request); throw; }
                return request;
            }
            case Operation::Status:
                if (const auto it = s_Owners.find(id); it != s_Owners.end() && it->second == owner)
                    return static_cast<uint64_t>(AssetLoader::GetResult(id).Status);
                return 0;
            case Operation::Release:
                if (const auto it = s_Owners.find(id); it != s_Owners.end() && it->second == owner) {
                    AssetLoader::Release(id);
                    s_Owners.erase(it);
                }
                return 0;
            }
        } catch (...) {}
        return 0;
    }
}
