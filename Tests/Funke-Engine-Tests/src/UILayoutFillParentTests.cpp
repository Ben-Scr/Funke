#include <doctest/doctest.h>

#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Core/Window.hpp"
#include "Core/EngineModules.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/UI/InteractableComponent.hpp"
#include "Components/UI/HorizontalLayoutGroupComponent.hpp"
#include "Components/UI/ContentSizeFitterComponent.hpp"
#include "Components/UI/WidthConstraintComponent.hpp"
#include "Components/Tags.hpp"
#include "Scene/Entity.hpp"
#include "Scene/EntityHelper.hpp"
#include "Scene/Scene.hpp"
#include "Project/ProjectManager.hpp"
#include "Systems/UILayoutSystem.hpp"
#include "Systems/UIEventSystem.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

using namespace Funke;

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

TEST_CASE("UpdateUILayout reuses unchanged layouts and observes direct field edits") {
	WideSurfaceScope surface;
	auto scene = Scene::CreateDetachedScene("Layout Gate");
	Entity panel = EntityHelper::CreateUIPanel(*scene);
	auto& rect = panel.GetComponent<RectTransform2DComponent>();
	rect.SizeDelta = Vec2{ 100.0f, 100.0f };

	UpdateUILayout(*scene);
	const float width = rect.ResolvedMax.x - rect.ResolvedMin.x;
	const uint64_t revision = GetUILayoutRevision(*scene);
	UpdateUILayout(*scene);
	ComputeUILayout(*scene);
	CHECK(GetUILayoutRevision(*scene) == revision);
	rect.SizeDelta = Vec2{ 300.0f, 100.0f };

	UpdateUILayout(*scene);
	CHECK(rect.ResolvedMax.x - rect.ResolvedMin.x == doctest::Approx(width * 3.0f));
	CHECK(GetUILayoutRevision(*scene) == revision + 1);

	InvalidateUILayout();
	UpdateUILayout(*scene);
	CHECK(rect.ResolvedMax.x - rect.ResolvedMin.x == doctest::Approx(width * 3.0f));
	CHECK(GetUILayoutRevision(*scene) == revision + 2);
}

TEST_CASE("UILayout cache observes hierarchy order disabled children and layout settings") {
	WideSurfaceScope surface;
	auto scene = Scene::CreateDetachedScene("Layout Cache Inputs");
	Entity row = EntityHelper::CreateUIPanel(*scene);
	Entity first = EntityHelper::CreateUIPanel(*scene);
	Entity second = EntityHelper::CreateUIPanel(*scene);
	first.SetParent(row);
	second.SetParent(row);
	row.GetComponent<RectTransform2DComponent>().SizeDelta = { 600.0f, 200.0f };
	row.AddComponent<HorizontalLayoutGroupComponent>();
	ComputeUILayout(*scene);
	CHECK(first.GetComponent<RectTransform2DComponent>().ResolvedMin.x < second.GetComponent<RectTransform2DComponent>().ResolvedMin.x);
	const uint64_t revision = GetUILayoutRevision(*scene);
	ComputeUILayout(*scene);
	CHECK(GetUILayoutRevision(*scene) == revision);

	auto& children = row.GetComponent<HierarchyComponent>().Children;
	std::reverse(children.begin(), children.end());
	UpdateUILayout(*scene);
	CHECK(second.GetComponent<RectTransform2DComponent>().ResolvedMin.x < first.GetComponent<RectTransform2DComponent>().ResolvedMin.x);
	const float secondX = second.GetComponent<RectTransform2DComponent>().ResolvedMin.x;
	second.AddComponent<DisabledTag>();
	UpdateUILayout(*scene);
	CHECK(first.GetComponent<RectTransform2DComponent>().ResolvedMin.x == doctest::Approx(secondX));

	row.GetComponent<HorizontalLayoutGroupComponent>().PaddingLeft = 30.0f;
	UpdateUILayout(*scene);
	CHECK(first.GetComponent<RectTransform2DComponent>().ResolvedMin.x > secondX);
	first.AddComponent<WidthConstraintComponent>().MaxWidth = 25.0f;
	UpdateUILayout(*scene);
	CHECK(first.GetComponent<RectTransform2DComponent>().SizeDelta.x == doctest::Approx(25.0f));
}

TEST_CASE("UILayout cache refreshes after direct project default font changes") {
	WideSurfaceScope surface;
	struct ProjectScope {
		std::unique_ptr<FunkeProject> Previous;
		ProjectScope() {
			if (const auto* current = ProjectManager::GetCurrentProject()) Previous = std::make_unique<FunkeProject>(*current);
			ProjectManager::SetCurrentProject(std::make_unique<FunkeProject>());
		}
		~ProjectScope() { ProjectManager::SetCurrentProject(std::move(Previous)); }
	} project;
	auto scene = Scene::CreateDetachedScene("Layout Font Change");
	EntityHelper::CreateUIPanel(*scene);
	UpdateUILayout(*scene);
	const uint64_t revision = GetUILayoutRevision(*scene);
	UpdateUILayout(*scene);
	CHECK(GetUILayoutRevision(*scene) == revision);
	ProjectManager::GetCurrentProject()->DefaultFontAssetId = 123456789;
	UpdateUILayout(*scene);
	CHECK(GetUILayoutRevision(*scene) == revision + 1);
}

TEST_CASE("UILayout cache waits for coupled fitters and groups to settle") {
	WideSurfaceScope surface;
	auto scene = Scene::CreateDetachedScene("Layout Cache Convergence");
	Entity row = EntityHelper::CreateUIPanel(*scene);
	Entity first = EntityHelper::CreateUIPanel(*scene);
	Entity second = EntityHelper::CreateUIPanel(*scene);
	first.SetParent(row);
	second.SetParent(row);
	row.AddComponent<HorizontalLayoutGroupComponent>();
	row.AddComponent<ContentSizeFitterComponent>().HorizontalFit = true;
	first.GetComponent<RectTransform2DComponent>().SizeDelta = { 100.0f, 30.0f };
	second.GetComponent<RectTransform2DComponent>().SizeDelta = { 100.0f, 30.0f };
	UpdateUILayout(*scene);
	const float initialWidth = row.GetComponent<RectTransform2DComponent>().SizeDelta.x;
	UpdateUILayout(*scene);
	CHECK(row.GetComponent<RectTransform2DComponent>().SizeDelta.x > initialWidth);
	UpdateUILayout(*scene);
	const uint64_t revision = GetUILayoutRevision(*scene);
	UpdateUILayout(*scene);
	CHECK(GetUILayoutRevision(*scene) == revision);
	first.GetComponent<RectTransform2DComponent>().SizeDelta.x = 200.0f;
	UpdateUILayout(*scene);
	CHECK(GetUILayoutRevision(*scene) == revision + 1);
}

TEST_CASE("UILayout cache handles surface changes and world canvas component changes") {
	WideSurfaceScope surface;
	auto scene = Scene::CreateDetachedScene("Layout Cache Surface");
	Entity canvas = EntityHelper::CreateUIPanel(*scene);
	auto& rect = canvas.GetComponent<RectTransform2DComponent>();
	rect.SizeDelta = { 400.0f, 200.0f };
	rect.FillWidth = true;
	UpdateUILayout(*scene);
	CHECK(rect.GetSize().x == doctest::Approx(2400.0f));
	Window::SetUIRegion(0, 0, 800, 600);
	UpdateUILayout(*scene);
	CHECK(rect.GetSize().x == doctest::Approx(800.0f));
	canvas.AddComponent<Transform3DComponent>();
	UpdateUILayout(*scene);
	CHECK(rect.GetSize().x == doctest::Approx(400.0f));
	const uint64_t revision = GetUILayoutRevision(*scene);
	UpdateUILayout(*scene);
	CHECK(GetUILayoutRevision(*scene) == revision);
}

TEST_CASE("World UI keeps local layout independent of screen dimensions") {
	WideSurfaceScope surface;
	auto scene = Scene::CreateDetachedScene("World UI");
	Entity canvas = EntityHelper::CreateUIPanel(*scene);
	Entity child = EntityHelper::CreateUIPanel(*scene);
	canvas.AddComponent<Transform3DComponent>();
	child.SetParent(canvas);
	canvas.GetComponent<RectTransform2DComponent>().SizeDelta = { 400.0f, 200.0f };
	child.GetComponent<RectTransform2DComponent>().FillWidth = true;
	ComputeUILayout(*scene);
	CHECK(canvas.GetComponent<RectTransform2DComponent>().GetSize().x == doctest::Approx(400.0f));
	CHECK(child.GetComponent<RectTransform2DComponent>().GetSize().x == doctest::Approx(400.0f));
	CHECK(WorldUI::FindRoot(scene->GetRegistry(), child.GetHandle()) == canvas.GetHandle());
	Window::SetUIRegion(0, 0, 800, 600);
	ComputeUILayout(*scene);
	CHECK(canvas.GetComponent<RectTransform2DComponent>().GetSize().x == doctest::Approx(400.0f));
	CHECK(child.GetComponent<RectTransform2DComponent>().GetSize().x == doctest::Approx(400.0f));
}

TEST_CASE("World UI pointer projection recovers a point on a rotated perspective canvas") {
	const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.6f, 0.1f, 100.0f);
	const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.5f, 0.0f))
		* glm::rotate(glm::mat4(1.0f), 0.6f, glm::vec3(0.0f, 1.0f, 0.0f))
		* glm::scale(glm::mat4(1.0f), glm::vec3(0.01f, 0.01f, 1.0f));
	const glm::mat4 vp = projection * view * model;
	const glm::vec4 clip = vp * glm::vec4(75.0f, -30.0f, 0.0f, 1.0f);
	Vec2 point;
	REQUIRE(WorldUI::ProjectPointer(vp, { clip.x / clip.w, clip.y / clip.w }, point));
	CHECK(point.x == doctest::Approx(75.0f).epsilon(0.001));
	CHECK(point.y == doctest::Approx(-30.0f).epsilon(0.001));
	CHECK_FALSE(WorldUI::ProjectPointer(glm::mat4(0.0f), {}, point));
}

TEST_CASE("World UI layout carries canvas state through bridges and refreshes direct edits") {
	WideSurfaceScope surface;
	auto scene = Scene::CreateDetachedScene("Nested World UI");
	auto& registry = scene->GetRegistry();
	auto add = [&](EntityHandle parent, bool withRect = true) {
		const auto entity = registry.create();
		registry.emplace<HierarchyComponent>(entity).Parent = parent;
		if (parent != entt::null) registry.get<HierarchyComponent>(parent).Children.push_back(entity);
		if (withRect) registry.emplace<RectTransform2DComponent>(entity).SizeDelta = { 400.0f, 200.0f };
		return entity;
	};
	const auto overlay = add(entt::null);
	const auto world = add(overlay);
	const auto bridge = add(world, false);
	const auto child = add(bridge);
	const auto nestedWorld = add(bridge);
	const auto nestedChild = add(nestedWorld);
	registry.emplace<Transform3DComponent>(world);
	registry.emplace<Transform3DComponent>(nestedWorld);
	registry.get<RectTransform2DComponent>(overlay).LocalScale = { 2.0f, 2.0f };
	registry.get<RectTransform2DComponent>(overlay).LocalRotation = 45.0f;
	registry.get<RectTransform2DComponent>(world).LocalScale = { 3.0f, 3.0f };
	registry.get<RectTransform2DComponent>(world).LocalRotation = 20.0f;
	registry.get<RectTransform2DComponent>(child).FillWidth = true;
	registry.get<RectTransform2DComponent>(child).IgnoreSafeArea = true;
	registry.get<RectTransform2DComponent>(nestedChild).FillWidth = true;

	ComputeUILayout(*scene);
	CHECK(registry.get<RectTransform2DComponent>(child).Scale.x == doctest::Approx(3.0f));
	CHECK(registry.get<RectTransform2DComponent>(child).Rotation == doctest::Approx(20.0f));
	CHECK(registry.get<RectTransform2DComponent>(nestedChild).Scale.x == doctest::Approx(1.0f));
	CHECK(registry.get<RectTransform2DComponent>(nestedChild).Rotation == doctest::Approx(0.0f));
	CHECK(registry.get<RectTransform2DComponent>(nestedChild).GetSize().x == doctest::Approx(400.0f));

	registry.remove<Transform3DComponent>(world);
	ComputeUILayout(*scene);
	CHECK(registry.get<RectTransform2DComponent>(child).Scale.x == doctest::Approx(6.0f));
	CHECK(registry.get<RectTransform2DComponent>(child).Rotation == doctest::Approx(65.0f));
	CHECK(registry.get<RectTransform2DComponent>(nestedChild).Scale.x == doctest::Approx(1.0f));

	registry.emplace<Transform3DComponent>(world);
	ComputeUILayout(*scene);
	CHECK(registry.get<RectTransform2DComponent>(child).Scale.x == doctest::Approx(3.0f));
	CHECK(registry.get<RectTransform2DComponent>(child).Rotation == doctest::Approx(20.0f));
}

TEST_CASE("World UI picks refresh canvas projections for each pointer and direct edit") {
	WideSurfaceScope surface;
	struct RestoreModules {
		std::vector<std::string> Previous = EngineModules::GetActiveIds();
		~RestoreModules() { EngineModules::Configure(Previous); }
	} modules;
	EngineModules::Configure({ std::string(EngineModules::k_Id3D) });
	auto scene = Scene::CreateDetachedScene("World UI picks");
	auto& registry = scene->GetRegistry();
	const auto camera = registry.create();
	registry.emplace<Transform3DComponent>(camera).Position.z = 10.0f;
	registry.emplace<Camera3DComponent>(camera).Projection = CameraProjection::Orthographic;
	auto add = [&](EntityHandle parent, float x) {
		const auto entity = registry.create();
		auto& rect = registry.emplace<RectTransform2DComponent>(entity);
		rect.SizeDelta = { 200.0f, 100.0f };
		rect.AnchoredPosition.x = x;
		registry.emplace<HierarchyComponent>(entity).Parent = parent;
		if (parent != entt::null) registry.get<HierarchyComponent>(parent).Children.push_back(entity);
		return entity;
	};
	const auto leftCanvas = add(entt::null, 0.0f);
	const auto rightCanvas = add(entt::null, 0.0f);
	registry.emplace<Transform3DComponent>(leftCanvas).Position.x = -3.0f;
	registry.emplace<Transform3DComponent>(rightCanvas).Position.x = 3.0f;
	const auto bridge = registry.create();
	registry.emplace<HierarchyComponent>(bridge).Parent = leftCanvas;
	registry.get<HierarchyComponent>(leftCanvas).Children.push_back(bridge);
	const auto left = add(bridge, 0.0f);
	const auto right = add(rightCanvas, 0.0f);
	const auto overlay = add(entt::null, -324.0f);
	registry.emplace<InteractableComponent>(left);
	registry.emplace<InteractableComponent>(right);
	registry.emplace<InteractableComponent>(overlay);
	ComputeUILayout(*scene);

	CHECK(UIEventSystem::PickInteractable(*scene, { -324.0f, 0.0f }) == overlay);
	registry.get<InteractableComponent>(overlay).Interactable = false;
	CHECK(UIEventSystem::PickInteractable(*scene, { -324.0f, 0.0f }) == left);
	CHECK(UIEventSystem::PickInteractable(*scene, { 324.0f, 0.0f }) == right);
	CHECK(UIEventSystem::PickInteractable(*scene, { -324.0f, 0.0f }) == left);

	registry.get<Transform3DComponent>(leftCanvas).Position.x = -8.0f;
	CHECK(UIEventSystem::PickInteractable(*scene, { -324.0f, 0.0f }) == entt::null);
	registry.get<Transform3DComponent>(camera).Position.x = 2.0f;
	CHECK(UIEventSystem::PickInteractable(*scene, { 108.0f, 0.0f }) == right);
	CHECK(UIEventSystem::PickInteractable(*scene, { 324.0f, 0.0f }) == entt::null);

	Window::SetUIRegion(0, 0, 800, 600);
	CHECK(UIEventSystem::PickInteractable(*scene, { 60.0f, 0.0f }) == right);
	CHECK(UIEventSystem::PickInteractable(*scene, { 324.0f, 0.0f }) == entt::null);

	registry.remove<Transform3DComponent>(rightCanvas);
	ComputeUILayout(*scene);
	CHECK(UIEventSystem::PickInteractable(*scene, { -50.0f, 0.0f }) == right);
	registry.emplace<DisabledTag>(rightCanvas);
	CHECK(UIEventSystem::PickInteractable(*scene, { -50.0f, 0.0f }) == entt::null);
	registry.remove<DisabledTag>(rightCanvas);
	CHECK(UIEventSystem::PickInteractable(*scene, { -50.0f, 0.0f }) == right);
}
