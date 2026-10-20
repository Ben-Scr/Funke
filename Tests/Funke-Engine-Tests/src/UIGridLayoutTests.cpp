#include <doctest/doctest.h>

#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/UI/GridLayoutGroupComponent.hpp"
#include "Core/Window.hpp"
#include "Scene/Entity.hpp"
#include "Scene/EntityHelper.hpp"
#include "Scene/Scene.hpp"
#include "Systems/UILayoutSystem.hpp"

#include <vector>

using namespace Funke;

namespace {
	Vec2 Center(const RectTransform2DComponent& rect) {
		return (rect.ResolvedMin + rect.ResolvedMax) * 0.5f;
	}

	// Layout needs a UI surface; tests have no window.
	struct SurfaceScope {
		Window::UIRegion Previous = Window::GetUIRegion();
		SurfaceScope() { Window::SetUIRegion(0, 0, 1920, 1080); }
		~SurfaceScope() {
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

TEST_CASE("Grid Layout Group aligns the occupied cells like Unity") {
	SurfaceScope surface;
	auto scene = Scene::CreateDetachedScene("Grid Layout");

	Entity grid = EntityHelper::CreateUIGridLayout(*scene);
	std::vector<Entity> items;
	for (int i = 0; i < 4; ++i) {
		items.push_back(EntityHelper::CreateUIPanel(*scene));
		items.back().SetParent(grid);
	}

	// References only after every entity exists: creating more can move the component pool.
	auto& gridRect = grid.GetComponent<RectTransform2DComponent>();
	auto& layout = grid.GetComponent<GridLayoutGroupComponent>();
	gridRect.SizeDelta = Vec2{ 100.0f, 100.0f };
	layout.CellSize = Vec2{ 50.0f, 50.0f };
	layout.Spacing = Vec2{ 0.0f, 0.0f };
	layout.PaddingLeft = layout.PaddingRight = layout.PaddingTop = layout.PaddingBottom = 0.0f;
	layout.ChildAlignment = UIAlignment::MiddleCenter;

	SUBCASE("a single child in a two-column flexible grid sits in the middle") {
		for (size_t i = 1; i < items.size(); ++i) items[i].SetEnabled(false);
		ComputeUILayout(*scene);
		REQUIRE(gridRect.ResolvedMax.x - gridRect.ResolvedMin.x == doctest::Approx(100.0f));
		const Vec2 gridCenter = Center(gridRect);
		const auto& item = items[0].GetComponent<RectTransform2DComponent>();
		CHECK(Center(item).x == doctest::Approx(gridCenter.x));
		CHECK(Center(item).y == doctest::Approx(gridCenter.y));
		CHECK(item.ResolvedMax.x - item.ResolvedMin.x == doctest::Approx(50.0f));
	}

	SUBCASE("a full flexible grid still fills its columns from the start corner") {
		ComputeUILayout(*scene);
		const auto& first = items[0].GetComponent<RectTransform2DComponent>();
		const auto& second = items[1].GetComponent<RectTransform2DComponent>();
		const auto& third = items[2].GetComponent<RectTransform2DComponent>();
		CHECK(first.ResolvedMin.x == doctest::Approx(gridRect.ResolvedMin.x));
		CHECK(first.ResolvedMax.y == doctest::Approx(gridRect.ResolvedMax.y));
		CHECK(second.ResolvedMin.x == doctest::Approx(first.ResolvedMax.x));
		CHECK(third.ResolvedMax.y == doctest::Approx(first.ResolvedMin.y));
	}

	SUBCASE("fewer children than a fixed column count are centred as a group") {
		gridRect.SizeDelta = Vec2{ 400.0f, 50.0f };
		layout.Constraint = GridLayoutConstraint::FixedColumnCount;
		layout.ConstraintCount = 8;
		items[3].SetEnabled(false);
		ComputeUILayout(*scene);
		const auto& first = items[0].GetComponent<RectTransform2DComponent>();
		const auto& last = items[2].GetComponent<RectTransform2DComponent>();
		CHECK((first.ResolvedMin.x + last.ResolvedMax.x) * 0.5f == doctest::Approx(Center(gridRect).x));
	}
}
