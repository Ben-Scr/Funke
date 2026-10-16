#pragma once
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Components/UI/MaskComponent.hpp"
#include "Scene/EntityHandle.hpp"

#include <entt/entity/registry.hpp>
#include <algorithm>
#include <utility>
#include <vector>

namespace Index::UIDrawOrder {

	// Stride 4 (not 1) so GuiRenderer can slot per-widget overlay draws (selection, caret) between a parent and its first child without a separate key.
	inline constexpr int k_HierarchyStep = 4;

	inline void Collect(entt::registry& registry, EntityHandle entity,
		std::vector<std::pair<EntityHandle, int>>& outOrder, int& counter)
	{
		if (registry.all_of<DisabledTag>(entity)) return;
		if (registry.all_of<RectTransform2DComponent>(entity)) {
			outOrder.emplace_back(entity, counter);
			counter += k_HierarchyStep;
		}
		if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
			for (EntityHandle child : hierarchy->Children) {
				if (registry.valid(child)) {
					Collect(registry, child, outOrder, counter);
				}
			}
		}
	}

	// GuiRenderer and UIEventSystem both consume this list — the renderer's z-stack and the hit-test stack MUST agree, so both go through this single helper.
	inline void Build(entt::registry& registry,
		std::vector<std::pair<EntityHandle, int>>& outOrder)
	{
		auto uiView = registry.view<RectTransform2DComponent>();
		if (uiView.size() == 0) return;

		// Top-most ancestor per entity, memoized by entity index so shared ancestor chains are walked once.
		// t_Excluded marks chains with a disabled ancestor; a slot is valid only while its stamp is current.
		thread_local std::vector<EntityHandle> t_RootOf;
		thread_local std::vector<uint8_t> t_Excluded;
		thread_local std::vector<uint32_t> t_Stamp;
		thread_local uint32_t t_Serial = 0;
		thread_local std::vector<EntityHandle> t_Path;
		thread_local std::vector<EntityHandle> t_Roots;
		if (++t_Serial == 0) {
			std::fill(t_Stamp.begin(), t_Stamp.end(), 0u);
			t_Serial = 1;
		}
		auto memoSlot = [&](EntityHandle e) -> size_t {
			const size_t index = entt::to_entity(e);
			if (index >= t_Stamp.size()) {
				t_Stamp.resize(index + 1, 0u);
				t_RootOf.resize(index + 1, entt::null);
				t_Excluded.resize(index + 1, 0);
			}
			return index;
		};

		// rootOf(X) = top-most ancestor reached from X; excluded if any strict ancestor of X is disabled.
		t_Roots.clear();
		for (auto entity : uiView) {
			if (registry.all_of<DisabledTag>(entity)) continue;

			t_Path.clear();
			EntityHandle cur = entity;
			EntityHandle result = entt::null;
			bool excluded = false;
			bool resolved = false;
			for (int hop = 0; hop < 4096; ++hop) {
				const size_t slot = memoSlot(cur);
				if (t_Stamp[slot] == t_Serial) {
					result = t_RootOf[slot];
					excluded = t_Excluded[slot] != 0;
					resolved = true;
					break;
				}
				t_Path.push_back(cur);
				const HierarchyComponent* hc = registry.try_get<HierarchyComponent>(cur);
				if (!hc || hc->Parent == entt::null || !registry.valid(hc->Parent)) { result = cur; resolved = true; break; }
				if (registry.all_of<DisabledTag>(hc->Parent)) { excluded = true; resolved = true; break; }
				cur = hc->Parent;
			}
			if (!resolved) result = cur;  // hop limit: same fallback as an unbounded walk's last node
			for (EntityHandle onPath : t_Path) {
				const size_t slot = memoSlot(onPath);
				t_Stamp[slot] = t_Serial;
				t_RootOf[slot] = result;
				t_Excluded[slot] = excluded ? 1 : 0;
			}
			if (!excluded) t_Roots.push_back(result);
		}

		if (t_Roots.empty()) return;

		const auto& entityStorage = registry.storage<entt::entity>();
		std::sort(t_Roots.begin(), t_Roots.end(),
			[&](EntityHandle a, EntityHandle b) {
				return entityStorage.index(a) < entityStorage.index(b);
			});
		t_Roots.erase(std::unique(t_Roots.begin(), t_Roots.end()), t_Roots.end());

		int counter = 0;
		for (EntityHandle entity : t_Roots) {
			Collect(registry, entity, outOrder, counter);
		}
	}

	// Entity → position in a Build() result without hashing; valid until the order vector changes.
	class OrderLookup {
	public:
		static constexpr uint32_t k_None = UINT32_MAX;

		void Rebuild(const std::vector<std::pair<EntityHandle, int>>& order) {
			std::fill(m_Slots.begin(), m_Slots.end(), k_None);
			m_Order = &order;
			for (uint32_t i = 0; i < static_cast<uint32_t>(order.size()); ++i) {
				const size_t index = entt::to_entity(order[i].first);
				if (index >= m_Slots.size()) m_Slots.resize(index + 1, k_None);
				m_Slots[index] = i;
			}
		}

		uint32_t SlotOf(EntityHandle entity) const {
			if (entity == entt::null || !m_Order) return k_None;
			const size_t index = entt::to_entity(entity);
			if (index >= m_Slots.size()) return k_None;
			const uint32_t slot = m_Slots[index];
			return slot != k_None && (*m_Order)[slot].first == entity ? slot : k_None;
		}

		const int* DrawIndexOf(EntityHandle entity) const {
			const uint32_t slot = SlotOf(entity);
			return slot == k_None ? nullptr : &(*m_Order)[slot].second;
		}

	private:
		std::vector<uint32_t> m_Slots;
		const std::vector<std::pair<EntityHandle, int>>* m_Order = nullptr;
	};

	// Mirrors GuiRenderer's ResolveClipForEntity: a point only hits if it lies inside every ancestor Mask rect. Texture-alpha masks are approximated by their rect.
	inline bool PassesAncestorMasks(entt::registry& registry, EntityHandle entity, const Vec2& point)
	{
		EntityHandle current = entity;
		for (int hop = 0; hop < 4096; ++hop) {
			const HierarchyComponent* hc = registry.try_get<HierarchyComponent>(current);
			if (!hc || hc->Parent == entt::null || !registry.valid(hc->Parent)) return true;
			current = hc->Parent;
			if (!registry.all_of<MaskComponent>(current)) continue;
			const auto* rect = registry.try_get<RectTransform2DComponent>(current);
			if (rect && !rect->ContainsPoint(point)) return false;
		}
		return true;
	}

	inline bool HitTest(entt::registry& registry, EntityHandle entity,
		const RectTransform2DComponent& rect, const Vec2& point)
	{
		return rect.ContainsPoint(point) && PassesAncestorMasks(registry, entity, point);
	}

}
