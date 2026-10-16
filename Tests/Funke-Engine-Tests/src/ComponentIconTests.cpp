#include <doctest/doctest.h>

#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Core/Application.hpp"
#include "Packages/PackageRegistration.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptComponent.hpp"

#if FUNKE_WITH_2D
#include "Components/Physics/Rigidbody2DComponent.hpp"
#endif
#if FUNKE_WITH_3D
#include "Components/Graphics/SpotLightComponent.hpp"
#include "Components/Physics3D/Collider3DComponents.hpp"
#endif

#include <filesystem>
#include <string>

using namespace Funke;

namespace {
	struct IconTestPackageComponent { int Value = 0; };
	struct IconTestLateComponent { int Value = 0; };
}

TEST_CASE("Built-in components carry editor icons that exist on disk") {
	ComponentRegistry registry;
	RegisterBuiltInComponents(registry);

	CHECK(registry.GetInfo<AudioSourceComponent>()->icon == "audio_source");
	CHECK(registry.GetInfo<ScriptComponent>()->icon == "script");
	CHECK(registry.GetInfo<NameComponent>()->icon.empty());
#if FUNKE_WITH_2D
	CHECK(registry.GetInfo<Rigidbody2DComponent>()->icon == "rigidbody_2d");
#endif
#if FUNKE_WITH_3D
	CHECK(registry.GetInfo<SpotLightComponent>()->icon == "light_spot");
	CHECK(registry.GetInfo<BoxCollider3DComponent>()->icon == "collider_box_3d");
#endif

	const std::filesystem::path iconDir = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()
		/ "Funke-Runtime" / "FunkeAssets" / "Textures" / "Editor" / "General";
	if (!std::filesystem::is_directory(iconDir)) {
		MESSAGE("Editor icon folder not found next to the test sources; skipping the file check");
		return;
	}
	registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
		if (info.icon.empty()) return;
		INFO(info.displayName << " -> " << info.icon);
		CHECK(std::filesystem::exists(iconDir / info.icon / (info.icon + "_32.png")));
	});
}

TEST_CASE("Packages set component icons at registration or afterwards") {
	Application app;
	Package::RegisterSerializableComponent<IconTestPackageComponent>("Icon Test", "", "IconTestPackageComponent",
		nullptr, nullptr, nullptr, ComponentCategory::Component, {}, "particles");
	Package::RegisterComponent<IconTestLateComponent>("Icon Test Late");

	ComponentRegistry& registry = SceneManager::GetComponentRegistry();
	REQUIRE(registry.GetInfo<IconTestPackageComponent>() != nullptr);
	CHECK(registry.GetInfo<IconTestPackageComponent>()->icon == "particles");
	REQUIRE(registry.GetInfo<IconTestLateComponent>() != nullptr);
	CHECK(registry.GetInfo<IconTestLateComponent>()->icon.empty());
	Package::SetComponentIcon<IconTestLateComponent>("audio_source");
	CHECK(registry.GetInfo<IconTestLateComponent>()->icon == "audio_source");
}
