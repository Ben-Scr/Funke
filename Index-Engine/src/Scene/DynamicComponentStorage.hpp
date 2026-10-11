#pragma once

#include "Scene/EntityHandle.hpp"

#include <entt/entity/registry.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Index {

	// Type-erased paged-sparse-array storage for runtime-registered C# components.
	class DynamicComponentStorage {
		using traits_type = entt::entt_traits<EntityHandle>;
		static constexpr std::size_t kPageSize = traits_type::page_size;

		struct SceneStorage {
			std::vector<EntityHandle> Entities;
			std::vector<uint8_t> Data;
			std::vector<std::vector<EntityHandle>> Sparse;
		};

	public:
		class SceneView {
		public:
			SceneView() = default;

			bool Contains(EntityHandle entity) const noexcept {
				return m_Owner && m_Storage && m_Owner->Contains(*m_Storage, entity);
			}

			void* Get(EntityHandle entity) const noexcept {
				return m_Owner && m_Storage ? m_Owner->Get(*m_Storage, entity) : nullptr;
			}

			std::size_t Size() const noexcept {
				return m_Storage ? m_Storage->Entities.size() : 0;
			}

			const std::vector<EntityHandle>& Entities() const noexcept {
				return m_Storage ? m_Storage->Entities : s_EmptyEntities;
			}

		private:
			friend class DynamicComponentStorage;
			SceneView(DynamicComponentStorage* owner, std::shared_ptr<SceneStorage> storage)
				: m_Owner(owner), m_Storage(std::move(storage)) {}

			DynamicComponentStorage* m_Owner = nullptr;
			std::shared_ptr<SceneStorage> m_Storage;
		};

		explicit DynamicComponentStorage(uint32_t elementSize, uint32_t alignment)
			: m_ElementSize(elementSize), m_Alignment(alignment) {}

		SceneView GetSceneView(const entt::registry& registry) noexcept {
			auto it = m_SceneStorages.find(&registry);
			return SceneView(this, it != m_SceneStorages.end() ? it->second : nullptr);
		}

		bool Contains(const entt::registry& registry, EntityHandle entity) const noexcept {
			const SceneStorage* sceneStorage = FindSceneStorage(registry);
			return sceneStorage && Contains(*sceneStorage, entity);
		}

		void* Get(const entt::registry& registry, EntityHandle entity) noexcept {
			SceneStorage* sceneStorage = FindSceneStorage(registry);
			return sceneStorage ? Get(*sceneStorage, entity) : nullptr;
		}

		const void* Get(const entt::registry& registry, EntityHandle entity) const noexcept {
			const SceneStorage* sceneStorage = FindSceneStorage(registry);
			if (!sceneStorage) return nullptr;

			const EntityHandle* slot = SparsePtr(*sceneStorage, entity);
			constexpr auto cap = traits_type::entity_mask;
			constexpr auto mask = traits_type::to_integral(entt::null) & ~cap;
			if (!slot) return nullptr;
			const auto xored = (mask & traits_type::to_integral(entity)) ^ traits_type::to_integral(*slot);
			if (xored >= cap) return nullptr;
			const auto denseIndex = static_cast<std::size_t>(traits_type::to_entity(*slot));
			return &sceneStorage->Data[denseIndex * m_ElementSize];
		}

		void Add(entt::registry& registry, EntityHandle entity) {
			AddInternal(registry, entity, nullptr);
		}

		void AddWithBytes(entt::registry& registry, EntityHandle entity, const void* bytes) {
			AddInternal(registry, entity, bytes);
		}

		void EmplaceOrReplace(entt::registry& registry, EntityHandle entity, const void* bytes) {
			if (void* existing = Get(registry, entity)) {
				std::memcpy(existing, bytes, m_ElementSize);
				return;
			}
			AddInternal(registry, entity, bytes);
		}

		void Remove(const entt::registry& registry, EntityHandle entity) {
			SceneStorage* sceneStorage = FindSceneStorage(registry);
			if (!sceneStorage || !Contains(registry, entity)) return;
			RemoveInternal(*sceneStorage, entity);
			if (sceneStorage->Entities.empty()) {
				m_SceneStorages.erase(&registry);
			}
		}

		void Clear(const entt::registry& registry) {
			m_SceneStorages.erase(&registry);
		}

		void Clear() {
			m_SceneStorages.clear();
		}

		std::size_t Size(const entt::registry& registry) const noexcept {
			const SceneStorage* sceneStorage = FindSceneStorage(registry);
			return sceneStorage ? sceneStorage->Entities.size() : 0;
		}

		uint32_t ElementSize() const noexcept { return m_ElementSize; }
		uint32_t Alignment() const noexcept { return m_Alignment; }

		const std::vector<EntityHandle>& Entities(const entt::registry& registry) const noexcept {
			const SceneStorage* sceneStorage = FindSceneStorage(registry);
			return sceneStorage ? sceneStorage->Entities : s_EmptyEntities;
		}

	private:
		bool Contains(const SceneStorage& sceneStorage, EntityHandle entity) const noexcept {
			const EntityHandle* slot = SparsePtr(sceneStorage, entity);
			constexpr auto cap = traits_type::entity_mask;
			constexpr auto mask = traits_type::to_integral(entt::null) & ~cap;
			return slot && (((mask & traits_type::to_integral(entity)) ^ traits_type::to_integral(*slot)) < cap);
		}

		void* Get(SceneStorage& sceneStorage, EntityHandle entity) noexcept {
			const EntityHandle* slot = SparsePtr(sceneStorage, entity);
			constexpr auto cap = traits_type::entity_mask;
			constexpr auto mask = traits_type::to_integral(entt::null) & ~cap;
			if (!slot) return nullptr;
			const auto xored = (mask & traits_type::to_integral(entity)) ^ traits_type::to_integral(*slot);
			if (xored >= cap) return nullptr;
			const auto denseIndex = static_cast<std::size_t>(traits_type::to_entity(*slot));
			return &sceneStorage.Data[denseIndex * m_ElementSize];
		}

		SceneStorage* FindSceneStorage(const entt::registry& registry) noexcept {
			auto it = m_SceneStorages.find(&registry);
			return it != m_SceneStorages.end() ? it->second.get() : nullptr;
		}

		const SceneStorage* FindSceneStorage(const entt::registry& registry) const noexcept {
			auto it = m_SceneStorages.find(&registry);
			return it != m_SceneStorages.end() ? it->second.get() : nullptr;
		}

		const EntityHandle* SparsePtr(const SceneStorage& sceneStorage, EntityHandle entity) const noexcept {
			const auto position = static_cast<std::size_t>(traits_type::to_entity(entity));
			const auto page = position / kPageSize;
			if (page >= sceneStorage.Sparse.size() || sceneStorage.Sparse[page].empty()) return nullptr;
			return &sceneStorage.Sparse[page][position % kPageSize];
		}

		EntityHandle& AssureSlot(SceneStorage& sceneStorage, EntityHandle entity) {
			const auto position = static_cast<std::size_t>(traits_type::to_entity(entity));
			const auto page = position / kPageSize;
			if (page >= sceneStorage.Sparse.size()) {
				sceneStorage.Sparse.resize(page + 1);
			}
			if (sceneStorage.Sparse[page].empty()) {
				sceneStorage.Sparse[page].assign(kPageSize, entt::null);
			}
			return sceneStorage.Sparse[page][position % kPageSize];
		}

		void AddInternal(entt::registry& registry, EntityHandle entity, const void* bytes) {
			if (Contains(registry, entity)) return;

			auto& sceneStoragePtr = m_SceneStorages[&registry];
			if (!sceneStoragePtr) sceneStoragePtr = std::make_shared<SceneStorage>();
			SceneStorage& sceneStorage = *sceneStoragePtr;
			if (const EntityHandle* slot = SparsePtr(sceneStorage, entity);
				slot && *slot != entt::null)
			{
				const auto denseIndex = static_cast<std::size_t>(traits_type::to_entity(*slot));
				if (denseIndex < sceneStorage.Entities.size()) {
					RemoveInternal(sceneStorage, sceneStorage.Entities[denseIndex]);
				}
			}
			const uint32_t denseIndex = static_cast<uint32_t>(sceneStorage.Entities.size());
			sceneStorage.Entities.push_back(entity);
			sceneStorage.Data.resize(sceneStorage.Data.size() + m_ElementSize);
			std::byte* slot = reinterpret_cast<std::byte*>(
				&sceneStorage.Data[static_cast<std::size_t>(denseIndex) * m_ElementSize]);
			if (bytes) {
				std::memcpy(slot, bytes, m_ElementSize);
			}
			else {
				std::memset(slot, 0, m_ElementSize);
			}
			AssureSlot(sceneStorage, entity) = traits_type::combine(
				static_cast<traits_type::entity_type>(denseIndex),
				traits_type::to_integral(entity));
		}

		void RemoveInternal(SceneStorage& sceneStorage, EntityHandle entity) {
			EntityHandle& slot = AssureSlot(sceneStorage, entity);
			const auto denseIndex = static_cast<uint32_t>(traits_type::to_entity(slot));
			const auto lastIndex = static_cast<uint32_t>(sceneStorage.Entities.size() - 1);
			if (denseIndex != lastIndex) {
				const EntityHandle lastEntity = sceneStorage.Entities[lastIndex];
				std::memcpy(
					&sceneStorage.Data[static_cast<std::size_t>(denseIndex) * m_ElementSize],
					&sceneStorage.Data[static_cast<std::size_t>(lastIndex) * m_ElementSize],
					m_ElementSize);
				sceneStorage.Entities[denseIndex] = lastEntity;
				AssureSlot(sceneStorage, lastEntity) = traits_type::combine(
					static_cast<traits_type::entity_type>(denseIndex),
					traits_type::to_integral(lastEntity));
			}

			sceneStorage.Entities.pop_back();
			sceneStorage.Data.resize(sceneStorage.Data.size() - m_ElementSize);
			slot = entt::null;
		}

		uint32_t m_ElementSize;
		uint32_t m_Alignment;
		std::unordered_map<const entt::registry*, std::shared_ptr<SceneStorage>> m_SceneStorages;
		static inline const std::vector<EntityHandle> s_EmptyEntities;
	};

}
