#include <doctest/doctest.h>

#include "Core/PackageHost.hpp"
#include "Project/IndexProject.hpp"
#include "Scene/ComponentRegistry.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#ifdef IDX_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
	class ScopedTempDirectory {
	public:
		ScopedTempDirectory() {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Path = std::filesystem::temp_directory_path()
				/ ("index-package-host-tests-" + std::to_string(suffix));
			std::filesystem::create_directories(Path);
		}

		~ScopedTempDirectory() {
			std::error_code ec;
			std::filesystem::remove_all(Path, ec);
		}

		std::filesystem::path Path;
	};

	void Touch(const std::filesystem::path& path) {
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path, std::ios::binary).put('\0');
	}

	void WriteAbiMetadata(const std::filesystem::path& packageLibrary,
		std::string_view contents) {
		std::ofstream metadata(
			Index::PackageHostDetail::GetNativePackageAbiMetadataPath(packageLibrary),
			std::ios::binary);
		metadata << contents;
	}
}

TEST_CASE("PackageHost requires native package ABI metadata") {
	ScopedTempDirectory temp;
	const std::filesystem::path library = temp.Path / "Pkg.Game.Tools.Native.dll";
	Touch(library);

	std::string reason;
	CHECK_FALSE(Index::PackageHostDetail::ValidateNativePackageCompatibility(
		library, std::string(64, 'a'), reason));
	CHECK(reason.find("missing") != std::string::npos);
}

TEST_CASE("PackageHost accepts native packages built against the running engine") {
	ScopedTempDirectory temp;
	const std::filesystem::path library = temp.Path / "Pkg.Game.Tools.Native.dll";
	Touch(library);
	WriteAbiMetadata(library,
		"schema=1\nboundary=engine_cpp\nengine_sha256=" + std::string(64, 'a') + "\n");

	std::string reason;
	CHECK(Index::PackageHostDetail::ValidateNativePackageCompatibility(
		library, std::string(64, 'A'), reason));
	CHECK(reason.empty());
}

TEST_CASE("PackageHost rejects native packages built against another engine") {
	ScopedTempDirectory temp;
	const std::filesystem::path library = temp.Path / "Pkg.Game.Tools.Native.dll";
	Touch(library);
	WriteAbiMetadata(library,
		"schema=1\nboundary=engine_cpp\nengine_sha256=" + std::string(64, 'a') + "\n");

	std::string reason;
	CHECK_FALSE(Index::PackageHostDetail::ValidateNativePackageCompatibility(
		library, std::string(64, 'b'), reason));
	CHECK(reason.find("different Index-Engine") != std::string::npos);
}

TEST_CASE("PackageHost keeps standalone C packages independent from the engine fingerprint") {
	ScopedTempDirectory temp;
	const std::filesystem::path library = temp.Path / "Pkg.Game.Tools.Native.dll";
	Touch(library);
	WriteAbiMetadata(library, "schema=1\nboundary=standalone_c\n");

	std::string reason;
	CHECK(Index::PackageHostDetail::ValidateNativePackageCompatibility(
		library, {}, reason));
	CHECK(reason.empty());
}

TEST_CASE("PackageHost discovers project workspace native outputs") {
	ScopedTempDirectory temp;
	Index::IndexProject project;
	project.RootDirectory = temp.Path.string();
	project.PackagesDirectory = (temp.Path / "Packages").string();
	project.Packages = { "Game.Tools" };

	const std::filesystem::path workspaceLibrary =
		Index::IndexProject::GetProjectNativePackageLibraryPath(
			project.RootDirectory, "Game.Tools", "Release");
	Touch(workspaceLibrary);

	std::vector<std::filesystem::path> candidates;
	Index::PackageHostDetail::AppendProjectNativeCandidates(
		project, "Release", candidates);

	REQUIRE(candidates.size() == 1);
	CHECK(candidates.front().lexically_normal() == workspaceLibrary.lexically_normal());
}

TEST_CASE("PackageHost keeps prebuilt native packages as fallback") {
	ScopedTempDirectory temp;
	Index::IndexProject project;
	project.RootDirectory = temp.Path.string();
	project.PackagesDirectory = (temp.Path / "Packages").string();
	project.Packages = { "Game.Tools" };

	const std::filesystem::path prebuiltLibrary = temp.Path / "Packages" / "Game.Tools"
		/ "Bin" / "Windows-x64" / "Pkg.Game.Tools.Native.dll";
	Touch(prebuiltLibrary);

	std::vector<std::filesystem::path> candidates;
	Index::PackageHostDetail::AppendProjectNativeCandidates(
		project, "Release", candidates);

	REQUIRE(candidates.size() == 1);
	CHECK(candidates.front().lexically_normal() == prebuiltLibrary.lexically_normal());
}

TEST_CASE("PackageHost prioritizes workspace outputs over prebuilt packages") {
	ScopedTempDirectory temp;
	Index::IndexProject project;
	project.RootDirectory = temp.Path.string();
	project.PackagesDirectory = (temp.Path / "Packages").string();
	project.Packages = { "Game.Tools" };

	const std::filesystem::path workspaceLibrary =
		Index::IndexProject::GetProjectNativePackageLibraryPath(
			project.RootDirectory, "Game.Tools", "Release");
	const std::filesystem::path prebuiltLibrary = temp.Path / "Packages" / "Game.Tools"
		/ "Bin" / "Windows-x64" / "Pkg.Game.Tools.Native.dll";
	Touch(workspaceLibrary);
	Touch(prebuiltLibrary);

	std::vector<std::filesystem::path> candidates;
	Index::PackageHostDetail::AppendProjectNativeCandidates(
		project, "Release", candidates);

	REQUIRE(candidates.size() == 2);
	CHECK(candidates[0].lexically_normal() == workspaceLibrary.lexically_normal());
	CHECK(candidates[1].lexically_normal() == prebuiltLibrary.lexically_normal());
}

#ifdef IDX_PLATFORM_WINDOWS
TEST_CASE("PackageHost loads native modules from Unicode paths") {
	ScopedTempDirectory temp;
	wchar_t systemDirectory[MAX_PATH]{};
	REQUIRE(::GetSystemDirectoryW(systemDirectory, MAX_PATH) > 0);

	const std::filesystem::path source =
		std::filesystem::path(systemDirectory) / "version.dll";
	const std::filesystem::path library = temp.Path
		/ std::filesystem::path(L"Projekt-\u00dc") / "Pkg.Game.Tools.Native.dll";
	std::filesystem::create_directories(library.parent_path());
	std::filesystem::copy_file(source, library);

	void* module = Index::PackageHostDetail::LoadNativeModule(library);
	REQUIRE(module != nullptr);
	Index::PackageHostDetail::UnloadNativeModule(module);
}
#endif

namespace {
	struct PackageOwnedTestComponent { int Value = 0; };
	struct PackageDependentTestComponent { int Value = 0; };
	struct EngineOwnedTestComponent { int Value = 0; };
}

TEST_CASE("ComponentRegistry UnregisterOwner drops package registrations and scrubs the conflict graph") {
	using namespace Index;
	constexpr uint32_t k_Owner = 7;
	ComponentRegistry registry;

	ComponentInfo engineInfo("Engine Owned", ComponentCategory::Component);
	engineInfo.serializedName = "EngineOwnedTest";
	registry.Register<EngineOwnedTestComponent>(engineInfo);

	registry.SetRegistrationOwner(k_Owner);
	ComponentInfo packageInfo("Package Owned", ComponentCategory::Component);
	packageInfo.serializedName = "PackageOwnedTest";
	packageInfo.conflictsWith.push_back(typeid(EngineOwnedTestComponent));
	registry.Register<PackageOwnedTestComponent>(packageInfo);
	ComponentInfo dependentInfo("Package Dependent", ComponentCategory::Component);
	dependentInfo.serializedName = "PackageDependentTest";
	dependentInfo.dependsOn.push_back(typeid(PackageOwnedTestComponent));
	registry.Register<PackageDependentTestComponent>(dependentInfo);
	// Re-registering an engine type under a package owner must not transfer ownership.
	registry.Register<EngineOwnedTestComponent>(ComponentInfo("Engine Owned", ComponentCategory::Component));
	registry.SetRegistrationOwner(0);

	registry.ForEachComponentInfo([](const std::type_index& id, ComponentInfo& info) {
		if (id == typeid(EngineOwnedTestComponent)) {
			info.conflictsWith.push_back(typeid(PackageOwnedTestComponent));
			info.dependsOn.push_back(typeid(PackageOwnedTestComponent));
		}
	});

	const ComponentInfo* packageBefore = registry.GetInfo<PackageOwnedTestComponent>();
	REQUIRE(packageBefore != nullptr);
	CHECK(packageBefore->ownerId == k_Owner);
	const uint32_t packageTypeId = packageBefore->typeIdU32;
	const uint32_t dependentTypeId = registry.GetInfo<PackageDependentTestComponent>()->typeIdU32;
	REQUIRE(registry.GetByTypeId(packageTypeId) == packageBefore);
	CHECK(registry.GetInfo<EngineOwnedTestComponent>()->ownerId == 0);
	CHECK(registry.TypesConflict(typeid(EngineOwnedTestComponent), typeid(PackageOwnedTestComponent)));

	CHECK(registry.UnregisterOwner(k_Owner) == 2);

	CHECK(registry.GetInfo<PackageOwnedTestComponent>() == nullptr);
	CHECK(registry.GetInfo<PackageDependentTestComponent>() == nullptr);
	CHECK(registry.FindBySerializedName("PackageOwnedTest") == nullptr);
	CHECK(registry.FindBySerializedName("PackageDependentTest") == nullptr);
	CHECK(registry.GetByTypeId(packageTypeId) == nullptr);
	CHECK(registry.GetByTypeId(dependentTypeId) == nullptr);
	CHECK_FALSE(registry.TypesConflict(typeid(EngineOwnedTestComponent), typeid(PackageOwnedTestComponent)));

	const ComponentInfo* engineAfter = registry.GetInfo<EngineOwnedTestComponent>();
	REQUIRE(engineAfter != nullptr);
	CHECK(engineAfter->conflictsWith.empty());
	CHECK(engineAfter->dependsOn.empty());
	CHECK(registry.FindBySerializedName("EngineOwnedTest") == engineAfter);
	CHECK(registry.GetByTypeId(engineAfter->typeIdU32) == engineAfter);
	CHECK(registry.UnregisterOwner(k_Owner) == 0);
	CHECK(registry.UnregisterOwner(0) == 0);
	CHECK(registry.All().size() == 1);
}
