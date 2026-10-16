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
#include <vector>

using namespace Funke;

namespace {
	struct ModuleStateRestore {
		~ModuleStateRestore() { EngineModules::ActivateAllCompiled(); }
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
