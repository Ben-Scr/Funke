#include <doctest/doctest.h>
#include "Graphics/QuadVisibility.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/UI/MaskComponent.hpp"
#include "Gui/CircularSliderGeometry.hpp"
#include "Gui/UIDrawOrder.hpp"

#include <entt/entity/registry.hpp>

using namespace Funke;

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

TEST_CASE("Circular slider hit-testing follows the scaled ring and leaves the center empty")
{
	RectTransform2DComponent rect;
	rect.SizeDelta = { 200.0f, 200.0f };
	CircularSliderComponent slider;
	auto shape = CircularSliderGeometry::Resolve(slider, rect);
	CHECK(shape.OuterRadius == doctest::Approx(100.0f));
	CHECK(shape.Thickness == doctest::Approx(20.0f));
	CHECK(CircularSliderGeometry::Contains(shape, { 90.0f, 0.0f }));
	CHECK_FALSE(CircularSliderGeometry::Contains(shape, { 79.0f, 0.0f }));
	CHECK_FALSE(CircularSliderGeometry::Contains(shape, { 101.0f, 0.0f }));
	CHECK_FALSE(CircularSliderGeometry::Contains(shape, { 0.0f, 0.0f }));

	rect.LocalScale = { 2.0f, 2.0f };
	shape = CircularSliderGeometry::Resolve(slider, rect);
	CHECK(shape.OuterRadius == doctest::Approx(200.0f));
	CHECK(shape.Thickness == doctest::Approx(40.0f));
	CHECK(CircularSliderGeometry::Contains(shape, { 180.0f, 0.0f }));
	CHECK_FALSE(CircularSliderGeometry::Contains(shape, { 159.0f, 0.0f }));

	rect.ResolvedValid = true;
	rect.ResolvedMin = { -200.0f, -100.0f };
	rect.ResolvedMax = { 200.0f, 100.0f };
	rect.Scale = { 2.0f, 1.0f };
	shape = CircularSliderGeometry::Resolve(slider, rect);
	CHECK(shape.Radius == doctest::Approx(90.0f));
	rect.ResolvedMin = { -50.0f, -50.0f };
	rect.ResolvedMax = { 50.0f, 50.0f };
	rect.Scale = { 0.5f, 0.5f };
	shape = CircularSliderGeometry::Resolve(slider, rect);
	CHECK(shape.Thickness == doctest::Approx(10.0f));
	CHECK(CircularSliderGeometry::Contains(shape, { 45.0f, 0.0f }));
	CHECK_FALSE(CircularSliderGeometry::Contains(shape, { 39.0f, 0.0f }));
}

TEST_CASE("Circular slider partial arcs reject the gap and include rounded end caps")
{
	RectTransform2DComponent rect;
	rect.SizeDelta = { 200.0f, 200.0f };
	CircularSliderComponent slider;
	slider.SweepDegrees = 90.0f;
	auto shape = CircularSliderGeometry::Resolve(slider, rect);
	CHECK(CircularSliderGeometry::Contains(shape, { 63.64f, 63.64f }));
	CHECK(CircularSliderGeometry::Contains(shape, { -5.0f, 90.0f }));
	CHECK(CircularSliderGeometry::Contains(shape, { 90.0f, -5.0f }));
	CHECK_FALSE(CircularSliderGeometry::Contains(shape, { -90.0f, 0.0f }));
	CHECK_FALSE(CircularSliderGeometry::Contains(shape, { 90.0f, -11.0f }));

	slider.Clockwise = false;
	shape = CircularSliderGeometry::Resolve(slider, rect);
	CHECK(CircularSliderGeometry::Contains(shape, { -63.64f, 63.64f }));
	CHECK_FALSE(CircularSliderGeometry::Contains(shape, { 63.64f, 63.64f }));
	rect.Rotation = CircularSliderGeometry::k_Pi;
	shape = CircularSliderGeometry::Resolve(slider, rect);
	CHECK(CircularSliderGeometry::Contains(shape, { 63.64f, -63.64f }));
	CHECK_FALSE(CircularSliderGeometry::Contains(shape, { -63.64f, 63.64f }));
}

TEST_CASE("Circular slider geometry rotates about its pivot and places the handle on the ring")
{
	RectTransform2DComponent rect;
	rect.SizeDelta = { 200.0f, 200.0f };
	rect.Pivot = { 0.0f, 0.0f };
	rect.Rotation = CircularSliderGeometry::k_Pi * 0.5f;
	CircularSliderComponent slider;
	slider.Value = 0.25f;
	const auto shape = CircularSliderGeometry::Resolve(slider, rect);
	CHECK(shape.Center.x == doctest::Approx(-100.0f));
	CHECK(shape.Center.y == doctest::Approx(100.0f));
	const Vec2 handle = CircularSliderGeometry::HandlePosition(shape);
	CHECK(handle.x == doctest::Approx(-100.0f));
	CHECK(handle.y == doctest::Approx(190.0f));
	CHECK(CircularSliderGeometry::Contains(shape, handle));
}

TEST_CASE("Circular slider empty and oversized arcs have bounded geometry")
{
	RectTransform2DComponent rect;
	CircularSliderComponent slider;
	slider.SweepDegrees = 0.0f;
	CHECK_FALSE(CircularSliderGeometry::Contains(CircularSliderGeometry::Resolve(slider, rect), { 40.0f, 0.0f }));
	slider.SweepDegrees = 720.0f;
	CHECK(std::abs(CircularSliderGeometry::Resolve(slider, rect).SweepRadians) == doctest::Approx(CircularSliderGeometry::k_Tau));
	slider.RingThickness = -1.0f;
	CHECK_FALSE(CircularSliderGeometry::Contains(CircularSliderGeometry::Resolve(slider, rect), { 40.0f, 0.0f }));
	slider.RingThickness = 1000.0f;
	CHECK(CircularSliderGeometry::Resolve(slider, rect).Thickness == doctest::Approx(50.0f));
}

TEST_CASE("UI quad culling retains partial overlap rotation and perspective crossings") {
	using namespace Funke;
	const glm::mat4 identity{ 1.0f };
	const QuadVisibility::Clip clip;
	const std::array<Vec2, 4> inside{ Vec2{ -0.5f, -0.5f }, Vec2{ 0.5f, -0.5f },
		Vec2{ 0.5f, 0.5f }, Vec2{ -0.5f, 0.5f } };
	CHECK(QuadVisibility::Classify(inside, identity, clip) == QuadVisibility::Visibility::Inside);
	CHECK_FALSE(QuadVisibility::IsVisible(inside, glm::translate(identity, glm::vec3{ 2.0f, 0.0f, 0.0f }), clip));
	CHECK(QuadVisibility::IsVisible(inside, glm::translate(identity, glm::vec3{ 1.25f, 0.0f, 0.0f }), clip));
	const auto rotated = glm::rotate(glm::translate(identity, glm::vec3{ 1.6f, 0.0f, 0.0f }),
		0.785398163f, glm::vec3{ 0.0f, 0.0f, 1.0f });
	CHECK(QuadVisibility::IsVisible(inside, rotated, clip));
	CHECK(QuadVisibility::IsVisible(inside, glm::scale(identity, glm::vec3{ -1.0f, -1.0f, 1.0f }), clip));
	const std::array<Vec2, 4> spanning{ Vec2{ -2.0f, -2.0f }, Vec2{ 2.0f, -2.0f },
		Vec2{ 2.0f, 2.0f }, Vec2{ -2.0f, 2.0f } };
	CHECK(QuadVisibility::Classify(spanning, identity, clip) == QuadVisibility::Visibility::Intersecting);
	auto perspective = identity;
	perspective[0][3] = 4.0f;
	CHECK(QuadVisibility::IsVisible(inside, perspective, clip));
	auto nonFinite = inside;
	nonFinite[0].x = std::numeric_limits<float>::quiet_NaN();
	CHECK(QuadVisibility::IsVisible(nonFinite, identity, clip));
}

TEST_CASE("UI quad culling uses letterboxed scissor pixels and preserves edge samples") {
	using namespace Funke;
	const auto clip = QuadVisibility::FromScissor(350, 300, 100, 100, 100, 200, 600, 400);
	CHECK(clip.Min.x == doctest::Approx(-1.0f / 6.0f));
	CHECK(clip.Max.x == doctest::Approx(1.0f / 6.0f));
	CHECK(clip.Min.y == doctest::Approx(0.0f));
	CHECK(clip.Max.y == doctest::Approx(0.5f));
	const std::array<Vec2, 4> hiddenRow{ Vec2{ -0.1f, -0.4f }, Vec2{ 0.1f, -0.4f },
		Vec2{ 0.1f, -0.1f }, Vec2{ -0.1f, -0.1f } };
	CHECK_FALSE(QuadVisibility::IsVisible(hiddenRow, glm::mat4{ 1.0f }, clip));
	const auto scrolled = glm::translate(glm::mat4{ 1.0f }, glm::vec3{ 0.0f, 0.1f, 0.0f });
	CHECK(QuadVisibility::IsVisible(hiddenRow, scrolled, clip));
	const auto zeroViewport = QuadVisibility::FromScissor(1, 1, 1, 1, 0, 0, 0, 0);
	CHECK(zeroViewport.Min.x == -1.0f);
	CHECK(zeroViewport.Max.x == 1.0f);
}
