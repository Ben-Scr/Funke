#include "pch.hpp"
#include "Scene/ComponentRegistry.hpp"

#include <algorithm>

namespace Funke {
    const ComponentInfo* ComponentRegistry::PrepareRegistration(std::type_index id, ComponentInfo& info) {
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
            if (!info.emplaceFromBytes) {
                info.emplaceFromBytes = existing->second.emplaceFromBytes;
                info.bytePayloadSize = existing->second.bytePayloadSize;
            }
            if (!info.writeBytes) info.writeBytes = existing->second.writeBytes;
            if (!info.defaultEmplace) info.defaultEmplace = existing->second.defaultEmplace;
        }

        // Ownership is fixed at first registration so a package re-registering a built-in can't take it down on unload.
        if (existing != m_map.end()) info.ownerId = existing->second.ownerId;
        else if (info.ownerId == 0) info.ownerId = m_registrationOwner;
        if (existing != m_map.end()) info.engineModule = existing->second.engineModule;
        else if (!info.engineModule) info.engineModule = m_registrationModule;

        info.typeId = id;
        return existing != m_map.end() ? &existing->second : nullptr;
    }

    void ComponentRegistry::FinishRegistration(std::type_index id, ComponentInfo&& info, const ComponentInfo* existing) {
        info.serializedNameHash = info.serializedName.empty()
            ? 0u
            : detail::FnvHash64(info.serializedName);

        if (existing != nullptr) {
            const uint64_t oldHash = existing->serializedNameHash;
            if (oldHash != 0 && oldHash != info.serializedNameHash) {
                const auto oldIt = m_hashIndex.find(oldHash);
                if (oldIt != m_hashIndex.end() && oldIt->second == existing) {
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

    uint32_t ComponentRegistry::RegisterDynamic(
        const std::string& displayName,
        const std::string& serializedName,
        const std::string& subcategory,
        ComponentCategory category,
        uint32_t size,
        uint32_t alignment) {
        if (size == 0) {
            FNK_CORE_WARN_TAG("ComponentRegistry",
                "RegisterDynamic refused '{}': size is zero", displayName);
            return 0;
        }
        if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
            FNK_CORE_WARN_TAG("ComponentRegistry",
                "RegisterDynamic refused '{}': alignment {} is not a power of two",
                displayName, alignment);
            return 0;
        }
        if (alignment > alignof(std::max_align_t)) {
            FNK_CORE_WARN_TAG("ComponentRegistry",
                "RegisterDynamic refused '{}': alignment {} exceeds max_align_t ({})",
                displayName, alignment, static_cast<uint32_t>(alignof(std::max_align_t)));
            return 0;
        }
        if ((size % alignment) != 0) {
            FNK_CORE_WARN_TAG("ComponentRegistry",
                "RegisterDynamic refused '{}': size {} is not a multiple of alignment {} "
                "(successive elements would land at misaligned offsets)",
                displayName, size, alignment);
            return 0;
        }
        const uint64_t serializedHash = serializedName.empty()
            ? 0u : detail::FnvHash64(serializedName);
        if (serializedHash != 0 && m_hashIndex.find(serializedHash) != m_hashIndex.end()) {
            FNK_CORE_WARN_TAG("ComponentRegistry",
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
        info.bytePayloadSize   = size;
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
            if (byteSize != storagePtr->ElementSize()) {
                FNK_THROW(FunkeErrorCode::InvalidValue,
                    "Dynamic component emplaceFromBytes size mismatch");
            }
            if (bytes == nullptr) {
                FNK_THROW(FunkeErrorCode::NullReference,
                    "Dynamic component emplaceFromBytes payload is null");
            }
            if (!registry.valid(e)) {
                FNK_THROW(FunkeErrorCode::InvalidHandle,
                    "Dynamic component emplaceFromBytes entity is invalid");
            }
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
                    FNK_CORE_WARN_TAG("ComponentRegistry",
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

    void ComponentRegistry::ScrubEntity(const entt::registry& registry, EntityHandle e) {
        for (auto& [typeIdU32, storage] : m_dynamicStorages) {
            (void)typeIdU32;
            if (storage) {
                storage->Remove(registry, e);
            }
        }
    }

    void ComponentRegistry::ScrubRegistry(const entt::registry& registry) {
        for (auto& [typeIdU32, storage] : m_dynamicStorages) {
            (void)typeIdU32;
            if (storage) {
                storage->Clear(registry);
            }
        }
    }

    void ComponentRegistry::UnregisterAllDynamic() {
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

    size_t ComponentRegistry::UnregisterOwner(uint32_t owner) {
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

    void ComponentRegistry::CopyComponents(Entity src, Entity dst) const {
        for (const auto& [id, info] : m_map) {
            (void)id;
            if (info.copyTo) {
                info.copyTo(src, dst);
            }
        }
    }

    const ComponentInfo* ComponentRegistry::FindConflict(Entity entity, std::type_index proposed) const {
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

    std::pair<const ComponentInfo*, const ComponentInfo*> ComponentRegistry::FindConflictingPair(Entity entity) const {
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

    void ComponentRegistry::DeclareConflict(std::type_index a, std::type_index b) {
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

    bool ComponentRegistry::AddWithDependencies(Entity entity, std::type_index type) const {
        std::unordered_set<std::type_index> visited;
        return AddWithDependenciesImpl(entity, type, visited, /*isRoot=*/true);
    }

    bool ComponentRegistry::TypesConflict(std::type_index a, std::type_index b) const {
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

    bool ComponentRegistry::AddWithDependenciesImpl(Entity entity, std::type_index type,
        std::unordered_set<std::type_index>& visited, bool isRoot) const {
        if (!visited.insert(type).second) return true;

        const auto it = m_map.find(type);
        if (it == m_map.end() || !it->second.add || !it->second.has) return false;
        const ComponentInfo& info = it->second;

        if (info.has(entity)) return true;

        if (const ComponentInfo* conflict = FindConflict(entity, type)) {
            const std::string& name = info.displayName.empty() ? info.serializedName : info.displayName;
            const std::string& conflictName = conflict->displayName.empty() ? conflict->serializedName : conflict->displayName;
            if (isRoot) {
                FNK_CORE_WARN_TAG("ComponentRegistry", "Cannot add '{}': the entity has '{}', which it can't be combined with",
                    name, conflictName);
            }
            else {
                FNK_CORE_WARN_TAG("ComponentRegistry",
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
}
