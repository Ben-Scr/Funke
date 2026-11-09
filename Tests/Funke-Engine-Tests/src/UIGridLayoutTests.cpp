#include <doctest/doctest.h>

#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/UI/GridLayoutGroupComponent.hpp"
#include "Components/UI/HorizontalLayoutGroupComponent.hpp"
#include "Components/UI/VerticalLayoutGroupComponent.hpp"
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

TEST_CASE("Backlog child scale stays stable across layout invalidations") {
	SurfaceScope surface;
	for (bool vertical : {false, true}) for (float scale : {0.5f, 2.0f}) {
		auto scene = Scene::CreateDetachedScene("Scaled layout");
		auto parent = vertical ? EntityHelper::CreateUIVerticalLayout(*scene) : EntityHelper::CreateUIHorizontalLayout(*scene);
		auto child = EntityHelper::CreateUIPanel(*scene);
		child.SetParent(parent);
		parent.GetComponent<RectTransform2DComponent>().SizeDelta = {500,500};
		auto& rect = child.GetComponent<RectTransform2DComponent>();
		rect.SizeDelta = {100,100};
		rect.LocalScale = {scale,scale};
		if (vertical) {
			auto& layout = parent.GetComponent<VerticalLayoutGroupComponent>();
			layout.ControlChildHeight = true;
			layout.ChildForceExpandHeight = false;
			layout.UseChildScaleHeight = true;
		} else {
			auto& layout = parent.GetComponent<HorizontalLayoutGroupComponent>();
			layout.ControlChildWidth = true;
			layout.ChildForceExpandWidth = false;
			layout.UseChildScaleWidth = true;
		}
		for (int i = 0; i < 4; ++i) {
			Window::SetUIRegion(0,0,1920+i*10,1080+i*10);
			ComputeUILayout(*scene);
			CHECK((vertical ? rect.SizeDelta.y : rect.SizeDelta.x) == doctest::Approx(100));
			const Vec2 size = rect.ResolvedMax - rect.ResolvedMin;
			CHECK((vertical ? size.y : size.x) == doctest::Approx(100*scale));
		}
	}
}

TEST_CASE("Backlog layout combinations preserve spacing alignment and authored dimensions") {
	SurfaceScope surface;
	for (bool vertical : {false, true}) for (float scale : {0.5f, 2.0f})
	for (bool control : {false, true}) for (bool expand : {false, true}) for (bool useScale : {false, true}) {
		auto scene = Scene::CreateDetachedScene("Layout matrix");
		auto parent = vertical ? EntityHelper::CreateUIVerticalLayout(*scene) : EntityHelper::CreateUIHorizontalLayout(*scene);
		auto first = EntityHelper::CreateUIPanel(*scene);
		auto second = EntityHelper::CreateUIPanel(*scene);
		first.SetParent(parent);
		second.SetParent(parent);
		parent.GetComponent<RectTransform2DComponent>().SizeDelta = {500, 500};
		for (auto child : {first, second}) {
			child.GetComponent<RectTransform2DComponent>().SizeDelta = {100, 100};
			child.GetComponent<RectTransform2DComponent>().LocalScale = {scale, scale};
		}
		const auto setup = [&](auto& layout) {
			layout.ControlChildWidth = layout.ControlChildHeight = control;
			layout.ChildForceExpandWidth = layout.ChildForceExpandHeight = expand;
			layout.UseChildScaleWidth = layout.UseChildScaleHeight = useScale;
			layout.Spacing = 7;
			layout.PaddingLeft = layout.PaddingRight = layout.PaddingTop = layout.PaddingBottom = 0;
			layout.ChildAlignment = UIAlignment::MiddleCenter;
		};
		if (vertical) setup(parent.GetComponent<VerticalLayoutGroupComponent>());
		else setup(parent.GetComponent<HorizontalLayoutGroupComponent>());
		for (int iteration = 0; iteration < 4; ++iteration) {
			Window::SetUIRegion(0, 0, 800 + iteration * 20, 600 + iteration * 10);
			ComputeUILayout(*scene);
			const auto& a = first.GetComponent<RectTransform2DComponent>();
			const auto& b = second.GetComponent<RectTransform2DComponent>();
			const int axis = vertical ? 1 : 0;
			const float expected = control && expand ? 246.5f / (useScale ? scale : 1) : 100;
			CHECK(a.SizeDelta[axis] == doctest::Approx(expected));
			CHECK(b.SizeDelta[axis] == doctest::Approx(expected));
			if (useScale) {
				CHECK((vertical ? a.ResolvedMin.y - b.ResolvedMax.y : b.ResolvedMin.x - a.ResolvedMax.x) == doctest::Approx(7));
				CHECK((Center(a)[axis] + Center(b)[axis]) * 0.5f == doctest::Approx(Center(parent.GetComponent<RectTransform2DComponent>())[axis]));
			}
		}
	}
}
