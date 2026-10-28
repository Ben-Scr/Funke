#pragma once
#include "Core/Assert.hpp"
#include "Core/Exceptions.hpp"
#include "Core/Export.hpp"
#include "Components/Tags.hpp"
#include "Scene/ComponentInfo.hpp"
#include "Scene/DynamicComponentStorage.hpp"

#include <entt/core/type_info.hpp>
#include <entt/entity/registry.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Funke::detail {
    // FNV-1a 64-bit, stable across ABIs — safe to embed in serialized assets.
    constexpr uint64_t FnvHash64(std::string_view str) noexcept {
        constexpr uint64_t kOffsetBasis = 0xcbf29ce484222325ULL;
        constexpr uint64_t kPrime = 0x100000001b3ULL;
        uint64_t h = kOffsetBasis;
        for (char c : str) {
            h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
            h *= kPrime;
        }
        return h;
    }
}

namespace Funke {
    class ComponentRegistry {
    public:
        template<typename T>
        void Register(ComponentInfo info) {
            const std::type_index id = typeid(T);
            const ComponentInfo* existing = PrepareRegistration(id, info);
            info.storageHash = entt::type_hash<T>::value();
            info.has = [](Entity e) { return e.HasComponent<T>(); };
            info.add = [](Entity e) { e.AddComponent<T>(); };
            info.remove = [](Entity e) { e.RemoveComponent<T>(); };

            // Skip empty tag types — no addressable payload in EnTT's empty-type storage.
            if constexpr (!std::is_empty_v<T>) {
                info.getRaw = [](Entity e) -> void* {
                    if (!e.HasComponent<T>()) return nullptr;
                    return static_cast<void*>(&e.GetComponent<T>());
                };
                info.fillRawPointers = [](entt::registry& registry, void** outPointers, int maxRows, int enableFilter) -> int {
                    int count = 0;
                    auto view = registry.view<T>();
                    for (auto&& [entity, component] : view.each()) {
                        if (enableFilter == 1 && registry.all_of<DisabledTag>(entity)) continue;
                        if (enableFilter == 2 && !registry.all_of<DisabledTag>(entity)) continue;
                        if (outPointers && count < maxRows) {
                            outPointers[count] = static_cast<void*>(&component);
                        }
                        ++count;
                    }
                    return count;
                };
                info.rawSize = sizeof(T);

                // Gate on is_trivially_destructible (not is_trivially_copyable): UUID has a user-declared
                // copy-ctor but a trivial dtor, making SpriteRendererComponent memcpy-safe despite being
                // non-trivially-copyable. Components with owning heap (std::vector etc.) are excluded —
                // null emplaceFromBytes surfaces a clean error instead of a silent double-free.
                if constexpr (std::is_trivially_destructible_v<T>) {
                    if (info.emplaceFromBytes == nullptr) {
                        info.bytePayloadSize = sizeof(T);
                        info.emplaceFromBytes = [](entt::registry& r, EntityHandle e,
                                                   const void* bytes, size_t size) {
                            if (size != sizeof(T)) {
                                FNK_THROW(FunkeErrorCode::InvalidValue,
                                    "ComponentRegistry: emplaceFromBytes size mismatch for component");
                            }
                            if (bytes == nullptr) {
                                FNK_THROW(FunkeErrorCode::NullReference,
                                    "ComponentRegistry: emplaceFromBytes payload is null");
                            }
                            if (!r.valid(e)) {
                                FNK_THROW(FunkeErrorCode::InvalidHandle,
                                    "ComponentRegistry: emplaceFromBytes entity is invalid");
                            }
                            T value;
                            std::memcpy(&value, bytes, sizeof(T));
                            r.emplace_or_replace<T>(e, std::move(value));
                        };
                    }
                }

                if (info.defaultEmplace == nullptr) {
                    if constexpr (std::is_default_constructible_v<T>) {
                        info.defaultEmplace = [](entt::registry& r, EntityHandle e) {
                            r.emplace<T>(e);
                        };
                    }
                }

                // Same trivially-destructible gate as emplaceFromBytes; null writeBytes makes PrefabTemplateCache
                // mark the template unbakeable and fall back to the slow per-property path.
                if constexpr (std::is_trivially_destructible_v<T>) {
                    if (info.writeBytes == nullptr) {
                        info.writeBytes = [](const entt::registry& r, EntityHandle e,
                                              std::vector<uint8_t>& out) -> bool {
                            const T* comp = r.try_get<T>(e);
                            if (comp == nullptr) return false;
                            const size_t oldSize = out.size();
                            out.resize(oldSize + sizeof(T));
                            std::memcpy(out.data() + oldSize, comp, sizeof(T));
                            return true;
                        };
                    }
                }
            }

            // Preserve custom copyTo — scene-bound components (e.g. ParticleSystem2D) need re-binding
            // that the raw value-copy path bypasses.
            const bool preserveExistingCopyTo =
                info.copyTo != nullptr ||
                (existing != nullptr && existing->copyTo != nullptr);
            if (preserveExistingCopyTo && info.copyTo == nullptr) {
                info.copyTo = existing->copyTo;
            }

            if (!preserveExistingCopyTo) {
                if constexpr (!std::is_empty_v<T>) {
                    info.copyTo = [](Entity src, Entity dst) {
                        if (!src.HasComponent<T>()) return;
                        if (dst.HasComponent<T>())
                            dst.GetComponent<T>() = src.GetComponent<T>();
                        else
                            dst.AddComponent<T>(src.GetComponent<T>());
                    };
                } else {
                    info.copyTo = [](Entity src, Entity dst) {
                        if (src.HasComponent<T>() && !dst.HasComponent<T>())
                            dst.AddComponent<T>();
                    };
                }
            }

            FinishRegistration(id, std::move(info), existing);
        }

        /// O(1) lookup by stable u32 type ID. Returns nullptr for id 0 (sentinel) or out-of-range ids.
        const ComponentInfo* GetByTypeId(uint32_t typeIdU32) const {
            if (typeIdU32 == 0 || typeIdU32 >= m_byTypeId.size()) return nullptr;
            return m_byTypeId[typeIdU32];
        }

        uint32_t GetTypeIdCount() const {
            // Slot 0 is the null sentinel — subtract it from the reported count.
            return m_byTypeId.empty() ? 0u : static_cast<uint32_t>(m_byTypeId.size() - 1);
        }

        const auto& All() const { return m_map; }

        /// Bumped on every registration change; lets callers memoize ComponentInfo pointers.
        uint64_t GetGeneration() const noexcept { return m_generation; }

        /// O(1) lookup by FNV-1a hash of serializedName. Returns nullptr for hash 0 or no match.
        const ComponentInfo* FindByHash(uint64_t serializedNameHash) const {
            if (serializedNameHash == 0) return nullptr;
            const auto it = m_hashIndex.find(serializedNameHash);
            return it != m_hashIndex.end() ? it->second : nullptr;
        }

        const ComponentInfo* FindBySerializedName(std::string_view name) const {
            return FindByHash(detail::FnvHash64(name));
        }

        /// Resolve ComponentInfo for hybrid inspectors that mix DrawAll with custom widgets.
        template <typename T>
        const ComponentInfo* GetInfo() const {
            const auto it = m_map.find(typeid(T));
            return it != m_map.end() ? &it->second : nullptr;
        }

        template <typename T>
        ComponentInfo* GetInfo() {
            const auto it = m_map.find(typeid(T));
            return it != m_map.end() ? &it->second : nullptr;
        }

        /// Registration order (typeIdU32), static and dynamic entries alike; keeps written files deterministic.
        template <typename F>
        void ForEachComponentInfoInRegistrationOrder(F&& fn) const {
            for (size_t typeId = 1; typeId < m_byTypeId.size(); ++typeId) {
                if (const ComponentInfo* info = m_byTypeId[typeId]) fn(*info);
            }
        }

        template <typename F>
        void ForEachComponentInfo(F&& fn) {
            for (auto& [id, info] : m_map)
                fn(id, info);
            // Dynamic entries use typeid(void) — callers keying by type_index implicitly skip them.
            for (auto& [tid, info] : m_dynamicMap) {
                (void)tid;
                fn(s_VoidTypeIndex, info);
            }
        }

        template <typename F>
        void ForEachComponentInfo(F&& fn) const {
            for (const auto& [id, info] : m_map)
                fn(id, info);
            for (const auto& [tid, info] : m_dynamicMap) {
                (void)tid;
                fn(s_VoidTypeIndex, info);
            }
        }

        // RegisterDynamic: called by DynamicComponentRegistrar after user assembly loads. Returns assigned
        // typeIdU32 or 0 on failure. Storage is owned here, outliving all captured callbacks until UnregisterAllDynamic.
        FUNKE_API uint32_t RegisterDynamic(
            const std::string& displayName,
            const std::string& serializedName,
            const std::string& subcategory,
            ComponentCategory category,
            uint32_t size,
            uint32_t alignment);

        // Dynamics live outside EnTT pools so on_destroy won't fire; call before m_Registry.destroy().
        FUNKE_API void ScrubEntity(const entt::registry& registry, EntityHandle e);

        FUNKE_API void ScrubRegistry(const entt::registry& registry);

        // MUST be called before user assembly unloads so captured lambdas don't outlive storage.
        FUNKE_API void UnregisterAllDynamic();

        // Register<T> tags new entries with this owner while non-zero.
        void SetRegistrationOwner(uint32_t owner) noexcept { m_registrationOwner = owner; }
        uint32_t GetRegistrationOwner() const noexcept { return m_registrationOwner; }

        // Register<T> tags new entries with this engine module while set.
        void SetRegistrationModule(std::optional<EngineModule> module) noexcept { m_registrationModule = module; }
        std::optional<EngineModule> GetRegistrationModule() const noexcept { return m_registrationModule; }

        // MUST be called before the owner's module unloads: entries hold function pointers into it. Typed IDs are not reused.
        FUNKE_API size_t UnregisterOwner(uint32_t owner);

        FUNKE_API void CopyComponents(Entity src, Entity dst) const;

        // The component already on the entity that `proposed` conflicts with, or null.
        FUNKE_API const ComponentInfo* FindConflict(Entity entity, std::type_index proposed) const;

        bool HasConflict(Entity entity, std::type_index proposed) const {
            return FindConflict(entity, proposed) != nullptr;
        }

        // A pair of components on the entity that may not be combined (data from before a conflict existed).
        FUNKE_API std::pair<const ComponentInfo*, const ComponentInfo*> FindConflictingPair(Entity entity) const;

        // Symmetric, like Codegen::DeclareConflict<A, B>, for types only known at runtime.
        FUNKE_API void DeclareConflict(std::type_index a, std::type_index b);

        /// Adds component + transitive dependsOn chain. Cycle-safe. Conflicting deps are skipped with a warning.
        FUNKE_API bool AddWithDependencies(Entity entity, std::type_index type) const;

        /// Validates conflict symmetry (A↔B both declared). O(N*M), debug-only.
#ifdef FNK_DEBUG
        void ValidateConflictSymmetry() const {
            for (const auto& [aId, aInfo] : m_map) {
                for (const std::type_index& bId : aInfo.conflictsWith) {
                    const auto bIt = m_map.find(bId);
                    bool symmetric = false;
                    if (bIt != m_map.end()) {
                        for (const std::type_index& back : bIt->second.conflictsWith) {
                            if (back == aId) { symmetric = true; break; }
                        }
                    }
                    FNK_CORE_ASSERT(symmetric, FunkeErrorCode::InvalidValue,
                        std::string("Asymmetric conflict declaration: ") + aId.name() + " <-> " + bId.name() +
                        " (declared on first side only - DeclareConflict<A, B>() should add both directions)");
                }
            }
        }
#else
        void ValidateConflictSymmetry() const {}
#endif

        FUNKE_API bool TypesConflict(std::type_index a, std::type_index b) const;

    private:
        FUNKE_API const ComponentInfo* PrepareRegistration(std::type_index id, ComponentInfo& info);
        FUNKE_API void FinishRegistration(std::type_index id, ComponentInfo&& info, const ComponentInfo* existing);

        FUNKE_API bool AddWithDependenciesImpl(Entity entity, std::type_index type,
            std::unordered_set<std::type_index>& visited, bool isRoot) const;

        std::unordered_map<std::type_index, ComponentInfo> m_map;
        std::unordered_map<uint64_t, const ComponentInfo*> m_hashIndex;
        std::vector<const ComponentInfo*> m_byTypeId; // 1-indexed; slot 0 = null sentinel
        std::unordered_map<uint32_t, ComponentInfo> m_dynamicMap;
        std::unordered_map<uint32_t, std::unique_ptr<DynamicComponentStorage>> m_dynamicStorages;
        uint32_t m_registrationOwner = 0;
        std::optional<EngineModule> m_registrationModule;
        // Starts in a range of its own: a registry rebuilt by an in-process reload must never repeat a generation
        // of the destroyed one, or callers memoizing ComponentInfo pointers by generation keep dangling ones.
        static uint64_t NextGenerationEpoch() noexcept {
            static std::atomic<uint64_t> s_Epoch{ 0 };
            return (s_Epoch.fetch_add(1, std::memory_order_relaxed) + 1) << 32;
        }
        uint64_t m_generation = NextGenerationEpoch();
        // Dynamics share typeid(void) — std::type_index can't be synthesized; callers that key by
        // type_index implicitly skip dynamic entries.
        static inline const std::type_index s_VoidTypeIndex{ typeid(void) };
    };
}
