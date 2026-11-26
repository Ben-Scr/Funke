#pragma once

#include "Core/Application.hpp"
#include "Scripting/Bolt/NativeApi.hpp"
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace Funke::BoltScriptHost::RuntimeTextures {
    enum class Lifetime { Automatic, Generation, Manual, Play };
    enum class Operation { Create, Info, ReadPixel, WritePixel, ReadBytes, WriteBytes, Apply, SetLifetime, Dispose };

    struct Request {
        uint64_t Guid;
        int32_t Width, Height, Value, Capacity, LifetimeValue;
    };
    struct Texture {
        std::vector<uint8_t> Pixels;
        int Width, Height;
        Lifetime Scope;
        const void* Owner;
    };

    inline std::unordered_map<uint64_t, Texture> s_Textures;

    inline Lifetime Scope(int value) {
        if (value == 2) return Lifetime::Manual;
        if (value == 1) return Lifetime::Generation;
        return Application::GetIsPlaying() ? Lifetime::Play : Lifetime::Generation;
    }

    template<class Predicate>
    void Release(Predicate predicate) {
        const auto destroy = ScriptApi().Texture_DestroyRuntime;
        std::erase_if(s_Textures, [=](const auto& item) {
            if (!predicate(item.second)) return false;
            if (destroy) destroy(item.first);
            return true;
        });
    }

    inline void ReleaseGeneration(const void* owner) {
        Release([owner](const Texture& t) { return t.Scope != Lifetime::Manual && t.Owner == owner; });
    }
    inline void ExitPlay() { Release([](const Texture& t) { return t.Scope == Lifetime::Play; }); }
    inline void Shutdown() { Release([](const Texture&) { return true; }); }

    inline uint64_t Call(const void* owner, uint64_t op, void* request, void* bytes) noexcept {
        if (!request) return 0;
        auto& r = *static_cast<Request*>(request);
        const auto& api = ScriptApi();
        try {
            const auto operation = static_cast<Operation>(op);
            if (operation == Operation::Create) {
                if (!api.Texture_CreateRuntime || !api.Texture_DestroyRuntime || r.Width <= 0 || r.Height <= 0 ||
                    r.Capacity <= 0 || r.Width > r.Capacity / 4 / r.Height || !bytes) return 0;
                const size_t size = size_t(r.Width) * r.Height * 4;
                const auto* data = static_cast<const uint8_t*>(bytes);
                Texture texture{{data, data + size}, r.Width, r.Height, Scope(r.LifetimeValue), owner};
                const auto id = api.Texture_CreateRuntime(r.Width, r.Height, texture.Pixels.data(), r.Value);
                if (!id) return 0;
                try { s_Textures.emplace(id, std::move(texture)); }
                catch (...) { api.Texture_DestroyRuntime(id); throw; }
                return id;
            }
            const auto it = s_Textures.find(r.Guid);
            if (it == s_Textures.end()) return 0;
            auto& texture = it->second;
            switch (operation) {
            case Operation::Info:
                r.Width = texture.Width;
                r.Height = texture.Height;
                return texture.Pixels.size();
            case Operation::ReadPixel:
            case Operation::WritePixel: {
                if (r.Width < 0 || r.Height < 0 || r.Width >= texture.Width || r.Height >= texture.Height) {
                    r.Value = 0;
                    return 1;
                }
                auto* pixel = texture.Pixels.data() + (size_t(r.Height) * texture.Width + r.Width) * 4;
                if (operation == Operation::ReadPixel) std::memcpy(&r.Value, pixel, 4);
                else std::memcpy(pixel, &r.Value, 4);
                return 1;
            }
            case Operation::ReadBytes:
            case Operation::WriteBytes:
                if (!bytes || r.Capacity < 0 || size_t(r.Capacity) < texture.Pixels.size()) return 0;
                if (operation == Operation::ReadBytes) std::memcpy(bytes, texture.Pixels.data(), texture.Pixels.size());
                else std::memcpy(texture.Pixels.data(), bytes, texture.Pixels.size());
                return 1;
            case Operation::Apply:
                return api.Texture_UpdateRuntime && api.Texture_UpdateRuntime(r.Guid, texture.Pixels.data(), static_cast<int>(texture.Pixels.size()));
            case Operation::SetLifetime:
                texture.Owner = owner;
                texture.Scope = Scope(r.LifetimeValue);
                return 1;
            case Operation::Dispose:
                if (api.Texture_DestroyRuntime) api.Texture_DestroyRuntime(r.Guid);
                s_Textures.erase(it);
                return 1;
            default:
                return 0;
            }
        } catch (...) { return 0; }
    }
}

