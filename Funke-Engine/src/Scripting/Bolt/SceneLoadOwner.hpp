#pragma once

#include "Scene/SceneManager.hpp"
#include <unordered_map>
#include <vector>

namespace Funke::BoltScriptHost::SceneLoads {
    inline std::unordered_map<uint64_t, const void*> s_Owners;

    inline uint64_t Track(const void* owner, uint64_t operation, uint64_t keep) {
        if (!owner || !operation) return 0;
        if (keep) {
            try {
                const auto [it, inserted] = s_Owners.emplace(operation, owner);
                return it->second == owner;
            } catch (...) {
                SceneManager::CancelSceneLoad(operation);
                return 0;
            }
        }
        const auto it = s_Owners.find(operation);
        if (it == s_Owners.end() || it->second != owner) return 0;
        s_Owners.erase(it);
        return 1;
    }

    inline void ReleaseGeneration(const void* owner) {
        std::vector<uint64_t> canceled;
        std::erase_if(s_Owners, [&](const auto& item) {
            if (owner && item.second != owner) return false;
            canceled.push_back(item.first);
            return true;
        });
        for (uint64_t operation : canceled) SceneManager::CancelSceneLoad(operation);
    }
}
