#include <doctest/doctest.h>

#include "Core/Input.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptGlue.hpp"

using namespace Funke;

namespace {
	void CheckVec(Vec3 actual, float x, float y, float z) {
		CHECK(actual.x == doctest::Approx(x));
		CHECK(actual.y == doctest::Approx(y));
		CHECK(actual.z == doctest::Approx(z));
	}
}

TEST_CASE("Motion sensor axes follow the rotated screen") {
	// Phone held upright, right edge tilted down: gravity leans toward device +X.
	const Vec3 device{ 0.5f, -0.8f, 0.1f };

	CheckVec(Input::DeviceToScreenAxes(device, 0), 0.5f, -0.8f, 0.1f);
	// Landscape, right side up: the device's +X edge is now the top of the screen.
	CheckVec(Input::DeviceToScreenAxes(device, 1), 0.8f, 0.5f, 0.1f);
	CheckVec(Input::DeviceToScreenAxes(device, 2), -0.5f, 0.8f, 0.1f);
	CheckVec(Input::DeviceToScreenAxes(device, 3), -0.8f, -0.5f, 0.1f);
	// Natural-landscape tablet in portrait: current (0) minus natural (1).
	CheckVec(Input::DeviceToScreenAxes(device, -1), Input::DeviceToScreenAxes(device, 3).x,
		Input::DeviceToScreenAxes(device, 3).y, 0.1f);
}

TEST_CASE("Motion sensor bindings read zero without a running application") {
	NativeBindings bindings{};
	ScriptBindings::PopulateNativeBindings(bindings);
	REQUIRE(bindings.Input_GetAcceleration != nullptr);
	REQUIRE(bindings.Input_GetGyroscope != nullptr);
	REQUIRE(bindings.Input_IsMotionSensorAvailable != nullptr);

	float x = 1.0f, y = 1.0f, z = 1.0f;
	bindings.Input_GetAcceleration(&x, &y, &z);
	CheckVec({ x, y, z }, 0.0f, 0.0f, 0.0f);

	x = y = z = 1.0f;
	bindings.Input_GetGyroscope(&x, &y, &z);
	CheckVec({ x, y, z }, 0.0f, 0.0f, 0.0f);

	CHECK(bindings.Input_IsMotionSensorAvailable(0) == 0);
	CHECK(bindings.Input_IsMotionSensorAvailable(1) == 0);
	CHECK(bindings.Input_IsMotionSensorAvailable(7) == 0);
}
