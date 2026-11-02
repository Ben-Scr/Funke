#include <doctest/doctest.h>

#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Gui/UICanvasDrawOrder.hpp"

using namespace Funke;

namespace {
	EntityHandle AddNode(entt::registry& registry, EntityHandle parent = entt::null, bool rect = true) {
		const EntityHandle entity = registry.create();
		if (rect) registry.emplace<RectTransform2DComponent>(entity);
		registry.emplace<HierarchyComponent>(entity).Parent = parent;
		if (parent != entt::null) registry.get<HierarchyComponent>(parent).Children.push_back(entity);
		return entity;
	}

	void CheckCanvas(const UIDrawOrder::CanvasOrders& orders, EntityHandle root,
		const std::vector<std::pair<EntityHandle, int>>& expected) {
		const auto found = orders.GetCanvases().find(root);
		REQUIRE(found != orders.GetCanvases().end());
		CHECK(found->second == expected);
	}
}

TEST_CASE("UI canvas orders preserve nested canvas boundaries and painter indices") {
	entt::registry registry;
	const auto screen = AddNode(registry);
	const auto canvas2D = AddNode(registry, screen);
	registry.emplace<Transform2DComponent>(canvas2D);
	const auto bridge = AddNode(registry, canvas2D, false);
	const auto child2D = AddNode(registry, bridge);
	const auto canvas3D = AddNode(registry, bridge);
	registry.emplace<Transform3DComponent>(canvas3D);
	const auto child3D = AddNode(registry, canvas3D);
	const auto hidden = AddNode(registry, screen, false);
	registry.emplace<DisabledTag>(hidden);
	const auto hiddenCanvas = AddNode(registry, hidden);
	registry.emplace<Transform2DComponent>(hiddenCanvas);
	AddNode(registry, hiddenCanvas);
	const auto overlay = AddNode(registry, screen);

	UIDrawOrder::CanvasOrders orders;
	orders.Rebuild(registry);
	REQUIRE(orders.GetCanvases().size() == 3);
	CheckCanvas(orders, entt::null, { { screen, 0 }, { overlay, 20 } });
	CheckCanvas(orders, canvas2D, { { canvas2D, 4 }, { child2D, 8 } });
	CheckCanvas(orders, canvas3D, { { canvas3D, 12 }, { child3D, 16 } });

	std::vector<std::pair<EntityHandle, int>> oldOrder;
	UIDrawOrder::Build(registry, oldOrder);
	for (const auto& [root, order] : orders.GetCanvases()) {
		auto filtered = oldOrder;
		std::erase_if(filtered, [&](const auto& entry) { return WorldUI::FindRoot(registry, entry.first) != root; });
		CHECK(order == filtered);
	}
}

TEST_CASE("UI canvas orders refresh after edits and scene changes") {
	entt::registry registry;
	const auto first = AddNode(registry);
	registry.emplace<Transform2DComponent>(first);
	const auto child = AddNode(registry, first);
	const auto second = AddNode(registry);
	registry.emplace<Transform3DComponent>(second);
	UIDrawOrder::CanvasOrders orders;
	orders.Rebuild(registry);
	REQUIRE(orders.GetCanvases().size() == 2);

	registry.get<HierarchyComponent>(first).Children.clear();
	registry.get<HierarchyComponent>(second).Children.push_back(child);
	registry.get<HierarchyComponent>(child).Parent = second;
	registry.remove<Transform2DComponent>(first);
	orders.Rebuild(registry);
	CHECK(orders.GetCanvases().count(first) == 0);
	CheckCanvas(orders, entt::null, { { first, 0 } });
	CheckCanvas(orders, second, { { second, 4 }, { child, 8 } });

	registry.emplace<DisabledTag>(second);
	orders.Rebuild(registry);
	REQUIRE(orders.GetCanvases().size() == 1);
	CheckCanvas(orders, entt::null, { { first, 0 } });
	registry.clear();
	orders.Rebuild(registry);
	CHECK(orders.GetCanvases().empty());

	entt::registry otherScene;
	const auto other = AddNode(otherScene);
	orders.Rebuild(otherScene);
	REQUIRE(orders.GetCanvases().size() == 1);
	CheckCanvas(orders, entt::null, { { other, 0 } });
}

TEST_CASE("UI order lookup rejects stale slots across shrinking and recycled orders") {
	entt::registry registry;
	const auto first = registry.create();
	const auto second = registry.create();
	const auto third = registry.create();
	std::vector<std::pair<EntityHandle, int>> order{ { first, 0 }, { second, 4 }, { third, 8 } };
	UIDrawOrder::OrderLookup lookup;
	lookup.Rebuild(order);
	CHECK(lookup.SlotOf(third) == 2);
	order = { { second, 12 } };
	lookup.Rebuild(order);
	CHECK(lookup.SlotOf(first) == UIDrawOrder::OrderLookup::k_None);
	CHECK(lookup.SlotOf(third) == UIDrawOrder::OrderLookup::k_None);
	REQUIRE(lookup.DrawIndexOf(second));
	CHECK(*lookup.DrawIndexOf(second) == 12);

	registry.destroy(second);
	const auto replacement = registry.create();
	REQUIRE(entt::to_entity(replacement) == entt::to_entity(second));
	order = { { replacement, 24 } };
	lookup.Rebuild(order);
	CHECK(lookup.SlotOf(second) == UIDrawOrder::OrderLookup::k_None);
	REQUIRE(lookup.DrawIndexOf(replacement));
	CHECK(*lookup.DrawIndexOf(replacement) == 24);
	order.clear();
	lookup.Rebuild(order);
	CHECK(lookup.SlotOf(replacement) == UIDrawOrder::OrderLookup::k_None);
	CHECK(lookup.SlotOf(entt::null) == UIDrawOrder::OrderLookup::k_None);
}
