#include <doctest/doctest.h>

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/UI/ButtonComponent.hpp"
#include "Components/UI/InputFieldComponent.hpp"
#include "Components/UI/ScrollRectComponent.hpp"
#include "Components/UI/SliderComponent.hpp"
#include "Systems/UIEventSystem.hpp"

#include <entt/entity/registry.hpp>

using namespace Index;

namespace {
	EntityHandle MakeChild(entt::registry& registry, EntityHandle parent) {
		const EntityHandle e = registry.create();
		auto& hc = registry.emplace<HierarchyComponent>(e);
		hc.Parent = parent;
		if (parent != entt::null) {
			registry.get<HierarchyComponent>(parent).Children.push_back(e);
		}
		return e;
	}
}

TEST_CASE("A press on a button inside a scroll rect can become a scroll drag")
{
	entt::registry registry;
	const EntityHandle list = MakeChild(registry, entt::null);
	registry.emplace<ScrollRectComponent>(list);
	const EntityHandle content = MakeChild(registry, list);
	const EntityHandle row = MakeChild(registry, content);
	registry.emplace<ButtonComponent>(row);
	const EntityHandle rowLabel = MakeChild(registry, row);

	CHECK(UIEventSystem::CanScrollRectTakeOverPress(registry, row, list));
	CHECK(UIEventSystem::CanScrollRectTakeOverPress(registry, rowLabel, list));
	CHECK_FALSE(UIEventSystem::CanScrollRectTakeOverPress(registry, list, list));
	CHECK_FALSE(UIEventSystem::CanScrollRectTakeOverPress(registry, entt::null, list));

	const EntityHandle outside = MakeChild(registry, entt::null);
	CHECK_FALSE(UIEventSystem::CanScrollRectTakeOverPress(registry, outside, list));
}

TEST_CASE("Widgets that drag on their own keep their press inside a scroll rect")
{
	entt::registry registry;
	const EntityHandle list = MakeChild(registry, entt::null);
	registry.emplace<ScrollRectComponent>(list);
	const EntityHandle content = MakeChild(registry, list);

	const EntityHandle slider = MakeChild(registry, content);
	registry.emplace<SliderComponent>(slider);
	const EntityHandle field = MakeChild(registry, content);
	registry.emplace<InputFieldComponent>(field);
	const EntityHandle nested = MakeChild(registry, content);
	registry.emplace<ScrollRectComponent>(nested);

	CHECK_FALSE(UIEventSystem::CanScrollRectTakeOverPress(registry, slider, list));
	CHECK_FALSE(UIEventSystem::CanScrollRectTakeOverPress(registry, field, list));
	CHECK_FALSE(UIEventSystem::CanScrollRectTakeOverPress(registry, nested, list));
}

TEST_CASE("A slider thumb overhanging the track still grabs the slider")
{
	entt::registry registry;
	const EntityHandle slider = MakeChild(registry, entt::null);
	auto& track = registry.emplace<RectTransform2DComponent>(slider);
	track.ResolvedMin = Vec2{ 0.0f, 0.0f };
	track.ResolvedMax = Vec2{ 200.0f, 10.0f };
	track.ResolvedValid = true;

	// Round thumb at the right end, taller and wider than the track; no Interactable of its own.
	const EntityHandle handle = MakeChild(registry, slider);
	auto& thumb = registry.emplace<RectTransform2DComponent>(handle);
	thumb.ResolvedMin = Vec2{ 185.0f, -10.0f };
	thumb.ResolvedMax = Vec2{ 215.0f, 20.0f };
	thumb.ResolvedValid = true;
	registry.emplace<SliderComponent>(slider).HandleEntity = handle;

	CHECK(UIEventSystem::SliderHandleContains(registry, slider, Vec2{ 210.0f, 5.0f }));
	CHECK(UIEventSystem::SliderHandleContains(registry, slider, Vec2{ 200.0f, 18.0f }));
	CHECK_FALSE(UIEventSystem::SliderHandleContains(registry, slider, Vec2{ 100.0f, 5.0f }));

	// A read-only slider (progress bar) never takes input through its handle.
	registry.get<SliderComponent>(slider).IsReadOnly = true;
	CHECK_FALSE(UIEventSystem::SliderHandleContains(registry, slider, Vec2{ 210.0f, 5.0f }));
}

TEST_CASE("A long-press selects the word under the finger")
{
	InputFieldComponent field;
	field.Text = "Hallo schöne Welt!";

	UIEventSystem::SelectWordAt(field, 2);
	CHECK(field.Text.substr(field.SelectionAnchorBytePos, field.CaretBytePos - field.SelectionAnchorBytePos) == "Hallo");

	// Multi-byte letters belong to the word.
	UIEventSystem::SelectWordAt(field, 8);
	CHECK(field.Text.substr(field.SelectionAnchorBytePos, field.CaretBytePos - field.SelectionAnchorBytePos) == "schöne");

	// Right after a word still means that word; after the "!" there is none.
	UIEventSystem::SelectWordAt(field, static_cast<int>(field.Text.size()) - 1);
	CHECK(field.Text.substr(field.SelectionAnchorBytePos, field.CaretBytePos - field.SelectionAnchorBytePos) == "Welt");
	UIEventSystem::SelectWordAt(field, static_cast<int>(field.Text.size()));
	CHECK(field.CaretBytePos == static_cast<int>(field.Text.size()));
	CHECK(field.SelectionAnchorBytePos == static_cast<int>(field.Text.size()));

	field.Text = "a  b";
	UIEventSystem::SelectWordAt(field, 2);
	CHECK(field.SelectionAnchorBytePos == field.CaretBytePos);
}

TEST_CASE("The mouse wheel scrolls the list around the row under the cursor")
{
	entt::registry registry;
	const EntityHandle page = MakeChild(registry, entt::null);
	registry.emplace<ScrollRectComponent>(page).Vertical = true;
	const EntityHandle content = MakeChild(registry, page);
	const EntityHandle row = MakeChild(registry, content);
	registry.emplace<ButtonComponent>(row);

	CHECK(UIEventSystem::WheelScrollTarget(registry, row) == page);
	CHECK(UIEventSystem::WheelScrollTarget(registry, page) == page);

	// A horizontal carousel inside the page leaves the (vertical) wheel to the page.
	const EntityHandle carousel = MakeChild(registry, content);
	auto& carouselScroll = registry.emplace<ScrollRectComponent>(carousel);
	carouselScroll.Vertical = false;
	carouselScroll.Horizontal = true;
	const EntityHandle card = MakeChild(registry, carousel);
	CHECK(UIEventSystem::WheelScrollTarget(registry, card) == page);

	// With no vertical list around it, the carousel takes the wheel.
	const EntityHandle lone = MakeChild(registry, entt::null);
	auto& loneScroll = registry.emplace<ScrollRectComponent>(lone);
	loneScroll.Vertical = false;
	loneScroll.Horizontal = true;
	const EntityHandle loneCard = MakeChild(registry, lone);
	CHECK(UIEventSystem::WheelScrollTarget(registry, loneCard) == lone);

	const EntityHandle notes = MakeChild(registry, content);
	registry.emplace<InputFieldComponent>(notes).Multiline = true;
	CHECK(UIEventSystem::WheelScrollTarget(registry, notes) == entt::null);

	const EntityHandle outside = MakeChild(registry, entt::null);
	CHECK(UIEventSystem::WheelScrollTarget(registry, outside) == entt::null);
}
