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
