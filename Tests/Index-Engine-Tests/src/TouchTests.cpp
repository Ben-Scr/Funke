#include <doctest/doctest.h>

#include "Core/Input.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptGlue.hpp"

using namespace Index;

TEST_CASE("Touch tracker reports each finger's phase per frame") {
	TouchTracker tracker;
	tracker.BeginFrame();
	tracker.OnDown(1, { 10.f, 20.f });
	REQUIRE(tracker.GetCount() == 1);
	CHECK(tracker.Get(0)->Phase == TouchPhase::Began);

	// A second finger while the first one holds: both are tracked (the mouse only follows the first).
	tracker.BeginFrame();
	tracker.OnDown(2, { 300.f, 400.f });
	REQUIRE(tracker.GetCount() == 2);
	CHECK(tracker.Get(0)->Phase == TouchPhase::Stationary);
	CHECK(tracker.Get(1)->Phase == TouchPhase::Began);

	tracker.BeginFrame();
	tracker.OnMove(1, { 15.f, 25.f });
	CHECK(tracker.Get(0)->Phase == TouchPhase::Moved);
	CHECK(tracker.Get(0)->Position.x == doctest::Approx(15.f));
	CHECK(tracker.Get(0)->StartPosition.x == doctest::Approx(10.f));
	CHECK(tracker.Get(1)->Phase == TouchPhase::Stationary);

	tracker.BeginFrame();
	tracker.OnUp(1, { 16.f, 26.f });
	CHECK(tracker.Get(0)->Phase == TouchPhase::Ended);

	tracker.BeginFrame();
	REQUIRE(tracker.GetCount() == 1);
	CHECK(tracker.Get(0)->Id == 2);
	CHECK(tracker.Get(1) == nullptr);
}

TEST_CASE("Touch tracker keeps a tap that begins and ends in one frame") {
	TouchTracker tracker;
	tracker.BeginFrame();
	tracker.OnDown(7, { 1.f, 2.f });
	tracker.OnUp(7, { 1.f, 2.f });
	REQUIRE(tracker.GetCount() == 1);
	CHECK(tracker.Get(0)->Phase == TouchPhase::Began);

	tracker.BeginFrame();
	REQUIRE(tracker.GetCount() == 1);
	CHECK(tracker.Get(0)->Phase == TouchPhase::Ended);

	tracker.BeginFrame();
	CHECK(tracker.GetCount() == 0);
}

TEST_CASE("Touch bindings report no fingers without a running application") {
	NativeBindings bindings{};
	ScriptBindings::PopulateNativeBindings(bindings);
	REQUIRE(bindings.Input_GetTouchCount != nullptr);
	REQUIRE(bindings.Input_GetTouch != nullptr);

	CHECK(bindings.Input_GetTouchCount() == 0);
	int64_t id = 5;
	int phase = 5;
	float values[4] = { 1.f, 1.f, 1.f, 1.f };
	CHECK(bindings.Input_GetTouch(0, &id, &phase, values) == 0);
}
