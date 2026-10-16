#include <doctest/doctest.h>

#include "Core/PackageHost.hpp"
#include "Packages/StaticPackages.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneExtensions.hpp"

#include <algorithm>

using namespace Funke;

namespace {
	int s_ScenesCreated = 0;
	int s_ScenesDestroyed = 0;
	int s_PhysicsShutdowns = 0;

	struct ExtensionBodyComponent {
		int Value = 0;
	};

	SceneExtension MakeCountingSceneExtension() {
		SceneExtension extension;
		extension.Name = "Tests.SceneExtension";
		extension.OnSceneCreated = [](Scene&) { ++s_ScenesCreated; };
		extension.OnSceneDestroyed = [](Scene&) { ++s_ScenesDestroyed; };
		return extension;
	}

	PhysicsExtension2D MakeBodyExtension() {
		PhysicsExtension2D extension;
		extension.Name = "Tests.PhysicsExtension";
		extension.BodyComponent = entt::type_hash<ExtensionBodyComponent>::value();
		extension.Shutdown = [] { ++s_PhysicsShutdowns; };
		return extension;
	}

	int s_StaticLoads = 0;
	int s_StaticUnloads = 0;
	int StaticExport() { return 42; }

	size_t CountPhysicsExtensions(std::string_view name) {
		const auto& extensions = PhysicsSystem2D::GetExtensions();
		return static_cast<size_t>(std::count_if(extensions.begin(), extensions.end(),
			[&](const PhysicsExtension2D& e) { return e.Name == name; }));
	}
}

TEST_CASE("Scene extensions see scenes created and destroyed while registered")
{
	s_ScenesCreated = 0;
	s_ScenesDestroyed = 0;
	SceneExtensions::Register(MakeCountingSceneExtension());
	// Same name replaces the entry, so hooks don't run twice.
	SceneExtensions::Register(MakeCountingSceneExtension());

	{
		auto scene = Scene::CreateDetachedScene("Scene Extension Test");
		CHECK(s_ScenesCreated == 1);
		CHECK(s_ScenesDestroyed == 0);
	}
	CHECK(s_ScenesDestroyed == 1);

	SceneExtensions::Unregister("Tests.SceneExtension");
	{
		auto scene = Scene::CreateDetachedScene("Scene Extension Test After Unregister");
	}
	CHECK(s_ScenesCreated == 1);
	CHECK(s_ScenesDestroyed == 1);
}

TEST_CASE("Physics extensions own the transform of their body component until unregistered")
{
	s_PhysicsShutdowns = 0;
	PhysicsSystem2D::RegisterExtension(MakeBodyExtension());

	auto scene = Scene::CreateDetachedScene("Physics Extension Test");
	entt::registry& registry = scene->GetRegistry();
	const EntityHandle body = scene->CreateEntity("Body").GetHandle();
	const EntityHandle plain = scene->CreateEntity("Plain").GetHandle();
	registry.emplace<ExtensionBodyComponent>(body);

	CHECK(PhysicsSystem2D::IsTransformOwnedByPhysics(registry, body));
	CHECK_FALSE(PhysicsSystem2D::IsTransformOwnedByPhysics(registry, plain));

	// Re-registering shuts the old entry down and keeps a single one.
	PhysicsSystem2D::RegisterExtension(MakeBodyExtension());
	CHECK(s_PhysicsShutdowns == 1);
	CHECK(CountPhysicsExtensions("Tests.PhysicsExtension") == 1);

	PhysicsSystem2D::UnregisterExtension("Tests.PhysicsExtension");
	CHECK(s_PhysicsShutdowns == 2);
	CHECK(CountPhysicsExtensions("Tests.PhysicsExtension") == 0);
	CHECK_FALSE(PhysicsSystem2D::IsTransformOwnedByPhysics(registry, body));
}

TEST_CASE("Statically linked packages load through PackageHost and resolve their P/Invoke symbols")
{
	s_StaticLoads = 0;
	s_StaticUnloads = 0;
	static const StaticPackageSymbol k_Symbols[] = {
		{ "Tests_Static_Export", reinterpret_cast<const void*>(&StaticExport) },
	};
	StaticPackage package;
	package.Name = "Tests.Static";
	package.Version = "1.0.0";
	package.NativeLibrary = "Pkg.Tests.Static.Native";
	package.OnLoad = [] { ++s_StaticLoads; return 0; };
	package.OnUnload = [] { ++s_StaticUnloads; };
	package.Symbols = k_Symbols;
	package.SymbolCount = 1;
	StaticPackages::Register(package);

	CHECK(StaticPackages::FindSymbol("Pkg.Tests.Static.Native", "Tests_Static_Export") == k_Symbols[0].Address);
	CHECK(StaticPackages::FindSymbol("Pkg.Tests.Static.Native", "Missing") == nullptr);
	CHECK(StaticPackages::FindSymbol("Pkg.Other.Native", "Tests_Static_Export") == nullptr);

	PackageHost::LoadAll();
	CHECK(PackageHost::IsPackageLoaded("Tests.Static"));
	CHECK(s_StaticLoads == 1);
	// Idempotent like the DLL path.
	PackageHost::LoadAll();
	CHECK(s_StaticLoads == 1);

	PackageHost::UnloadAll();
	CHECK(s_StaticUnloads == 1);
	CHECK_FALSE(PackageHost::IsPackageLoaded("Tests.Static"));
}
