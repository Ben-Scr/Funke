#include <doctest/doctest.h>

#include "Core/EngineModules.hpp"
#include "Project/FunkeProject.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics3D/Rigidbody3DComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace Funke;

namespace {
	struct ModuleStateRestore {
		~ModuleStateRestore() { EngineModules::ActivateAllCompiled(); }
	};

	struct DependencyRootComponent {
		int AddCount = 0;
	};

	struct DependencyChildComponent {
		int AddCount = 0;
	};

	struct DependencyBlockingTag {};

	struct DirectMutationComponent {
		EntityHandle Owner = entt::null;
		int Value = 17;
		DirectMutationComponent() = default;
		explicit DirectMutationComponent(EntityHandle owner) : Owner(owner) {}
	};

	struct MutationSignals {
		int Constructed = 0;
		int Destroyed = 0;
		void OnConstruct(entt::registry&, EntityHandle) { ++Constructed; }
		void OnDestroy(entt::registry&, EntityHandle) { ++Destroyed; }
	};
}

TEST_CASE("EngineModules: project module ids activate only compiled, listed modules") {
	ModuleStateRestore restore;

	EngineModules::Configure({ std::string(EngineModules::k_Id2D) });
	CHECK(EngineModules::IsActive(EngineModule::TwoD) == EngineModules::IsCompiled(EngineModule::TwoD));
	CHECK_FALSE(EngineModules::IsActive(EngineModule::ThreeD));

	EngineModules::Configure({ std::string(EngineModules::k_Id3D), "Some.Unknown.Module" });
	CHECK_FALSE(EngineModules::IsActive(EngineModule::TwoD));
	CHECK(EngineModules::IsActive(EngineModule::ThreeD) == EngineModules::IsCompiled(EngineModule::ThreeD));

	EngineModules::Configure({});
	CHECK_FALSE(EngineModules::IsActive(EngineModule::TwoD));
	CHECK_FALSE(EngineModules::IsActive(EngineModule::ThreeD));

	EngineModules::ActivateAllCompiled();
	CHECK(EngineModules::IsActive(EngineModule::ThreeD) == EngineModules::IsCompiled(EngineModule::ThreeD));
}

TEST_CASE("EngineModules: ids round-trip and missing modules are reported") {
	for (EngineModule module : k_AllEngineModules) {
		const std::optional<EngineModule> parsed = EngineModules::FromId(EngineModules::GetId(module));
		REQUIRE(parsed.has_value());
		CHECK(*parsed == module);
	}
	CHECK_FALSE(EngineModules::FromId("Funke.4D").has_value());

	const std::vector<std::string> missing = EngineModules::GetMissingModules(
		{ std::string(EngineModules::k_Id2D), std::string(EngineModules::k_Id3D) });
	for (EngineModule module : k_AllEngineModules) {
		const bool reported = std::find(missing.begin(), missing.end(), EngineModules::GetId(module)) != missing.end();
		CHECK(reported == !EngineModules::IsCompiled(module));
	}
}

TEST_CASE("EngineModules: projects without a modules field keep both; PrefersThreeD needs 3D only") {
	FunkeProject project;
	CHECK(project.HasModule(FunkeProject::k_Module2D));
	CHECK(project.HasModule(FunkeProject::k_Module3D));
	CHECK_FALSE(project.PrefersThreeD());

	project.Modules = { std::string(FunkeProject::k_Module3D) };
	CHECK(project.PrefersThreeD());
}

TEST_CASE("EngineModules: 3D components are tagged with their module and hidden while inactive") {
	ModuleStateRestore restore;
	ComponentRegistry registry;
	RegisterBuiltInComponents(registry);

	bool foundTransform3D = false;
	registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
		if (info.serializedName == "Transform3D") {
			foundTransform3D = true;
			REQUIRE(info.engineModule.has_value());
			CHECK(*info.engineModule == EngineModule::ThreeD);
			EngineModules::Configure({ std::string(EngineModules::k_Id2D) });
			CHECK_FALSE(info.IsModuleActive());
			EngineModules::ActivateAllCompiled();
			CHECK(info.IsModuleActive());
		}
		if (info.serializedName == "Name") {
			CHECK_FALSE(info.engineModule.has_value());
		}
	});
	CHECK(foundTransform3D == EngineModules::IsCompiled(EngineModule::ThreeD));
}

TEST_CASE("EngineModules: every module binding slot has a signature-matched stub") {
	for (EngineModule module : k_AllEngineModules) {
		CAPTURE(EngineModules::GetId(module));
		CHECK(ScriptBindings::FindUnstubbedModuleSlots(module).empty());
	}
}

TEST_CASE("EngineModules: 2D and 3D components never share an entity, and neither sits on a UI rect") {
	ComponentRegistry registry;
	RegisterBuiltInComponents(registry);

	std::vector<std::type_index> twoD;
	std::vector<std::type_index> threeD;
	registry.ForEachComponentInfo([&](const std::type_index& id, const ComponentInfo& info) {
		if (!info.engineModule || info.category != ComponentCategory::Component) return;
		(*info.engineModule == EngineModule::TwoD ? twoD : threeD).push_back(id);
	});
	REQUIRE_FALSE(twoD.empty());
	REQUIRE_FALSE(threeD.empty());
	const std::type_index rect = typeid(RectTransform2DComponent);
	for (const std::type_index& a : twoD) {
		CAPTURE(a.name());
		CHECK(registry.TypesConflict(a, rect));
		for (const std::type_index& b : threeD) {
			CAPTURE(b.name());
			CHECK(registry.TypesConflict(a, b));
		}
	}
	for (const std::type_index& b : threeD) {
		CAPTURE(b.name());
		CHECK(registry.TypesConflict(b, rect));
	}
}

TEST_CASE("EngineModules: adding a component of the other module is refused") {
	ComponentRegistry registry;
	RegisterBuiltInComponents(registry);
	auto scene = Scene::CreateDetachedScene("ModuleConflicts");

	Entity entity2D = scene->CreateEmptyEntity();
	entity2D.AddComponent<Transform2DComponent>();
	CHECK_FALSE(registry.AddWithDependencies(entity2D, typeid(Rigidbody3DComponent)));
	CHECK_FALSE(entity2D.HasComponent<Rigidbody3DComponent>());
	CHECK_FALSE(entity2D.HasComponent<Transform3DComponent>());
	CHECK_FALSE(registry.AddWithDependencies(entity2D, typeid(MeshRendererComponent)));
	CHECK(registry.AddWithDependencies(entity2D, typeid(SpriteRendererComponent)));

	Entity entity3D = scene->CreateEmptyEntity();
	CHECK(registry.AddWithDependencies(entity3D, typeid(Rigidbody3DComponent)));
	CHECK(entity3D.HasComponent<Transform3DComponent>());
	CHECK_FALSE(registry.AddWithDependencies(entity3D, typeid(Rigidbody2DComponent)));
	CHECK_FALSE(registry.AddWithDependencies(entity3D, typeid(Transform2DComponent)));
	const ComponentInfo* conflict = registry.FindConflict(entity3D, typeid(SpriteRendererComponent));
	REQUIRE(conflict != nullptr);
	CHECK((conflict->typeId == std::type_index(typeid(Rigidbody3DComponent)) || conflict->typeId == std::type_index(typeid(Transform3DComponent))));

	Entity uiEntity = scene->CreateEmptyEntity();
	uiEntity.AddComponent<RectTransform2DComponent>();
	CHECK_FALSE(registry.AddWithDependencies(uiEntity, typeid(Rigidbody2DComponent)));
	CHECK_FALSE(registry.AddWithDependencies(uiEntity, typeid(MeshRendererComponent)));
}

TEST_CASE("EngineModules: scenes that already mix the modules are detected, not rejected") {
	ComponentRegistry registry;
	RegisterBuiltInComponents(registry);
	auto scene = Scene::CreateDetachedScene("LegacyMix");
	Entity entity = scene->CreateEmptyEntity();
	entity.AddComponent<Transform2DComponent>();
	CHECK(registry.FindConflictingPair(entity).first == nullptr);
	entity.AddComponent<Rigidbody3DComponent>();
	const auto [first, second] = registry.FindConflictingPair(entity);
	REQUIRE(first != nullptr);
	REQUIRE(second != nullptr);
	CHECK(registry.TypesConflict(first->typeId, second->typeId));
}

TEST_CASE("ComponentRegistry: pool conflict checks support tags and reject stale entities") {
	ComponentRegistry registry;
	registry.Register<DependencyRootComponent>({ "DependencyRoot", ComponentCategory::Component });
	registry.Register<DependencyBlockingTag>({ "DependencyBlocking", ComponentCategory::Tag });
	registry.DeclareConflict(typeid(DependencyRootComponent), typeid(DependencyBlockingTag));
	auto scene = Scene::CreateDetachedScene("PoolConflicts");
	Entity entity = scene->CreateEmptyEntity();
	const ComponentInfo* rootInfo = registry.GetInfo<DependencyRootComponent>();
	REQUIRE(rootInfo != nullptr);
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *rootInfo) == nullptr);
	entity.AddComponent<DependencyBlockingTag>();
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *rootInfo)
		== registry.GetInfo<DependencyBlockingTag>());
	CHECK_FALSE(registry.AddWithDependencies(entity, typeid(DependencyRootComponent)));
	entity.RemoveComponent<DependencyBlockingTag>();
	CHECK(registry.AddWithDependencies(entity, typeid(DependencyRootComponent)));
	entity.AddComponent<DependencyBlockingTag>();
	CHECK(registry.FindConflictingPair(entity).first != nullptr);
	const EntityHandle stale = entity.GetHandle();
	scene->DestroyEntity(entity);
	Entity replacement = scene->CreateEmptyEntity();
	replacement.AddComponent<DependencyBlockingTag>();
	CHECK(registry.FindConflict(scene->GetRegistry(), stale, *rootInfo) == nullptr);
	CHECK(registry.FindConflict(entity, typeid(DependencyRootComponent)) == nullptr);
	CHECK(registry.FindConflictingPair(entity).first == nullptr);
}

TEST_CASE("ComponentRegistry: dependency-only addition breaks cycles without adding the root") {
	ComponentRegistry registry;
	ComponentInfo rootInfo{ "DependencyRoot", ComponentCategory::Component };
	rootInfo.dependsOn.push_back(typeid(DependencyChildComponent));
	rootInfo.onAdd = [](Entity entity) { ++entity.GetComponent<DependencyRootComponent>().AddCount; };
	registry.Register<DependencyRootComponent>(std::move(rootInfo));
	ComponentInfo childInfo{ "DependencyChild", ComponentCategory::Component };
	childInfo.dependsOn.push_back(typeid(DependencyRootComponent));
	childInfo.onAdd = [](Entity entity) { ++entity.GetComponent<DependencyChildComponent>().AddCount; };
	registry.Register<DependencyChildComponent>(std::move(childInfo));
	auto scene = Scene::CreateDetachedScene("DependencyCycles");
	Entity entity = scene->CreateEmptyEntity();
	registry.AddDependencies(entity, typeid(DependencyRootComponent));
	CHECK_FALSE(entity.HasComponent<DependencyRootComponent>());
	REQUIRE(entity.HasComponent<DependencyChildComponent>());
	CHECK(entity.GetComponent<DependencyChildComponent>().AddCount == 1);
	const ComponentInfo* registeredRoot = registry.GetInfo<DependencyRootComponent>();
	REQUIRE(registeredRoot != nullptr);
	CHECK(registry.AddWithDependencies(entity, *registeredRoot));
	CHECK(registry.AddWithDependencies(entity, *registeredRoot));
	REQUIRE(entity.HasComponent<DependencyRootComponent>());
	CHECK(entity.GetComponent<DependencyRootComponent>().AddCount == 1);
	CHECK(entity.GetComponent<DependencyChildComponent>().AddCount == 1);
}

TEST_CASE("ComponentRegistry: conflicting dependencies are skipped while leaf callbacks run once") {
	ComponentRegistry registry;
	ComponentInfo rootInfo{ "DependencyRoot", ComponentCategory::Component };
	rootInfo.dependsOn.push_back(typeid(DependencyChildComponent));
	registry.Register<DependencyRootComponent>(std::move(rootInfo));
	ComponentInfo childInfo{ "DependencyChild", ComponentCategory::Component };
	childInfo.onAdd = [](Entity entity) { ++entity.GetComponent<DependencyChildComponent>().AddCount; };
	registry.Register<DependencyChildComponent>(std::move(childInfo));
	registry.Register<DependencyBlockingTag>({ "DependencyBlocking", ComponentCategory::Tag });
	registry.DeclareConflict(typeid(DependencyChildComponent), typeid(DependencyBlockingTag));
	auto scene = Scene::CreateDetachedScene("DependencyConflicts");
	Entity entity = scene->CreateEmptyEntity();
	entity.AddComponent<DependencyBlockingTag>();
	registry.AddDependencies(entity, typeid(DependencyRootComponent));
	CHECK_FALSE(entity.HasComponent<DependencyRootComponent>());
	CHECK_FALSE(entity.HasComponent<DependencyChildComponent>());
	CHECK(registry.AddWithDependencies(entity, typeid(DependencyRootComponent)));
	CHECK_FALSE(entity.HasComponent<DependencyChildComponent>());
	entity.RemoveComponent<DependencyBlockingTag>();
	CHECK(registry.AddWithDependencies(entity, typeid(DependencyChildComponent)));
	CHECK(registry.AddWithDependencies(entity, typeid(DependencyChildComponent)));
	REQUIRE(entity.HasComponent<DependencyChildComponent>());
	CHECK(entity.GetComponent<DependencyChildComponent>().AddCount == 1);
}

TEST_CASE("ComponentRegistry: resolved conflicts follow mutable descriptors and re-registration") {
	ComponentRegistry registry;
	registry.Register<DependencyRootComponent>({ "DependencyRoot", ComponentCategory::Component });
	registry.Register<DependencyChildComponent>({ "DependencyChild", ComponentCategory::Component });
	registry.Register<DependencyBlockingTag>({ "DependencyBlocking", ComponentCategory::Tag });
	auto scene = Scene::CreateDetachedScene("MutableConflicts");
	Entity entity = scene->CreateEmptyEntity();
	entity.AddComponent<DependencyChildComponent>();
	ComponentInfo* root = registry.GetInfo<DependencyRootComponent>();
	REQUIRE(root != nullptr);
	root->conflictsWith.push_back(typeid(DependencyChildComponent));
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root)
		== registry.GetInfo<DependencyChildComponent>());
	root->conflictsWith[0] = typeid(DependencyBlockingTag);
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root) == nullptr);
	registry.ForEachComponentInfo([](const std::type_index& id, ComponentInfo& info) {
		if (id == typeid(DependencyRootComponent)) info.conflictsWith[0] = typeid(DependencyChildComponent);
	});
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root)
		== registry.GetInfo<DependencyChildComponent>());
	root->conflictsWith.clear();
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root) == nullptr);
	root->conflictsWith.push_back(typeid(DependencyChildComponent));
	CHECK_FALSE(registry.AddWithDependencies(entity, *root));
	ComponentInfo updated{ "UpdatedDependencyRoot", ComponentCategory::Component };
	updated.conflictsWith.push_back(typeid(DependencyBlockingTag));
	registry.Register<DependencyRootComponent>(std::move(updated));
	root = registry.GetInfo<DependencyRootComponent>();
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root) == nullptr);
	entity.AddComponent<DependencyBlockingTag>();
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root)
		== registry.GetInfo<DependencyBlockingTag>());
	registry.GetInfo<DependencyBlockingTag>()->has = nullptr;
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root) == nullptr);
	registry.Register<DependencyBlockingTag>({ "DependencyBlocking", ComponentCategory::Tag });
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root)
		== registry.GetInfo<DependencyBlockingTag>());
}

TEST_CASE("ComponentRegistry: resolved conflicts survive registration unload and registry switches") {
	ComponentRegistry registry;
	registry.Register<DependencyRootComponent>({ "DependencyRoot", ComponentCategory::Component });
	ComponentInfo* root = registry.GetInfo<DependencyRootComponent>();
	root->conflictsWith.push_back(typeid(DependencyBlockingTag));
	auto scene = Scene::CreateDetachedScene("ReloadedConflicts");
	Entity entity = scene->CreateEmptyEntity();
	entity.AddComponent<DependencyBlockingTag>();
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root) == nullptr);
	registry.SetRegistrationOwner(71);
	registry.Register<DependencyBlockingTag>({ "DependencyBlocking", ComponentCategory::Tag });
	registry.SetRegistrationOwner(0);
	const uint32_t previousId = registry.GetInfo<DependencyBlockingTag>()->typeIdU32;
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root)
		== registry.GetInfo<DependencyBlockingTag>());
	CHECK(registry.UnregisterOwner(71) == 1);
	CHECK(registry.GetByTypeId(previousId) == nullptr);
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root) == nullptr);
	root->conflictsWith.push_back(typeid(DependencyBlockingTag));
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root) == nullptr);
	registry.Register<DependencyBlockingTag>({ "DependencyBlocking", ComponentCategory::Tag });
	CHECK(registry.GetInfo<DependencyBlockingTag>()->typeIdU32 != previousId);
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root)
		== registry.GetInfo<DependencyBlockingTag>());
	ComponentRegistry alternate;
	alternate.Register<DependencyRootComponent>({ "OtherRoot", ComponentCategory::Component });
	alternate.Register<DependencyChildComponent>({ "OtherChild", ComponentCategory::Component });
	alternate.DeclareConflict(typeid(DependencyRootComponent), typeid(DependencyChildComponent));
	CHECK(alternate.FindConflict(entity, typeid(DependencyRootComponent)) == nullptr);
	CHECK(registry.FindConflict(scene->GetRegistry(), entity.GetHandle(), *root)
		== registry.GetInfo<DependencyBlockingTag>());
}

TEST_CASE("ComponentRegistry: direct callbacks preserve constructors tags and storage signals") {
	ComponentRegistry components;
	components.Register<DirectMutationComponent>({ "DirectMutation", ComponentCategory::Component });
	components.Register<DependencyBlockingTag>({ "DirectTag", ComponentCategory::Tag });
	const ComponentInfo* info = components.GetInfo<DirectMutationComponent>();
	const ComponentInfo* tag = components.GetInfo<DependencyBlockingTag>();
	REQUIRE(info->has.RegistryCallback() != nullptr);
	REQUIRE(info->add.RegistryCallback() != nullptr);
	REQUIRE(info->remove.RegistryCallback() != nullptr);
	MutationSignals signals;
	entt::registry registry;
	registry.on_construct<DirectMutationComponent>().connect<&MutationSignals::OnConstruct>(signals);
	registry.on_destroy<DirectMutationComponent>().connect<&MutationSignals::OnDestroy>(signals);
	const EntityHandle entity = registry.create();
	CHECK_FALSE(info->has.RegistryCallback()(registry, entity));
	info->add.RegistryCallback()(registry, entity);
	REQUIRE(info->has.RegistryCallback()(registry, entity));
	CHECK(registry.get<DirectMutationComponent>(entity).Owner == entity);
	registry.get<DirectMutationComponent>(entity).Value = 42;
	info->add.RegistryCallback()(registry, entity);
	CHECK(registry.get<DirectMutationComponent>(entity).Value == 42);
	CHECK(signals.Constructed == 1);
	info->remove.RegistryCallback()(registry, entity);
	info->remove.RegistryCallback()(registry, entity);
	CHECK_FALSE(info->has.RegistryCallback()(registry, entity));
	CHECK(signals.Destroyed == 1);
	tag->add.RegistryCallback()(registry, entity);
	tag->add.RegistryCallback()(registry, entity);
	CHECK(tag->has.RegistryCallback()(registry, entity));
	tag->remove.RegistryCallback()(registry, entity);
	CHECK_FALSE(tag->has.RegistryCallback()(registry, entity));
}

TEST_CASE("ComponentRegistry: callback replacement disables only its own direct operation") {
	ComponentRegistry registry;
	registry.Register<DirectMutationComponent>({ "DirectMutation", ComponentCategory::Component });
	ComponentInfo* info = registry.GetInfo<DirectMutationComponent>();
	const auto originalHas = info->has;
	const auto originalAdd = info->add;
	const auto originalRemove = info->remove;
	int checks = 0;
	info->has = [&checks, calls = 0](Entity) mutable { checks = ++calls; return false; };
	CHECK(info->has.RegistryCallback() == nullptr);
	CHECK(info->add.RegistryCallback() == originalAdd.RegistryCallback());
	CHECK(info->remove.RegistryCallback() == originalRemove.RegistryCallback());
	auto scene = Scene::CreateDetachedScene("DirectCallbackOverrides");
	Entity entity = scene->CreateEmptyEntity();
	CHECK(registry.AddWithDependencies(entity, *info));
	REQUIRE(entity.HasComponent<DirectMutationComponent>());
	entity.GetComponent<DirectMutationComponent>().Value = 23;
	CHECK(registry.AddWithDependencies(entity, *info));
	CHECK(entity.GetComponent<DirectMutationComponent>().Value == 23);
	CHECK(checks == 2);
	info->has = originalHas;
	CHECK(info->has.RegistryCallback() == originalHas.RegistryCallback());
	int additions = 0;
	InlineFunction<void(Entity)> customAdd = [&additions, calls = 0](Entity target) mutable {
		++additions;
		target.AddComponent<DirectMutationComponent>().Value = 50 + ++calls;
	};
	info->add = customAdd;
	CHECK(info->add.RegistryCallback() == nullptr);
	CHECK(info->has.RegistryCallback() == originalHas.RegistryCallback());
	Entity second = scene->CreateEmptyEntity();
	CHECK(registry.AddWithDependencies(second, *info));
	CHECK(second.GetComponent<DirectMutationComponent>().Value == 51);
	CHECK(additions == 1);
	Entity third = scene->CreateEmptyEntity();
	CHECK(registry.AddWithDependencies(third, *info));
	CHECK(third.GetComponent<DirectMutationComponent>().Value == 52);
	CHECK(additions == 2);
	int removals = 0;
	registry.ForEachComponentInfo([&](const std::type_index& type, ComponentInfo& component) {
		if (type == typeid(DirectMutationComponent)) {
			component.remove = [&removals](Entity target) {
				++removals;
				target.GetComponent<DirectMutationComponent>().Value = 67;
			};
		}
	});
	CHECK(info->remove.RegistryCallback() == nullptr);
	info->remove(second);
	CHECK(second.GetComponent<DirectMutationComponent>().Value == 67);
	CHECK(removals == 1);
	auto moved = std::move(info->remove);
	CHECK_FALSE(info->remove);
	CHECK(info->remove.RegistryCallback() == nullptr);
	moved(second);
	CHECK(removals == 2);
	info->remove = originalRemove;
	auto directMoved = std::move(info->remove);
	CHECK_FALSE(info->remove);
	CHECK(info->remove.RegistryCallback() == nullptr);
	CHECK(directMoved.RegistryCallback() == originalRemove.RegistryCallback());
	info->remove = std::move(directMoved);
	CHECK_FALSE(directMoved);
	CHECK(directMoved.RegistryCallback() == nullptr);
	info->add = nullptr;
	CHECK(info->add == nullptr);
	CHECK(nullptr == info->add);
	CHECK(info->add.RegistryCallback() == nullptr);
	CHECK_FALSE(registry.AddWithDependencies(second, *info));
	registry.Register<DirectMutationComponent>({ "DirectMutationAgain", ComponentCategory::Component });
	CHECK(registry.GetInfo<DirectMutationComponent>()->add.RegistryCallback() != nullptr);
}

TEST_CASE("ComponentRegistry: direct dependency addition stops after entity destruction or descriptor removal") {
	ComponentRegistry registry;
	ComponentInfo root{ "DirectRoot", ComponentCategory::Component };
	root.dependsOn.push_back(typeid(DependencyChildComponent));
	registry.Register<DependencyRootComponent>(std::move(root));
	ComponentInfo child{ "DestroyingChild", ComponentCategory::Component };
	child.onAdd = [](Entity entity) { entity.Destroy(); };
	registry.Register<DependencyChildComponent>(std::move(child));
	auto scene = Scene::CreateDetachedScene("DirectCallbackLifetime");
	Entity entity = scene->CreateEmptyEntity();
	CHECK_FALSE(registry.AddWithDependencies(entity, typeid(DependencyRootComponent)));
	CHECK_FALSE(entity.IsValid());
	CHECK_FALSE(registry.AddWithDependencies(entity, typeid(DependencyRootComponent)));
	registry.SetRegistrationOwner(71);
	registry.Register<DirectMutationComponent>({ "UnregisteredDuringAdd", ComponentCategory::Component });
	registry.SetRegistrationOwner(0);
	ComponentInfo* transient = registry.GetInfo<DirectMutationComponent>();
	const uint32_t typeId = transient->typeIdU32;
	transient->add = [&registry](Entity target) {
		target.AddComponent<DirectMutationComponent>();
		registry.UnregisterOwner(71);
	};
	Entity survivor = scene->CreateEmptyEntity();
	CHECK_FALSE(registry.AddWithDependencies(survivor, *transient));
	CHECK(survivor.HasComponent<DirectMutationComponent>());
	CHECK(registry.GetByTypeId(typeId) == nullptr);
}
