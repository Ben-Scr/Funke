#include <doctest/doctest.h>

#include "Core/PackageHost.hpp"
#include "Core/Platform.hpp"
#include "Packages/PackageManifest.hpp"
#include "Packages/PackageResolver.hpp"
#include "Project/FunkeProject.hpp"
#include "Scene/ComponentRegistry.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

#ifdef FNK_PLATFORM_WINDOWS
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
				/ ("funke-package-host-tests-" + std::to_string(suffix));
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
			Funke::PackageHostDetail::GetNativePackageAbiMetadataPath(packageLibrary),
			std::ios::binary);
		metadata << contents;
	}
}

TEST_CASE("PackageHost requires native package ABI metadata") {
	ScopedTempDirectory temp;
	const std::filesystem::path library = temp.Path / "Pkg.Game.Tools.Native.dll";
	Touch(library);

	std::string reason;
	CHECK_FALSE(Funke::PackageHostDetail::ValidateNativePackageCompatibility(
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
	CHECK(Funke::PackageHostDetail::ValidateNativePackageCompatibility(
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
	CHECK_FALSE(Funke::PackageHostDetail::ValidateNativePackageCompatibility(
		library, std::string(64, 'b'), reason));
	CHECK(reason.find("different Funke-Engine") != std::string::npos);
}

TEST_CASE("PackageHost keeps standalone C packages independent from the engine fingerprint") {
	ScopedTempDirectory temp;
	const std::filesystem::path library = temp.Path / "Pkg.Game.Tools.Native.dll";
	Touch(library);
	WriteAbiMetadata(library, "schema=1\nboundary=standalone_c\n");

	std::string reason;
	CHECK(Funke::PackageHostDetail::ValidateNativePackageCompatibility(
		library, {}, reason));
	CHECK(reason.empty());
}

TEST_CASE("PackageHost discovers project workspace native outputs") {
	ScopedTempDirectory temp;
	Funke::FunkeProject project;
	project.RootDirectory = temp.Path.string();
	project.PackagesDirectory = (temp.Path / "Packages").string();
	project.Packages = { "Game.Tools" };

	const std::filesystem::path workspaceLibrary =
		Funke::FunkeProject::GetProjectNativePackageLibraryPath(
			project.RootDirectory, "Game.Tools", "Release");
	Touch(workspaceLibrary);

	std::vector<std::filesystem::path> candidates;
	Funke::PackageHostDetail::AppendProjectNativeCandidates(
		project, "Release", candidates);

	REQUIRE(candidates.size() == 1);
	CHECK(candidates.front().lexically_normal() == workspaceLibrary.lexically_normal());
}

TEST_CASE("PackageHost keeps prebuilt native packages as fallback") {
	ScopedTempDirectory temp;
	Funke::FunkeProject project;
	project.RootDirectory = temp.Path.string();
	project.PackagesDirectory = (temp.Path / "Packages").string();
	project.Packages = { "Game.Tools" };

	const std::filesystem::path prebuiltLibrary = temp.Path / "Packages" / "Game.Tools"
		/ "Bin" / (std::string(FNK_PLATFORM_NAME) + "-" + FNK_ARCH_NAME)
		/ Funke::Platform::SharedLibraryFileName("Pkg.Game.Tools.Native");
	Touch(prebuiltLibrary);

	std::vector<std::filesystem::path> candidates;
	Funke::PackageHostDetail::AppendProjectNativeCandidates(
		project, "Release", candidates);

	REQUIRE(candidates.size() == 1);
	CHECK(candidates.front().lexically_normal() == prebuiltLibrary.lexically_normal());
}

TEST_CASE("PackageHost prioritizes workspace outputs over prebuilt packages") {
	ScopedTempDirectory temp;
	Funke::FunkeProject project;
	project.RootDirectory = temp.Path.string();
	project.PackagesDirectory = (temp.Path / "Packages").string();
	project.Packages = { "Game.Tools" };

	const std::filesystem::path workspaceLibrary =
		Funke::FunkeProject::GetProjectNativePackageLibraryPath(
			project.RootDirectory, "Game.Tools", "Release");
	const std::filesystem::path prebuiltLibrary = temp.Path / "Packages" / "Game.Tools"
		/ "Bin" / (std::string(FNK_PLATFORM_NAME) + "-" + FNK_ARCH_NAME)
		/ Funke::Platform::SharedLibraryFileName("Pkg.Game.Tools.Native");
	Touch(workspaceLibrary);
	Touch(prebuiltLibrary);

	std::vector<std::filesystem::path> candidates;
	Funke::PackageHostDetail::AppendProjectNativeCandidates(
		project, "Release", candidates);

	REQUIRE(candidates.size() == 2);
	CHECK(candidates[0].lexically_normal() == workspaceLibrary.lexically_normal());
	CHECK(candidates[1].lexically_normal() == prebuiltLibrary.lexically_normal());
}

namespace {
	void WriteManifest(const std::filesystem::path& packagesDir, const std::string& name, const std::string& layers) {
		std::filesystem::create_directories(packagesDir / name);
		std::ofstream(packagesDir / name / "funke-package.lua", std::ios::binary)
			<< "return { name = \"" << name << "\", version = \"1.0.0\", layers = { " << layers << " } }\n";
	}

	Funke::FunkeProject MakeSourcePackageProject(const std::filesystem::path& root) {
		Funke::FunkeProject project;
		project.RootDirectory = root.string();
		project.PackagesDirectory = (root / "Packages").string();
		project.Packages = { "Game.Tools" };
		WriteManifest(root / "Packages", "Game.Tools", "native = { sources = { \"src/**.cpp\" } }");
		return project;
	}

	std::filesystem::path BuildWorkspaceLibrary(const Funke::FunkeProject& project, std::string_view engineSha256) {
		const std::filesystem::path library = Funke::FunkeProject::GetProjectNativePackageLibraryPath(
			project.RootDirectory, "Game.Tools", "Release");
		Touch(library);
		WriteAbiMetadata(library, "schema=1\nboundary=engine_cpp\nengine_sha256=" + std::string(engineSha256) + "\n");
		return library;
	}
}

TEST_CASE("PackageHost reports never-built project source packages as stale") {
	ScopedTempDirectory temp;
	const Funke::FunkeProject project = MakeSourcePackageProject(temp.Path);

	CHECK(Funke::PackageHostDetail::FindStaleProjectNativePackages(project, "Release", std::string(64, 'a'))
		== std::vector<std::string>{ "Game.Tools" });
	CHECK(Funke::PackageHostDetail::FindStaleProjectNativePackages(project, "Release", {})
		== std::vector<std::string>{ "Game.Tools" });
}

TEST_CASE("PackageHost uses project manifests before bundled engine packages") {
	ScopedTempDirectory temp;
	const auto enginePackages = temp.Path / "EngineSDK" / "packages";
	const Funke::FunkeProject project = MakeSourcePackageProject(temp.Path / "Project");
	WriteManifest(enginePackages, "Game.Tools", "csharp = { sources = { \"Source/**.cs\" } }");
	WriteManifest(enginePackages, "Game.Fallback", "csharp = { sources = { \"Source/**.cs\" } }");
	const auto manifests = Funke::PackageHostDetail::DiscoverPackageManifests(project, enginePackages);
	REQUIRE(manifests.size() == 2);
	CHECK(manifests.front().Name == "Game.Tools");
	CHECK(manifests.front().HasNativeLayer);
	CHECK_FALSE(manifests.front().HasCSharpLayer);
	CHECK(manifests.back().Name == "Game.Fallback");
	const auto resolution = Funke::ResolvePackages(manifests, { "Game.Tools", "Game.Fallback" }, Funke::GetEngineCompatibilityVersion());
	CHECK(resolution.Issues.empty());
}

TEST_CASE("PackageHost retains duplicate names inside one package root for diagnostics") {
	ScopedTempDirectory temp;
	const Funke::FunkeProject project = MakeSourcePackageProject(temp.Path);
	const auto duplicate = temp.Path / "Packages" / "Game.Duplicate";
	std::filesystem::create_directories(duplicate);
	std::filesystem::copy_file(temp.Path / "Packages" / "Game.Tools" / "funke-package.lua",
		duplicate / "funke-package.lua");
	const auto manifests = Funke::PackageHostDetail::DiscoverPackageManifests(project, {});
	REQUIRE(manifests.size() == 2);
	const auto resolution = Funke::ResolvePackages(manifests, { "Game.Tools" }, Funke::GetEngineCompatibilityVersion());
	CHECK_FALSE(resolution.GetMessages("Game.Tools").empty());
}

TEST_CASE("PackageHost reports project packages built against another engine as stale") {
	ScopedTempDirectory temp;
	const Funke::FunkeProject project = MakeSourcePackageProject(temp.Path);
	BuildWorkspaceLibrary(project, std::string(64, 'a'));

	CHECK(Funke::PackageHostDetail::FindStaleProjectNativePackages(project, "Release", std::string(64, 'b'))
		== std::vector<std::string>{ "Game.Tools" });
	CHECK(Funke::PackageHostDetail::FindStaleProjectNativePackages(project, "Release", std::string(64, 'a')).empty());
	// Without a running fingerprint only missing builds count.
	CHECK(Funke::PackageHostDetail::FindStaleProjectNativePackages(project, "Release", {}).empty());
}

TEST_CASE("PackageHost does not rebuild packages that have a loadable prebuilt fallback") {
	ScopedTempDirectory temp;
	const Funke::FunkeProject project = MakeSourcePackageProject(temp.Path);
	BuildWorkspaceLibrary(project, std::string(64, 'a'));
	const std::filesystem::path prebuiltLibrary = temp.Path / "Packages" / "Game.Tools"
		/ "Bin" / (std::string(FNK_PLATFORM_NAME) + "-" + FNK_ARCH_NAME)
		/ Funke::Platform::SharedLibraryFileName("Pkg.Game.Tools.Native");
	Touch(prebuiltLibrary);
	WriteAbiMetadata(prebuiltLibrary, "schema=1\nboundary=engine_cpp\nengine_sha256=" + std::string(64, 'b') + "\n");

	CHECK(Funke::PackageHostDetail::FindStaleProjectNativePackages(project, "Release", std::string(64, 'b')).empty());
}

TEST_CASE("PackageHost ignores unlisted and managed-only packages when looking for stale builds") {
	ScopedTempDirectory temp;
	Funke::FunkeProject project = MakeSourcePackageProject(temp.Path);
	WriteManifest(temp.Path / "Packages", "Game.Scripts", "csharp = { sources = { \"csharp/**.cs\" } }");
	project.Packages = { "Game.Scripts" };

	CHECK(Funke::PackageHostDetail::FindStaleProjectNativePackages(project, "Release", std::string(64, 'a')).empty());
}

#ifdef FNK_PLATFORM_WINDOWS
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

	void* module = Funke::PackageHostDetail::LoadNativeModule(library);
	REQUIRE(module != nullptr);
	Funke::PackageHostDetail::UnloadNativeModule(module);
}
#endif

namespace {
	struct PackageOwnedTestComponent { int Value = 0; };
	struct PackageDependentTestComponent { int Value = 0; };
	struct EngineOwnedTestComponent { int Value = 0; };
}

TEST_CASE("ComponentRegistry UnregisterOwner drops package registrations and scrubs the conflict graph") {
	using namespace Funke;
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
