#include <doctest/doctest.h>

#include "Components/General/RectTransform2DComponent.hpp"
#include "Core/Window.hpp"
#include "Scene/Entity.hpp"
#include "Scene/EntityHelper.hpp"
#include "Scene/Scene.hpp"
#include "Systems/UILayoutSystem.hpp"

using namespace Index;

namespace {
	struct WideSurfaceScope {
		Window::UIRegion Previous = Window::GetUIRegion();
		// A 20:9 phone-shaped UI surface; the Editor source also keeps safe-area insets at zero.
		WideSurfaceScope() { Window::SetUIRegion(0, 0, 2400, 1080); }
		~WideSurfaceScope() {
			if (Previous.Source == Window::UIRegionSource::Editor) {
				Window::SetUIRegion(Previous.OffsetX, Previous.OffsetY, Previous.Width, Previous.Height,
					Previous.WindowToRegionScale);
			}
			else if (Previous.Source == Window::UIRegionSource::AspectLock) {
				Window::PublishAspectLockUIRegion(1.0f, Previous);
			}
			else {
				Window::ClearUIRegion();
			}
		}
	};
}

TEST_CASE("Fill Parent lets a canvas follow a wider screen") {
	WideSurfaceScope surface;
	auto scene = Scene::CreateDetachedScene("Fill Parent Layout");

	Entity canvas = EntityHelper::CreateUIPanel(*scene);
	Entity corner = EntityHelper::CreateUIPanel(*scene);
	Entity bar = EntityHelper::CreateUIPanel(*scene);
	corner.SetParent(canvas);
	bar.SetParent(canvas);

	// References only after every entity exists: creating more can move the component pool.
	auto& canvasRect = canvas.GetComponent<RectTransform2DComponent>();
	canvasRect.SizeDelta = Vec2{ 1920.0f, 1080.0f };

	auto& cornerRect = corner.GetComponent<RectTransform2DComponent>();
	cornerRect.AnchorMin = Vec2{ 1.0f, 1.0f };
	cornerRect.AnchorMax = Vec2{ 1.0f, 1.0f };
	cornerRect.SizeDelta = Vec2{ 100.0f, 100.0f };
	cornerRect.AnchoredPosition = Vec2{ -50.0f, -50.0f };

	auto& barRect = bar.GetComponent<RectTransform2DComponent>();
	barRect.AnchorMin = Vec2{ 0.5f, 0.0f };
	barRect.AnchorMax = Vec2{ 0.5f, 0.0f };
	barRect.Pivot = Vec2{ 0.5f, 0.0f };
	barRect.SizeDelta = Vec2{ 1920.0f, 270.0f };

	SUBCASE("a fixed-size canvas stays a centred 16:9 box") {
		ComputeUILayout(*scene);
		CHECK(canvasRect.ResolvedMax.x < 1200.0f);
		CHECK(cornerRect.ResolvedMax.x < 1200.0f);
	}

	SUBCASE("filling both axes covers the surface and puts corner children in its corners") {
		canvasRect.FillWidth = true;
		canvasRect.FillHeight = true;
		ComputeUILayout(*scene);
		CHECK(canvasRect.ResolvedMin.x == doctest::Approx(-1200.0f));
		CHECK(canvasRect.ResolvedMax.x == doctest::Approx(1200.0f));
		CHECK(canvasRect.ResolvedMin.y == doctest::Approx(-540.0f));
		CHECK(canvasRect.ResolvedMax.y == doctest::Approx(540.0f));
		CHECK(cornerRect.ResolvedMax.x == doctest::Approx(1200.0f));
		CHECK(cornerRect.ResolvedMax.y == doctest::Approx(540.0f));
	}

	SUBCASE("filling the width only spans the width and keeps the authored height at its edge anchor") {
		canvasRect.FillWidth = true;
		canvasRect.FillHeight = true;
		barRect.FillWidth = true;
		ComputeUILayout(*scene);
		CHECK(barRect.ResolvedMin.x == doctest::Approx(-1200.0f));
		CHECK(barRect.ResolvedMax.x == doctest::Approx(1200.0f));
		CHECK(barRect.ResolvedMin.y == doctest::Approx(-540.0f));
		CHECK(barRect.ResolvedMax.y - barRect.ResolvedMin.y == doctest::Approx(270.0f * barRect.Scale.y));
	}
}
