#pragma once
#include "Core/Assert.hpp"
#include "Core/Exceptions.hpp"
#include "Core/Log.hpp"
#include "Components/Tags.hpp"
#include "Scene/ComponentInfo.hpp"
#include "Scene/DynamicComponentStorage.hpp"

#include <entt/core/type_info.hpp>
#include <entt/entity/registry.hpp>

#include <algorithm>
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

namespace Index::detail {
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

namespace Index {
    class ComponentRegistry {
    public:
        template<typename T>
        void Register(ComponentInfo info) {
            const std::type_index id = typeid(T);
            const auto existing = m_map.find(id);
            if (existing != m_map.end()) {
                if (info.serializedName.empty()) info.serializedName = existing->second.serializedName;
                if (!info.drawInspector) info.drawInspector = existing->second.drawInspector;
                if (info.properties.empty()) info.properties = existing->second.properties;
                if (info.conflictsWith.empty()) info.conflictsWith = existing->second.conflictsWith;
                if (info.dependsOn.empty()) info.dependsOn = existing->second.dependsOn;
                // Preserve serialize/deserialize: AttachInspector re-registers and would otherwise drop them.
                if (!info.serialize) info.serialize = existing->second.serialize;
                if (!info.deserialize) info.deserialize = existing->second.deserialize;
                if (!info.serializeFirst) info.serializeFirst = existing->second.serializeFirst;
                if (!info.serializeArchive) info.serializeArchive = existing->second.serializeArchive;
                if (info.serializationVersion == 1 && existing->second.serializationVersion != 1) {
                    info.serializationVersion = existing->second.serializationVersion;
                }
                if (!info.onAdd) info.onAdd = existing->second.onAdd;
                // typeIdU32 is published to the managed side and embedded in ECB wire payloads — must survive re-registration.
                if (info.typeIdU32 == 0) info.typeIdU32 = existing->second.typeIdU32;
                if (!info.emplaceFromBytes) info.emplaceFromBytes = existing->second.emplaceFromBytes;
                if (!info.writeBytes) info.writeBytes = existing->second.writeBytes;
                if (!info.defaultEmplace) info.defaultEmplace = existing->second.defaultEmplace;
            }

            // Ownership is fixed at first registration so a package re-registering a built-in can't take it down on unload.
            if (existing != m_map.end()) info.ownerId = existing->second.ownerId;
            else if (info.ownerId == 0) info.ownerId = m_registrationOwner;
            if (existing != m_map.end()) info.engineModule = existing->second.engineModule;
            else if (!info.engineModule) info.engineModule = m_registrationModule;

            info.typeId = id;
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
                        info.emplaceFromBytes = [](entt::registry& r, EntityHandle e,
                                                   const void* bytes, size_t size) {
                            IDX_CORE_ASSERT(size == sizeof(T), IndexErrorCode::InvalidValue,
                                "ComponentRegistry: emplaceFromBytes size mismatch for component");
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
                (existing != m_map.end() && existing->second.copyTo != nullptr);
            if (preserveExistingCopyTo && info.copyTo == nullptr) {
                info.copyTo = existing->second.copyTo;
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

            info.serializedNameHash = info.serializedName.empty()
                ? 0u
                : detail::FnvHash64(info.serializedName);

            if (existing != m_map.end()) {
                const uint64_t oldHash = existing->second.serializedNameHash;
                if (oldHash != 0 && oldHash != info.serializedNameHash) {
                    const auto oldIt = m_hashIndex.find(oldHash);
                    if (oldIt != m_hashIndex.end() && oldIt->second == &existing->second) {
                        m_hashIndex.erase(oldIt);
                    }
                }
            }

            if (info.typeIdU32 == 0) {
                if (m_byTypeId.empty()) {
                    m_byTypeId.push_back(nullptr); // reserve slot 0 as null
                }
                info.typeIdU32 = static_cast<uint32_t>(m_byTypeId.size());
                m_byTypeId.push_back(nullptr); // placeholder, fixed up below
            }

            auto inserted = m_map.insert_or_assign(id, std::move(info));
            ComponentInfo& stored = inserted.first->second;
            if (stored.serializedNameHash != 0) {
                m_hashIndex[stored.serializedNameHash] = &stored;
            }
            if (stored.typeIdU32 != 0 && stored.typeIdU32 < m_byTypeId.size()) {
                m_byTypeId[stored.typeIdU32] = &stored;
            }
            ++m_generation;
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
        uint32_t RegisterDynamic(
            const std::string& displayName,
            const std::string& serializedName,
            const std::string& subcategory,
            ComponentCategory category,
            uint32_t size,
            uint32_t alignment)
        {
            if (size == 0) {
                IDX_CORE_WARN_TAG("ComponentRegistry",
                    "RegisterDynamic refused '{}': size is zero", displayName);
                return 0;
            }
            if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
                IDX_CORE_WARN_TAG("ComponentRegistry",
                    "RegisterDynamic refused '{}': alignment {} is not a power of two",
                    displayName, alignment);
                return 0;
            }
            if (alignment > alignof(std::max_align_t)) {
                IDX_CORE_WARN_TAG("ComponentRegistry",
                    "RegisterDynamic refused '{}': alignment {} exceeds max_align_t ({})",
                    displayName, alignment, static_cast<uint32_t>(alignof(std::max_align_t)));
                return 0;
            }
            if ((size % alignment) != 0) {
                IDX_CORE_WARN_TAG("ComponentRegistry",
                    "RegisterDynamic refused '{}': size {} is not a multiple of alignment {} "
                    "(successive elements would land at misaligned offsets)",
                    displayName, size, alignment);
                return 0;
            }
            const uint64_t serializedHash = serializedName.empty()
                ? 0u : detail::FnvHash64(serializedName);
            if (serializedHash != 0 && m_hashIndex.find(serializedHash) != m_hashIndex.end()) {
                IDX_CORE_WARN_TAG("ComponentRegistry",
                    "RegisterDynamic refused '{}': serializedName '{}' already taken",
                    displayName, serializedName);
                return 0;
            }

            auto storage = std::make_unique<DynamicComponentStorage>(size, alignment);
            DynamicComponentStorage* storagePtr = storage.get();

            ComponentInfo info{};
            info.displayName       = displayName;
            info.serializedName    = serializedName;
            info.subcategory       = subcategory;
            info.category          = category;
            info.serializedNameHash = serializedHash;
            info.isDynamic         = true;
            info.rawSize           = size;
            info.dynamicStorage    = storagePtr;
            info.has = [storagePtr](Entity e) -> bool {
                return e.IsValid() && storagePtr->Contains(*e.GetRegistry(), e.GetHandle());
            };
            info.add = [storagePtr](Entity e) {
                if (e.IsValid()) storagePtr->Add(*e.GetRegistry(), e.GetHandle());
            };
            info.remove = [storagePtr](Entity e) {
                if (e.IsValid()) storagePtr->Remove(*e.GetRegistry(), e.GetHandle());
            };
            info.getRaw = [storagePtr](Entity e) -> void* {
                if (!e.IsValid()) return nullptr;
                return storagePtr->Get(*e.GetRegistry(), e.GetHandle());
            };
            info.emplaceFromBytes = [storagePtr](entt::registry& registry, EntityHandle e,
                const void* bytes, size_t byteSize) {
                IDX_CORE_ASSERT(byteSize == storagePtr->ElementSize(),
                    IndexErrorCode::InvalidValue,
                    "Dynamic component emplaceFromBytes size mismatch");
                storagePtr->EmplaceOrReplace(registry, e, bytes);
            };
            info.defaultEmplace = [storagePtr](entt::registry& registry, EntityHandle e) {
                storagePtr->Add(registry, e);
            };
            info.copyTo = [storagePtr](Entity src, Entity dst) {
                if (!src.IsValid() || !dst.IsValid()) return;
                const void* srcBytes = storagePtr->Get(*src.GetRegistry(), src.GetHandle());
                if (!srcBytes) return;
                storagePtr->EmplaceOrReplace(*dst.GetRegistry(), dst.GetHandle(), srcBytes);
            };
            info.fillRawPointers = [storagePtr](entt::registry& reg, void** outPointers,
                int maxRows, int enableFilter) -> int {
				auto view = storagePtr->GetSceneView(reg);
				int count = 0;
				for (EntityHandle e : view.Entities()) {
                    if (enableFilter == 1 && reg.all_of<DisabledTag>(e)) continue;
                    if (enableFilter == 2 && !reg.all_of<DisabledTag>(e)) continue;
                    if (outPointers && count < maxRows) {
                        outPointers[count] = view.Get(e);
                    }
                    ++count;
                }
                return count;
            };

            info.serialize = [storagePtr](Entity e) -> Json::Value {
                if (!e.IsValid() || !storagePtr) return Json::Value::MakeObject();
                const void* bytes = storagePtr->Get(*e.GetRegistry(), e.GetHandle());
                if (!bytes || storagePtr->ElementSize() == 0) {
                    return Json::Value::MakeObject();
                }
                static constexpr char kHex[] = "0123456789abcdef";
                std::string hex;
                const uint32_t n = storagePtr->ElementSize();
                hex.resize(static_cast<std::size_t>(n) * 2);
                const std::uint8_t* src = static_cast<const std::uint8_t*>(bytes);
                for (uint32_t i = 0; i < n; ++i) {
                    hex[i * 2 + 0] = kHex[(src[i] >> 4) & 0x0F];
                    hex[i * 2 + 1] = kHex[(src[i] >> 0) & 0x0F];
                }
                Json::Value out = Json::Value::MakeObject();
                out.AddMember("data", Json::Value(std::move(hex)));
                return out;
            };

            info.deserialize = [storagePtr, componentName = std::string(serializedName)](Entity e, const Json::Value& v) {
                if (!e.IsValid() || !storagePtr) return;
                if (!v.IsObject()) return;
                const Json::Value* dataNode = v.FindMember("data");
                if (!dataNode || !dataNode->IsString()) return;
                const std::string hex = dataNode->AsStringOr();
                const std::size_t byteCount = hex.size() / 2;
                const std::size_t elementSize = storagePtr->ElementSize();
                if (byteCount == 0 || elementSize == 0) return;
                if (byteCount != elementSize) {
                    // The C# struct layout changed since this value was saved. Previously this
                    // returned silently, leaving the component zeroed — and the next save made
                    // the loss permanent. Keep the overlapping prefix instead: with sequential
                    // layout, fields appended to the end (the common edit) keep their offsets,
                    // so existing values survive and new fields start at their default (zero).
                    static std::unordered_set<std::string> s_WarnedLayoutChanges;
                    if (s_WarnedLayoutChanges.insert(componentName).second) {
                        IDX_CORE_WARN_TAG("ComponentRegistry",
                            "Component '{}' was saved with {} bytes but is now {} bytes. Kept the first {} bytes; "
                            "fields that were reordered, removed or inserted before existing fields may hold wrong values "
                            "— review this component's values before saving.",
                            componentName, byteCount, elementSize, (std::min)(byteCount, elementSize));
                    }
                }
                std::vector<std::uint8_t> bytes(elementSize, 0);
                auto fromHex = [](char c) -> int {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                    return -1;
                };
                const std::size_t copyCount = (std::min)(byteCount, elementSize);
                for (std::size_t i = 0; i < copyCount; ++i) {
                    const int hi = fromHex(hex[i * 2 + 0]);
                    const int lo = fromHex(hex[i * 2 + 1]);
                    if (hi < 0 || lo < 0) return; // malformed hex — leave zero-init
                    bytes[i] = static_cast<std::uint8_t>((hi << 4) | lo);
                }
                storagePtr->EmplaceOrReplace(*e.GetRegistry(), e.GetHandle(), bytes.data());
            };

            if (m_byTypeId.empty()) m_byTypeId.push_back(nullptr);
            info.typeIdU32 = static_cast<uint32_t>(m_byTypeId.size());
            m_byTypeId.push_back(nullptr); // placeholder; patched below

            info.storageHash = static_cast<entt::id_type>(serializedHash != 0
                ? serializedHash : static_cast<uint64_t>(info.typeIdU32));

            const uint32_t typeIdU32 = info.typeIdU32;
            m_dynamicStorages.emplace(typeIdU32, std::move(storage));
            auto inserted = m_dynamicMap.emplace(typeIdU32, std::move(info));
            ComponentInfo& stored = inserted.first->second;
            if (stored.serializedNameHash != 0) {
                m_hashIndex[stored.serializedNameHash] = &stored;
            }
            if (stored.typeIdU32 < m_byTypeId.size()) {
                m_byTypeId[stored.typeIdU32] = &stored;
            }
            ++m_generation;
            return typeIdU32;
        }

        // Dynamics live outside EnTT pools so on_destroy won't fire; call before m_Registry.destroy().
        void ScrubEntity(const entt::registry& registry, EntityHandle e) {
            for (auto& [typeIdU32, storage] : m_dynamicStorages) {
                (void)typeIdU32;
                if (storage) {
                    storage->Remove(registry, e);
                }
            }
        }

        void ScrubRegistry(const entt::registry& registry) {
            for (auto& [typeIdU32, storage] : m_dynamicStorages) {
                (void)typeIdU32;
                if (storage) {
                    storage->Clear(registry);
                }
            }
        }

        // MUST be called before user assembly unloads so captured lambdas don't outlive storage.
        void UnregisterAllDynamic() {
            for (auto& [typeIdU32, info] : m_dynamicMap) {
                if (info.serializedNameHash != 0) {
                    const auto it = m_hashIndex.find(info.serializedNameHash);
                    if (it != m_hashIndex.end() && it->second == &info) {
                        m_hashIndex.erase(it);
                    }
                }
                if (typeIdU32 < m_byTypeId.size()) {
                    m_byTypeId[typeIdU32] = nullptr;
                }
            }
            m_dynamicMap.clear();
            m_dynamicStorages.clear();
            ++m_generation;
        }

        // Register<T> tags new entries with this owner while non-zero.
        void SetRegistrationOwner(uint32_t owner) noexcept { m_registrationOwner = owner; }
        uint32_t GetRegistrationOwner() const noexcept { return m_registrationOwner; }

        // Register<T> tags new entries with this engine module while set.
        void SetRegistrationModule(std::optional<EngineModule> module) noexcept { m_registrationModule = module; }
        std::optional<EngineModule> GetRegistrationModule() const noexcept { return m_registrationModule; }

        // MUST be called before the owner's module unloads: entries hold function pointers into it. Typed IDs are not reused.
        size_t UnregisterOwner(uint32_t owner) {
            if (owner == 0) return 0;

            std::vector<std::type_index> removed;
            for (auto it = m_map.begin(); it != m_map.end();) {
                ComponentInfo& info = it->second;
                if (info.ownerId != owner) {
                    ++it;
                    continue;
                }
                if (info.serializedNameHash != 0) {
                    const auto hashIt = m_hashIndex.find(info.serializedNameHash);
                    if (hashIt != m_hashIndex.end() && hashIt->second == &info) {
                        m_hashIndex.erase(hashIt);
                    }
                }
                if (info.typeIdU32 < m_byTypeId.size() && m_byTypeId[info.typeIdU32] == &info) {
                    m_byTypeId[info.typeIdU32] = nullptr;
                }
                removed.push_back(it->first);
                it = m_map.erase(it);
            }
            if (removed.empty()) return 0;
            ++m_generation;

            const auto isRemoved = [&removed](const std::type_index& type) {
                return std::find(removed.begin(), removed.end(), type) != removed.end();
            };
            const auto scrub = [&isRemoved](ComponentInfo& info) {
                std::erase_if(info.conflictsWith, isRemoved);
                std::erase_if(info.dependsOn, isRemoved);
            };
            for (auto& [id, info] : m_map) {
                (void)id;
                scrub(info);
            }
            for (auto& [tid, info] : m_dynamicMap) {
                (void)tid;
                scrub(info);
            }
            return removed.size();
        }

        void CopyComponents(Entity src, Entity dst) const {
            for (const auto& [id, info] : m_map) {
                (void)id;
                if (info.copyTo) {
                    info.copyTo(src, dst);
                }
            }
        }

        // The component already on the entity that `proposed` conflicts with, or null.
        const ComponentInfo* FindConflict(Entity entity, std::type_index proposed) const {
            const auto proposedIt = m_map.find(proposed);
            if (proposedIt != m_map.end()) {
                // Conflicts are declared on both sides, so the proposed component's list is complete; this
                // keeps the check proportional to its few conflicts (ECB playback calls it per add).
                for (const std::type_index& conflict : proposedIt->second.conflictsWith) {
                    const auto existing = m_map.find(conflict);
                    if (existing != m_map.end() && existing->second.has && existing->second.has(entity)) {
                        return &existing->second;
                    }
                }
                return nullptr;
            }
            for (const auto& [existingId, existingInfo] : m_map) {
                if (!existingInfo.has || !existingInfo.has(entity)) continue;
                for (const std::type_index& conflict : existingInfo.conflictsWith) {
                    if (conflict == proposed) return &existingInfo;
                }
            }
            return nullptr;
        }

        bool HasConflict(Entity entity, std::type_index proposed) const {
            return FindConflict(entity, proposed) != nullptr;
        }

        // A pair of components on the entity that may not be combined (data from before a conflict existed).
        std::pair<const ComponentInfo*, const ComponentInfo*> FindConflictingPair(Entity entity) const {
            for (const auto& [id, info] : m_map) {
                if (info.conflictsWith.empty() || !info.has || !info.has(entity)) continue;
                for (const std::type_index& conflict : info.conflictsWith) {
                    const auto other = m_map.find(conflict);
                    if (other != m_map.end() && other->second.has && other->second.has(entity)) {
                        return { &info, &other->second };
                    }
                }
            }
            return { nullptr, nullptr };
        }

        // Symmetric, like Codegen::DeclareConflict<A, B>, for types only known at runtime.
        void DeclareConflict(std::type_index a, std::type_index b) {
            if (a == b) return;
            const auto ai = m_map.find(a);
            const auto bi = m_map.find(b);
            if (ai == m_map.end() || bi == m_map.end()) return;
            if (std::find(ai->second.conflictsWith.begin(), ai->second.conflictsWith.end(), b) == ai->second.conflictsWith.end()) {
                ai->second.conflictsWith.push_back(b);
            }
            if (std::find(bi->second.conflictsWith.begin(), bi->second.conflictsWith.end(), a) == bi->second.conflictsWith.end()) {
                bi->second.conflictsWith.push_back(a);
            }
        }

        /// Adds component + transitive dependsOn chain. Cycle-safe. Conflicting deps are skipped with a warning.
        bool AddWithDependencies(Entity entity, std::type_index type) const {
            std::unordered_set<std::type_index> visited;
            return AddWithDependenciesImpl(entity, type, visited, /*isRoot=*/true);
        }

        /// Validates conflict symmetry (A↔B both declared). O(N*M), debug-only.
#ifdef IDX_DEBUG
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
                    IDX_CORE_ASSERT(symmetric, IndexErrorCode::InvalidValue,
                        std::string("Asymmetric conflict declaration: ") + aId.name() + " <-> " + bId.name() +
                        " (declared on first side only - DeclareConflict<A, B>() should add both directions)");
                }
            }
        }
#else
        void ValidateConflictSymmetry() const {}
#endif

        bool TypesConflict(std::type_index a, std::type_index b) const {
            if (a == b) return false;
            const auto ai = m_map.find(a);
            if (ai != m_map.end()) {
                for (const std::type_index& c : ai->second.conflictsWith) {
                    if (c == b) return true;
                }
            }
            const auto bi = m_map.find(b);
            if (bi != m_map.end()) {
                for (const std::type_index& c : bi->second.conflictsWith) {
                    if (c == a) return true;
                }
            }
            return false;
        }

    private:
        bool AddWithDependenciesImpl(Entity entity, std::type_index type,
            std::unordered_set<std::type_index>& visited, bool isRoot) const
        {
            if (!visited.insert(type).second) return true;

            const auto it = m_map.find(type);
            if (it == m_map.end() || !it->second.add || !it->second.has) return false;
            const ComponentInfo& info = it->second;

            if (info.has(entity)) return true;

            if (const ComponentInfo* conflict = FindConflict(entity, type)) {
                const std::string& name = info.displayName.empty() ? info.serializedName : info.displayName;
                const std::string& conflictName = conflict->displayName.empty() ? conflict->serializedName : conflict->displayName;
                if (isRoot) {
                    IDX_CORE_WARN_TAG("ComponentRegistry", "Cannot add '{}': the entity has '{}', which it can't be combined with",
                        name, conflictName);
                }
                else {
                    IDX_CORE_WARN_TAG("ComponentRegistry",
                        "Skipping auto-add of dependency '{}' — conflicts with '{}' on the entity", name, conflictName);
                }
                return false;
            }

            for (const std::type_index& dep : info.dependsOn) {
                AddWithDependenciesImpl(entity, dep, visited, /*isRoot=*/false);
            }

            info.add(entity);
            if (info.onAdd) {
                info.onAdd(entity);
            }
            return true;
        }

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
