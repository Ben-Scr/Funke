#include <doctest/doctest.h>

#include "Gui/HierarchyGuides.hpp"
#include "Gui/SceneHierarchyState.hpp"
#include "Components/General/NameComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Scene/Entity.hpp"

#include <random>

using namespace Funke;

TEST_CASE("Hierarchy caches keep equal scene IDs and entity handles independent") {
	auto first = Scene::CreateDetachedScene("First");
	auto second = Scene::CreateDetachedScene("Second");
	second->SetSceneId(first->GetSceneId());
	Entity firstRoot = first->CreateEntity("Root");
	Entity firstChild = first->CreateEntity("Child");
	firstChild.SetParent(firstRoot);
	Entity secondRoot = second->CreateEntity("Root");
	Entity secondChild = second->CreateEntity("Child");
	secondChild.SetParent(secondRoot);
	REQUIRE(firstRoot.GetHandle() == secondRoot.GetHandle());
	auto& a = SceneHierarchyState::Get(*first);
	auto& b = SceneHierarchyState::Get(*second);
	CHECK(&a != &b);
	CHECK(a.Update(*first, ""));
	CHECK(b.Update(*second, ""));
	const auto* rows = a.VisibleEntityOrder.data();
	for (int frame = 0; frame < 10; ++frame) {
		CHECK_FALSE(a.Update(*first, ""));
		CHECK_FALSE(b.Update(*second, ""));
	}
	CHECK(a.VisibleEntityOrder.data() == rows);
	a.CollapsedEntities.insert(static_cast<uint32_t>(firstRoot.GetHandle()));
	a.Dirty = true;
	CHECK(a.Update(*first, ""));
	CHECK_FALSE(b.Update(*second, ""));
	CHECK(a.VisibleEntityOrder == std::vector<EntityHandle>{ firstRoot.GetHandle() });
	CHECK(b.VisibleEntityOrder == std::vector<EntityHandle>{ secondRoot.GetHandle(), secondChild.GetHandle() });
}

TEST_CASE("Hierarchy cache invalidates same-count reparents and reused entity slots") {
	auto scene = Scene::CreateDetachedScene("Structural changes");
	Entity root = scene->CreateEntity("Root");
	Entity child = scene->CreateEntity("Child");
	auto& state = SceneHierarchyState::Get(*scene);
	CHECK(state.Update(*scene, ""));
	CHECK(state.VisibleEntityDepths == std::vector<int>{ 0, 0 });
	child.SetParent(root);
	CHECK(state.Update(*scene, ""));
	CHECK(state.VisibleEntityDepths == std::vector<int>{ 0, 1 });
	const EntityHandle oldChild = child.GetHandle();
	scene->DestroyEntity(oldChild);
	Entity replacement = scene->CreateEntity("Replacement");
	CHECK(state.Update(*scene, ""));
	CHECK(state.VisibleEntityOrder == std::vector<EntityHandle>{ root.GetHandle(), replacement.GetHandle() });
	CHECK(state.VisibleEntityDepths == std::vector<int>{ 0, 0 });
	CHECK_FALSE(state.Update(*scene, ""));
	replacement.SetParent(root);
	Entity sibling = scene->CreateEntity("Sibling");
	sibling.SetParent(root);
	state.Update(*scene, "");
	root.GetComponent<HierarchyComponent>().Children = { sibling.GetHandle(), replacement.GetHandle() };
	scene->MarkHierarchyStructureDirty();
	CHECK(state.Update(*scene, ""));
	CHECK(state.VisibleEntityOrder == std::vector<EntityHandle>{ root.GetHandle(), sibling.GetHandle(), replacement.GetHandle() });
}

TEST_CASE("Hierarchy search clears per scene and observes names edited without structural changes") {
	auto first = Scene::CreateDetachedScene("Search first");
	auto second = Scene::CreateDetachedScene("Search second");
	for (Scene* scene : { first.get(), second.get() }) {
		Entity root = scene->CreateEntity("Root");
		Entity child = scene->CreateEntity("Needle");
		child.SetParent(root);
		auto& state = SceneHierarchyState::Get(*scene);
		state.CollapsedEntities.insert(static_cast<uint32_t>(root.GetHandle()));
		CHECK(state.Update(*scene, "nEeDlE"));
		CHECK(state.VisibleEntityOrder == std::vector<EntityHandle>{ child.GetHandle() });
		CHECK(state.VisibleEntityDepths == std::vector<int>{ 0 });
		child.GetComponent<NameComponent>().Name = "Renamed";
		CHECK_FALSE(state.Update(*scene, "nEeDlE"));
		CHECK(state.VisibleEntityOrder.empty());
	}
	for (Scene* scene : { first.get(), second.get() }) {
		auto& state = SceneHierarchyState::Get(*scene);
		CHECK(state.Update(*scene, ""));
		CHECK(state.VisibleEntityOrder.size() == 1);
		CHECK(state.EntityOrder.size() == 2);
	}
}

TEST_CASE("Hierarchy cache preserves manual root order and starts fresh with a replacement scene") {
	auto scene = Scene::CreateDetachedScene("Order");
	Entity first = scene->CreateEntity("First");
	Entity second = scene->CreateEntity("Second");
	auto& state = SceneHierarchyState::Get(*scene);
	state.Update(*scene, "");
	state.EntityOrder = { second.GetHandle(), first.GetHandle() };
	state.Dirty = true;
	CHECK(state.Update(*scene, ""));
	Entity third = scene->CreateEntity("Third");
	CHECK(state.Update(*scene, ""));
	CHECK(state.VisibleEntityOrder == std::vector<EntityHandle>{ second.GetHandle(), first.GetHandle(), third.GetHandle() });
	scene.reset();
	scene = Scene::CreateDetachedScene("Order");
	auto& fresh = SceneHierarchyState::Get(*scene);
	CHECK(fresh.EntityOrder.empty());
	CHECK(fresh.CollapsedEntities.empty());
	CHECK(fresh.Update(*scene, ""));
}

TEST_CASE("Hierarchy cache observes equal-size bulk replacement") {
	auto scene = Scene::CreateDetachedScene("Bulk replacement");
	Entity old = scene->CreateEntity("Old");
	auto& state = SceneHierarchyState::Get(*scene);
	state.Update(*scene, "");
	scene->DestroyEntity(old.GetHandle());
	std::vector<EntityHandle> created(1);
	scene->CreateEntitiesBulk(created.size(), created);
	CHECK(state.Update(*scene, ""));
	CHECK(state.VisibleEntityOrder == created);
}

TEST_CASE("Hierarchy guides connect siblings without crossing root boundaries") {
	HierarchyGuides guides;
	const std::vector<int> depths{ 0, 1, 2, 2, 1, 2, 0, 1 };
	guides.Rebuild(depths);
	const std::vector<uint64_t> expectedMasks{ 0, 0, 2, 2, 0, 0, 0, 0 };
	const std::vector<bool> expectedLast{ false, false, false, true, true, true, true, true };
	CHECK(guides.GetContinueMasks() == expectedMasks);
	CHECK(guides.GetLastChildren() == expectedLast);
	guides.Rebuild(std::vector<int>{ 0, 1 });
	CHECK(guides.GetContinueMasks() == std::vector<uint64_t>{ 0, 0 });
	CHECK(guides.GetLastChildren() == std::vector<bool>{ true, true });
	guides.Rebuild({});
	CHECK(guides.GetContinueMasks().empty());
	CHECK(guides.GetLastChildren().empty());
}

TEST_CASE("Hierarchy guides preserve the depth cap and legacy connection layout") {
	std::mt19937 random(8734);
	HierarchyGuides guides;
	for (int iteration = 0; iteration < 30; ++iteration) {
		std::vector<int> depths;
		for (int i = 0; i < 1000; ++i) depths.push_back(static_cast<int>(random() % 75) - 3);
		std::vector<uint64_t> masks(depths.size(), 0);
		std::vector<bool> last(depths.size(), true), siblings;
		for (size_t i = depths.size(); i-- > 0;) {
			const int d = std::clamp(depths[i], 0, 63);
			if (siblings.size() <= static_cast<size_t>(d)) siblings.resize(d + 1, false);
			last[i] = !siblings[d];
			for (int k = 1; k < d; ++k) if (siblings[k]) masks[i] |= (uint64_t{ 1 } << k);
			siblings[d] = true;
			std::fill(siblings.begin() + d + 1, siblings.end(), false);
		}
		guides.Rebuild(depths);
		CHECK(guides.GetContinueMasks() == masks);
		CHECK(guides.GetLastChildren() == last);
	}
}
