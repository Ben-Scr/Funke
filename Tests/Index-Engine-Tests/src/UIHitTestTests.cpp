#include <doctest/doctest.h>

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/UI/MaskComponent.hpp"
#include "Gui/UIDrawOrder.hpp"

#include <entt/entity/registry.hpp>

using namespace Index;

namespace {
	EntityHandle MakeRect(entt::registry& registry, EntityHandle parent, Vec2 position, Vec2 size) {
		const EntityHandle e = registry.create();
		auto& rect = registry.emplace<RectTransform2DComponent>(e);
		rect.AnchoredPosition = position;
		rect.SizeDelta = size;
		auto& hc = registry.emplace<HierarchyComponent>(e);
		hc.Parent = parent;
		if (parent != entt::null) {
			registry.get<HierarchyComponent>(parent).Children.push_back(e);
		}
		return e;
	}

	bool Hit(entt::registry& registry, EntityHandle e, Vec2 point) {
		return UIDrawOrder::HitTest(registry, e, registry.get<RectTransform2DComponent>(e), point);
	}
}

TEST_CASE("UI hit-test rejects points clipped by an ancestor mask")
{
	entt::registry registry;
	const EntityHandle viewport = MakeRect(registry, entt::null, { 0.0f, 0.0f }, { 100.0f, 100.0f });
	registry.emplace<MaskComponent>(viewport);
	const EntityHandle content = MakeRect(registry, viewport, { 0.0f, 0.0f }, { 400.0f, 100.0f });
	const EntityHandle inside = MakeRect(registry, content, { 0.0f, 0.0f }, { 40.0f, 40.0f });
	const EntityHandle scrolledOut = MakeRect(registry, content, { 150.0f, 0.0f }, { 40.0f, 40.0f });

	CHECK(Hit(registry, inside, { 0.0f, 0.0f }));
	CHECK_FALSE(Hit(registry, scrolledOut, { 150.0f, 0.0f }));
	CHECK_FALSE(Hit(registry, content, { 150.0f, 0.0f }));
	CHECK(Hit(registry, viewport, { 0.0f, 0.0f }));

	registry.remove<MaskComponent>(viewport);
	CHECK(Hit(registry, scrolledOut, { 150.0f, 0.0f }));
}

TEST_CASE("UI hit-test intersects nested masks")
{
	entt::registry registry;
	const EntityHandle outer = MakeRect(registry, entt::null, { 0.0f, 0.0f }, { 100.0f, 100.0f });
	registry.emplace<MaskComponent>(outer);
	const EntityHandle inner = MakeRect(registry, outer, { 50.0f, 0.0f }, { 100.0f, 100.0f });
	registry.emplace<MaskComponent>(inner);
	const EntityHandle leaf = MakeRect(registry, inner, { 0.0f, 0.0f }, { 300.0f, 300.0f });

	CHECK(Hit(registry, leaf, { 25.0f, 0.0f }));
	CHECK_FALSE(Hit(registry, leaf, { -25.0f, 0.0f }));
	CHECK_FALSE(Hit(registry, leaf, { 75.0f, 0.0f }));
}

TEST_CASE("UI draw order skips subtrees under a disabled ancestor and keeps child order")
{
	entt::registry registry;
	const EntityHandle root = registry.create();
	registry.emplace<HierarchyComponent>(root);
	const EntityHandle first = MakeRect(registry, root, { 0.0f, 0.0f }, { 10.0f, 10.0f });
	const EntityHandle second = MakeRect(registry, root, { 0.0f, 0.0f }, { 10.0f, 10.0f });
	const EntityHandle grandchild = MakeRect(registry, second, { 0.0f, 0.0f }, { 10.0f, 10.0f });

	const EntityHandle hiddenGroup = registry.create();
	registry.emplace<HierarchyComponent>(hiddenGroup).Parent = root;
	registry.get<HierarchyComponent>(root).Children.push_back(hiddenGroup);
	registry.emplace<DisabledTag>(hiddenGroup);
	const EntityHandle hidden = MakeRect(registry, hiddenGroup, { 0.0f, 0.0f }, { 10.0f, 10.0f });

	std::vector<std::pair<EntityHandle, int>> order;
	UIDrawOrder::Build(registry, order);
	REQUIRE(order.size() == 3);
	CHECK(order[0].first == first);
	CHECK(order[1].first == second);
	CHECK(order[2].first == grandchild);

	UIDrawOrder::OrderLookup lookup;
	lookup.Rebuild(order);
	REQUIRE(lookup.DrawIndexOf(grandchild) != nullptr);
	CHECK(*lookup.DrawIndexOf(grandchild) == order[2].second);
	CHECK(lookup.DrawIndexOf(hidden) == nullptr);
	CHECK(lookup.DrawIndexOf(root) == nullptr);
}
