#include <doctest/doctest.h>

#include "Components/UI/ButtonComponent.hpp"
#include "Components/UI/InteractableComponent.hpp"
#include "Components/UI/SliderComponent.hpp"
#include "Systems/UIEventSystem.hpp"

#include <entt/entity/registry.hpp>

#include <vector>

using namespace Funke;

namespace {
	using Finger = UIEventSystem::FingerSample;

	// Left half of the screen is button A, right half button B.
	struct TwoButtons {
		entt::registry Registry;
		EntityHandle A = Registry.create();
		EntityHandle B = Registry.create();
		std::vector<UIEventSystem::FingerPress> Presses;
		std::vector<EntityHandle> Clicked;

		TwoButtons() {
			for (EntityHandle e : { A, B }) {
				Registry.emplace<InteractableComponent>(e);
				Registry.emplace<ButtonComponent>(e);
			}
		}

		EntityHandle Pick(Vec2 ui) const { return ui.x < 0.0f ? A : B; }

		// Mirrors a frame of UIEventSystem: the mouse pass clears edges and holds only its own press.
		void Frame(std::vector<Finger> fingers, int64_t mouseFinger, EntityHandle mousePressed = entt::null) {
			for (EntityHandle e : { A, B }) {
				auto& interact = Registry.get<InteractableComponent>(e);
				interact.IsMouseDown = interact.IsMouseUp = interact.IsClicked = false;
				interact.IsPressed = e == mousePressed;
			}
			Clicked.clear();
			UIEventSystem::UpdateFingerPresses(Registry, fingers, mouseFinger, mousePressed,
				[this](Vec2 ui) { return Pick(ui); }, Presses, Clicked);
		}

		const InteractableComponent& Get(EntityHandle e) const { return Registry.get<InteractableComponent>(e); }
	};
}

TEST_CASE("The mouse finger is the first one down and stays until it lifts") {
	using UI = UIEventSystem;
	std::vector<Finger> fingers{ { 7, TouchPhase::Began, {}, true }, { 9, TouchPhase::Began, {}, true } };
	int64_t mouse = UI::TrackMouseFinger(UI::k_NoFinger, fingers);
	CHECK(mouse == 7);
	fingers = { { 7, TouchPhase::Ended, {}, true }, { 9, TouchPhase::Stationary, {}, true } };
	CHECK(UI::TrackMouseFinger(mouse, fingers) == 7);
	// Lifted: the finger still held does not take over, only a new one does.
	fingers = { { 9, TouchPhase::Stationary, {}, true } };
	mouse = UI::TrackMouseFinger(mouse, fingers);
	CHECK(mouse == UI::k_NoFinger);
	fingers.push_back({ 11, TouchPhase::Began, {}, true });
	CHECK(UI::TrackMouseFinger(mouse, fingers) == 11);
}

TEST_CASE("A second finger holds and clicks its own button while the first holds another") {
	TwoButtons ui;
	// Finger 1 (the mouse) holds A; finger 2 presses B.
	ui.Frame({ { 1, TouchPhase::Stationary, { -10.0f, 0.0f }, true }, { 2, TouchPhase::Began, { 10.0f, 0.0f }, true } }, 1, ui.A);
	CHECK(ui.Get(ui.B).IsMouseDown);
	CHECK(ui.Get(ui.B).IsPressed);
	CHECK_FALSE(ui.Get(ui.A).IsMouseDown);

	ui.Frame({ { 1, TouchPhase::Stationary, { -10.0f, 0.0f }, true }, { 2, TouchPhase::Moved, { 12.0f, 0.0f }, true } }, 1, ui.A);
	CHECK(ui.Get(ui.B).IsPressed);
	CHECK(ui.Get(ui.A).IsPressed);

	ui.Frame({ { 1, TouchPhase::Stationary, { -10.0f, 0.0f }, true }, { 2, TouchPhase::Ended, { 12.0f, 0.0f }, true } }, 1, ui.A);
	CHECK(ui.Get(ui.B).IsMouseUp);
	CHECK(ui.Get(ui.B).IsClicked);
	CHECK_FALSE(ui.Get(ui.B).IsPressed);
	REQUIRE(ui.Clicked.size() == 1);
	CHECK(ui.Clicked[0] == ui.B);
	CHECK(ui.Presses.empty());
}

TEST_CASE("A finger that slides off its button or vanishes releases without a click") {
	TwoButtons ui;
	ui.Frame({ { 2, TouchPhase::Began, { 10.0f, 0.0f }, true } }, UIEventSystem::k_NoFinger);
	ui.Frame({ { 2, TouchPhase::Ended, { -10.0f, 0.0f }, true } }, UIEventSystem::k_NoFinger);
	CHECK(ui.Get(ui.B).IsMouseUp);
	CHECK_FALSE(ui.Get(ui.B).IsClicked);
	CHECK(ui.Clicked.empty());

	ui.Frame({ { 3, TouchPhase::Began, { 10.0f, 0.0f }, true } }, UIEventSystem::k_NoFinger);
	ui.Frame({}, UIEventSystem::k_NoFinger);
	CHECK(ui.Get(ui.B).IsMouseUp);
	CHECK_FALSE(ui.Get(ui.B).IsClicked);
	CHECK(ui.Presses.empty());
}

TEST_CASE("Extra fingers leave the mouse finger, drag widgets and the mouse's own press alone") {
	TwoButtons ui;
	// The mouse finger is handled by the mouse pass.
	ui.Frame({ { 1, TouchPhase::Began, { 10.0f, 0.0f }, true } }, 1);
	CHECK(ui.Presses.empty());
	// A button the mouse already presses is not pressed twice.
	ui.Frame({ { 2, TouchPhase::Began, { 10.0f, 0.0f }, true } }, 1, ui.B);
	CHECK(ui.Presses.empty());
	// Sliders follow the mouse finger only.
	ui.Registry.emplace<SliderComponent>(ui.A);
	ui.Frame({ { 3, TouchPhase::Began, { -10.0f, 0.0f }, true } }, 1);
	CHECK(ui.Presses.empty());
	CHECK_FALSE(ui.Get(ui.A).IsPressed);
	// Off the game surface (e.g. over an editor panel).
	ui.Frame({ { 4, TouchPhase::Began, { 10.0f, 0.0f }, false } }, 1);
	CHECK(ui.Presses.empty());
}
