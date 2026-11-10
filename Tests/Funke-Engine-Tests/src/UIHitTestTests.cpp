#include <doctest/doctest.h>
#include "Graphics/QuadVisibility.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/UI/MaskComponent.hpp"
#include "Gui/CircularSliderGeometry.hpp"
#include "Gui/UIDrawOrder.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/UI/InteractableComponent.hpp"
#include "Components/UI/DropdownComponent.hpp"
#include "Components/UI/SliderComponent.hpp"
#include "Components/UI/InputFieldComponent.hpp"
#include "Scene/Scene.hpp"
#include "Systems/UIEventSystem.hpp"
#include "Systems/UIFocusSystem.hpp"
#include "Core/Application.hpp"
#include "Core/Window.hpp"

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

	struct NavigationFixture {
		std::shared_ptr<Scene> ScenePtr = Scene::CreateDetachedScene("Keyboard UI");
		entt::registry& Registry = ScenePtr->GetRegistry();
		EntityHandle Root = MakeRect(Registry, entt::null, {}, { 640, 360 });
		EntityHandle First = MakeRect(Registry, Root, {}, { 100, 40 });
		EntityHandle Second = MakeRect(Registry, Root, { 120, 0 }, { 100, 40 });
		UIFocusSystem Focus;

		NavigationFixture() {
			Registry.emplace<InteractableComponent>(First).Focusable = true;
			Registry.emplace<InteractableComponent>(Second).Focusable = true;
			Step({ .Tab = true });
		}
		void Step(const UIFocusSystem::NavigationInput& input = {}) { Focus.UpdateNavigation(*ScenePtr, input); }
		bool Focused(EntityHandle entity) { return Registry.get<InteractableComponent>(entity).IsFocused; }
	};

	struct SurfaceGuard {
		Window::UIRegion Previous = Window::GetUIRegion();
		explicit SurfaceGuard(int offset) { Window::SetUIRegion(offset, offset, 640, 360); }
		~SurfaceGuard() {
			Window::ClearUIRegion();
			if (Previous.Source == Window::UIRegionSource::AspectLock) Window::PublishAspectLockUIRegion(1.0f, Previous);
			else if (Previous.IsActive()) Window::SetUIRegion(Previous.OffsetX, Previous.OffsetY, Previous.Width, Previous.Height, Previous.WindowToRegionScale);
		}
	};
}

TEST_CASE("UI keyboard dropdown previews without committing and cancels without losing focus") {
	NavigationFixture ui;
	auto& dd = ui.Registry.emplace<DropdownComponent>(ui.First);
	dd.Options = { "First", "Second", "Third" };
	ui.Step({ .Activate = true });
	CHECK(dd.IsOpen);
	CHECK(dd.NavigationIndex == 0);
	ui.Step({ .Down = true });
	CHECK(dd.NavigationIndex == 1);
	CHECK(dd.SelectedIndex == 0);
	CHECK(ui.Focused(ui.First));
	ui.Step({ .Cancel = true });
	CHECK_FALSE(dd.IsOpen);
	CHECK(dd.NavigationIndex == -1);
	CHECK(dd.SelectedIndex == 0);
	CHECK(ui.Focused(ui.First));
	ui.Step({ .Activate = true });
	ui.Step({ .End = true });
	ui.Step({ .Down = true });
	CHECK(dd.NavigationIndex == 2);
	ui.Step({ .Home = true });
	ui.Step({ .Up = true });
	CHECK(dd.NavigationIndex == 0);
	ui.Step({ .Down = true });
	ui.Step({ .Activate = true });
	CHECK_FALSE(dd.IsOpen);
	CHECK(dd.SelectedIndex == 1);
	CHECK(dd.SelectionObserved);
	CHECK(dd.LastObservedSelectedIndex == 0);
	CHECK_FALSE(ui.Registry.get<InteractableComponent>(ui.First).ActivatedThisFrame);
	ui.Step({ .Activate = true });
	ui.Step({ .Down = true });
	ui.Step({ .Tab = true });
	CHECK_FALSE(dd.IsOpen);
	CHECK(dd.SelectedIndex == 1);
	CHECK(ui.Focused(ui.Second));
	ui.Step({ .Tab = true, .Shift = true });
	CHECK(ui.Focused(ui.First));
}

TEST_CASE("UI keyboard dropdown tolerates empty changed read-only and disabled controls") {
	NavigationFixture ui;
	auto& dd = ui.Registry.emplace<DropdownComponent>(ui.First);
	ui.Step({ .Activate = true });
	CHECK_FALSE(dd.IsOpen);
	dd.Options = { "One", "Two", "Three" };
	dd.IsReadOnly = true;
	ui.Step({ .Activate = true });
	CHECK_FALSE(dd.IsOpen);
	dd.IsReadOnly = false;
	ui.Step({ .Activate = true });
	ui.Step({ .End = true });
	dd.Options.resize(1);
	ui.Step({ .Activate = true });
	CHECK(dd.SelectedIndex == 0);
	ui.Step({ .Activate = true });
	ui.Step({ .Enabled = false, .Tab = true, .Down = true, .Activate = true, .Cancel = true });
	CHECK(dd.IsOpen);
	CHECK(ui.Focused(ui.First));
	ui.Registry.emplace<DisabledTag>(ui.Root);
	ui.Step({ .Activate = true });
	CHECK_FALSE(dd.IsOpen);
	CHECK_FALSE(ui.Focused(ui.First));
	ui.Registry.remove<DisabledTag>(ui.Root);
	ui.Step({ .Tab = true });
	ui.Step({ .Activate = true });
	dd.Options.clear();
	ui.Step({ .Down = true });
	CHECK_FALSE(dd.IsOpen);
	CHECK(dd.NavigationIndex == -1);
}

TEST_CASE("UI keyboard sliders respect direction steps limits and preserve Tab navigation") {
	for (auto direction : { SliderDirection::LeftToRight, SliderDirection::RightToLeft,
		SliderDirection::BottomToTop, SliderDirection::TopToBottom }) {
		CAPTURE(static_cast<int>(direction));
		NavigationFixture ui;
		auto& slider = ui.Registry.emplace<SliderComponent>(ui.First);
		slider.Direction = direction;
		slider.MinValue = 0;
		slider.MaxValue = 100;
		slider.Value = 50;
		const bool horizontal = direction == SliderDirection::LeftToRight || direction == SliderDirection::RightToLeft;
		const bool reversed = direction == SliderDirection::RightToLeft || direction == SliderDirection::TopToBottom;
		ui.Step({ .Right = horizontal, .Up = !horizontal });
		CHECK(slider.Value == doctest::Approx(reversed ? 49.0f : 51.0f));
		CHECK(slider.LastObservedValue == 50);
		CHECK(slider.ValueObserved);
		CHECK(ui.Focused(ui.First));
		ui.Step({ .Home = true });
		CHECK(slider.Value == 0);
		ui.Step({ .End = true });
		CHECK(slider.Value == 100);
		ui.Step({ .Left = horizontal && reversed, .Right = horizontal && !reversed,
			.Up = !horizontal && !reversed, .Down = !horizontal && reversed });
		CHECK(slider.Value == 100);
		CHECK(ui.Focused(ui.First));
		ui.Step({ .Activate = true });
		CHECK_FALSE(ui.Registry.get<InteractableComponent>(ui.First).ActivatedThisFrame);
		CHECK(slider.Value == 100);
		ui.Step({ .Tab = true });
		CHECK(ui.Focused(ui.Second));
		ui.Step({ .Tab = true, .Shift = true });
		CHECK(ui.Focused(ui.First));
	}
}

TEST_CASE("UI keyboard slider edits ignore read-only dragging and editor-owned input") {
	NavigationFixture ui;
	auto& slider = ui.Registry.emplace<SliderComponent>(ui.First);
	slider.MinValue = -20;
	slider.MaxValue = 180;
	slider.Value = 50;
	ui.Step({ .Right = true });
	CHECK(slider.Value == doctest::Approx(52.0f));
	slider.WholeNumbers = true;
	ui.Step({ .Left = true });
	CHECK(slider.Value == 51);
	slider.IsReadOnly = true;
	ui.Step({ .Right = true, .End = true });
	CHECK(slider.Value == 51);
	CHECK(ui.Focused(ui.First));
	slider.IsReadOnly = false;
	slider.IsDragging = true;
	ui.Step({ .Left = true });
	CHECK(slider.Value == 51);
	slider.IsDragging = false;
	ui.Step({ .Enabled = false, .Tab = true, .Right = true, .End = true, .Activate = true, .Cancel = true });
	CHECK(slider.Value == 51);
	CHECK(ui.Focused(ui.First));
	slider.HandleEntity = ui.Second;
	ui.Step({ .Tab = true });
	ui.Step({ .Right = true });
	CHECK(slider.Value == 52);
	CHECK(ui.Focused(ui.Second));
	slider.MinValue = slider.MaxValue = 5;
	ui.Step({ .Right = true });
	CHECK(slider.Value == 5);
}

TEST_CASE("UI keyboard input fields keep caret arrows and release focus with Tab") {
	NavigationFixture ui;
	auto& field = ui.Registry.emplace<InputFieldComponent>(ui.First);
	ui.Step();
	REQUIRE(field.IsFocused);
	ui.Step({ .Right = true });
	CHECK(ui.Focused(ui.First));
	ui.Step({ .Tab = true });
	CHECK_FALSE(field.IsFocused);
	CHECK(ui.Focused(ui.Second));
}

TEST_CASE("UI keyboard circular sliders adjust values without navigating or starting a drag") {
	for (bool clockwise : { false, true }) {
		CAPTURE(clockwise);
		NavigationFixture ui;
		auto& slider = ui.Registry.emplace<CircularSliderComponent>(ui.First);
		slider.Clockwise = clockwise;
		slider.MinValue = -20;
		slider.MaxValue = 180;
		slider.Value = 50;
		ui.Step({ .Right = true });
		CHECK(slider.Value == doctest::Approx(52.0f));
		CHECK(slider.LastObservedValue == 50);
		CHECK(slider.ValueObserved);
		ui.Step({ .Up = true });
		CHECK(slider.Value == doctest::Approx(54.0f));
		ui.Step({ .Left = true });
		ui.Step({ .Down = true });
		CHECK(slider.Value == doctest::Approx(50.0f));
		CHECK(ui.Focused(ui.First));
		slider.WholeNumbers = true;
		ui.Step({ .Right = true, .Up = true });
		CHECK(slider.Value == 51);
		ui.Step({ .Home = true });
		ui.Step({ .Left = true });
		CHECK(slider.Value == -20);
		ui.Step({ .End = true });
		ui.Step({ .Up = true });
		CHECK(slider.Value == 180);
		ui.Step({ .Activate = true });
		CHECK_FALSE(ui.Registry.get<InteractableComponent>(ui.First).ActivatedThisFrame);
		CHECK_FALSE(slider.IsDragging);
		CHECK(slider.Value == 180);
		ui.Step({ .Tab = true, .Home = true });
		CHECK(ui.Focused(ui.Second));
		CHECK(slider.Value == 180);
		ui.Step({ .Tab = true, .Shift = true });
		CHECK(ui.Focused(ui.First));
	}
}

TEST_CASE("UI keyboard circular sliders respect handle focus read-only and input ownership") {
	NavigationFixture ui;
	auto& slider = ui.Registry.emplace<CircularSliderComponent>(ui.First);
	slider.HandleEntity = ui.Second;
	ui.Step({ .Tab = true });
	ui.Step({ .Right = true });
	CHECK(ui.Focused(ui.Second));
	CHECK(slider.Value == doctest::Approx(0.51f));
	ui.Step({ .Activate = true });
	CHECK_FALSE(ui.Registry.get<InteractableComponent>(ui.Second).ActivatedThisFrame);
	slider.IsReadOnly = true;
	ui.Step({ .Up = true, .End = true });
	CHECK(slider.Value == doctest::Approx(0.51f));
	slider.IsReadOnly = false;
	slider.IsDragging = true;
	ui.Step({ .Left = true, .Home = true });
	CHECK(slider.Value == doctest::Approx(0.51f));
	slider.IsDragging = false;
	ui.Step({ .Enabled = false, .Tab = true, .Right = true, .End = true, .Activate = true, .Cancel = true });
	CHECK(slider.Value == doctest::Approx(0.51f));
	CHECK(ui.Focused(ui.Second));
	slider.MinValue = slider.MaxValue = 5;
	ui.Step({ .Down = true });
	CHECK(slider.Value == 5);
	ui.Registry.emplace<DisabledTag>(ui.Root);
	ui.Step({ .Right = true });
	CHECK_FALSE(ui.Focused(ui.Second));
	CHECK(slider.Value == 5);
}

TEST_CASE("UI keyboard circular slider events and handle visuals work outside the game surface") {
	Application app;
	int offset = 0;
	SUBCASE("pointer inside the game surface") { offset = 0; }
	SUBCASE("pointer outside the game surface") { offset = 1000; }
	SurfaceGuard surface(offset);
	NavigationFixture ui;
	UIEventSystem events;
	auto& slider = ui.Registry.emplace<CircularSliderComponent>(ui.First);
	ui.Registry.get<RectTransform2DComponent>(ui.First).SizeDelta = { 200, 200 };
	slider.StartAngleDegrees = 0;
	slider.SweepDegrees = 180;
	const auto handle = MakeRect(ui.Registry, ui.First, {}, { 20, 20 });
	auto& handleInteract = ui.Registry.emplace<InteractableComponent>(handle);
	auto& image = ui.Registry.emplace<ImageComponent>(handle);
	slider.HandleEntity = handle;
	ui.Step({ .Right = true });
	events.Update(*ui.ScenePtr);
	CHECK(slider.ValueChangedThisFrame);
	CHECK(slider.Value == doctest::Approx(0.51f));
	const auto position = ui.Registry.get<RectTransform2DComponent>(handle).AnchoredPosition;
	CHECK(position.x < -2.0f);
	CHECK(position.y < -89.0f);
	CHECK(image.Color == slider.HoveredColor);
	CHECK_FALSE(handleInteract.IsFocused);
	events.Update(*ui.ScenePtr);
	CHECK_FALSE(slider.ValueChangedThisFrame);
	slider.FocusedColor = { 1, 0, 0, 1 };
	events.Update(*ui.ScenePtr);
	CHECK(image.Color == slider.FocusedColor);
	slider.TransitionMode = UITransitionMode::SpriteSwap;
	slider.HoveredSprite = UUID{ 90001 };
	slider.FocusedSprite = UUID{ 90002 };
	events.Update(*ui.ScenePtr);
	CHECK(image.TextureAssetId == slider.FocusedSprite);
	slider.FocusedSprite = UUID{ 0 };
	events.Update(*ui.ScenePtr);
	CHECK(image.TextureAssetId == slider.HoveredSprite);
	slider.TransitionMode = UITransitionMode::ColorSwap;
	ui.Step({ .End = true });
	events.Update(*ui.ScenePtr);
	CHECK(slider.ValueChangedThisFrame);
	ui.Step({ .Right = true });
	events.Update(*ui.ScenePtr);
	CHECK_FALSE(slider.ValueChangedThisFrame);
	ui.Registry.get<InteractableComponent>(ui.First).Interactable = false;
	events.Update(*ui.ScenePtr);
	CHECK(image.Color == slider.DisabledColor);
}

TEST_CASE("UI keyboard widget changes emit one event through the existing UI event pass") {
	Application app;
	int offset = 0;
	SUBCASE("pointer inside the game surface") { offset = 0; }
	SUBCASE("pointer outside the game surface") { offset = 1000; }
	SurfaceGuard surface(offset);
	NavigationFixture ui;
	UIEventSystem events;
	auto& slider = ui.Registry.emplace<SliderComponent>(ui.First);
	const auto handle = MakeRect(ui.Registry, ui.First, {}, { 10, 40 });
	const auto label = MakeRect(ui.Registry, ui.First, {}, { 100, 30 });
	ui.Registry.emplace<TextRendererComponent>(label);
	slider.HandleEntity = handle;
	slider.LabelEntity = label;
	auto& dd = ui.Registry.emplace<DropdownComponent>(ui.Second);
	dd.Options = { "First", "Second", "Third" };
	ui.Step({ .Right = true });
	events.Update(*ui.ScenePtr);
	CHECK(slider.ValueChangedThisFrame);
	CHECK(slider.Value == doctest::Approx(0.51f));
	CHECK(ui.Registry.get<RectTransform2DComponent>(handle).AnchoredPosition.x == doctest::Approx(1.0f));
	CHECK(ui.Registry.get<TextRendererComponent>(label).Text == "51%");
	events.Update(*ui.ScenePtr);
	CHECK_FALSE(slider.ValueChangedThisFrame);
	ui.Step({ .Tab = true });
	ui.Step({ .Activate = true });
	ui.Step({ .Down = true });
	events.Update(*ui.ScenePtr);
	CHECK_FALSE(dd.SelectionChangedThisFrame);
	CHECK(dd.SelectedIndex == 0);
	ui.Step({ .Activate = true });
	events.Update(*ui.ScenePtr);
	CHECK(dd.SelectionChangedThisFrame);
	CHECK(dd.SelectedIndex == 1);
	events.Update(*ui.ScenePtr);
	CHECK_FALSE(dd.SelectionChangedThisFrame);
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

TEST_CASE("UI focus uses pointer stacking and hierarchy navigation rather than component insertion order") {
	auto scene = Scene::CreateDetachedScene("Focus stacking");
	auto& registry = scene->GetRegistry();
	const auto root = MakeRect(registry, entt::null, { 0, 0 }, { 300, 300 });
	const auto back = MakeRect(registry, root, { 0, 0 }, { 100, 100 });
	const auto front = MakeRect(registry, root, { 0, 0 }, { 100, 100 });
	registry.emplace<InteractableComponent>(front).Focusable = true;
	registry.emplace<InteractableComponent>(back).Focusable = true;
	registry.emplace<ImageComponent>(back).SortingOrder = 20;
	registry.emplace<ImageComponent>(front).SortingOrder = 10;
	CHECK(UIEventSystem::PickInteractable(*scene, { 0, 0 }) == back);
	CHECK(UIFocusSystem::PickFocusable(*scene, { 0, 0 }) == back);
	CHECK(UIFocusSystem::NavigationOrder(*scene) == std::vector<EntityHandle>{ back, front });
	registry.get<InteractableComponent>(back).Focusable = false;
	CHECK(UIFocusSystem::PickFocusable(*scene, { 0, 0 }) == entt::null);
	registry.get<ImageComponent>(front).SortingOrder = 30;
	CHECK(UIFocusSystem::PickFocusable(*scene, { 0, 0 }) == front);
	registry.emplace<DisabledTag>(root);
	CHECK(UIFocusSystem::NavigationOrder(*scene).empty());
	CHECK(UIFocusSystem::PickFocusable(*scene, { 0, 0 }) == entt::null);
}

TEST_CASE("UI touch release rechecks the real layout after a captured control moves") {
	auto scene = Scene::CreateDetachedScene("Moving captured UI");
	auto& registry = scene->GetRegistry();
	const auto entity = MakeRect(registry, entt::null, { 0, 0 }, { 50, 50 });
	registry.emplace<InteractableComponent>(entity);
	std::vector<UIEventSystem::FingerPress> presses;
	std::vector<EntityHandle> clicked;
	std::vector<UIEventSystem::FingerSample> fingers{ { 7, TouchPhase::Began, { 0, 0 }, true } };
	auto pick = [&](Vec2 point) { return UIEventSystem::PickInteractable(*scene, point); };
	UIEventSystem::UpdateFingerPresses(registry, fingers, UIEventSystem::k_NoFinger, entt::null, pick, presses, clicked);
	REQUIRE(presses.size() == 1);
	registry.get<RectTransform2DComponent>(entity).AnchoredPosition.x = 200;
	ComputeUILayout(*scene);
	fingers[0].Phase = TouchPhase::Ended;
	UIEventSystem::UpdateFingerPresses(registry, fingers, UIEventSystem::k_NoFinger, entt::null, pick, presses, clicked);
	CHECK(presses.empty());
	CHECK(clicked.empty());
	CHECK_FALSE(registry.get<InteractableComponent>(entity).IsPressed);
	CHECK(registry.get<InteractableComponent>(entity).IsMouseUp);
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
	CHECK(clip.Min.y == 0.0f);
	const auto offsetClip = QuadVisibility::FromScissor(1000000250, 1000000100, 100, 100,
		1000000000, 1000000000, 600, 400);
	CHECK(offsetClip.Min.x == clip.Min.x);
	CHECK(offsetClip.Min.y == clip.Min.y);
	CHECK(offsetClip.Max.x == clip.Max.x);
	CHECK(offsetClip.Max.y == clip.Max.y);
	const std::array<Vec2, 4> hiddenRow{ Vec2{ -0.1f, -0.4f }, Vec2{ 0.1f, -0.4f },
		Vec2{ 0.1f, -0.1f }, Vec2{ -0.1f, -0.1f } };
	CHECK_FALSE(QuadVisibility::IsVisible(hiddenRow, glm::mat4{ 1.0f }, clip));
	const auto scrolled = glm::translate(glm::mat4{ 1.0f }, glm::vec3{ 0.0f, 0.1f, 0.0f });
	CHECK(QuadVisibility::IsVisible(hiddenRow, scrolled, clip));
	const auto zeroViewport = QuadVisibility::FromScissor(1, 1, 1, 1, 0, 0, 0, 0);
	CHECK(zeroViewport.Min.x == -1.0f);
	CHECK(zeroViewport.Max.x == 1.0f);
}
