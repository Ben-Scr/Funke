#include <doctest/doctest.h>

#include "Editor/FunkePackageInstaller.hpp"
#include "Project/FunkeProject.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>

namespace {
	class ScopedPackageProject {
	public:
		ScopedPackageProject() {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Root = std::filesystem::temp_directory_path()
				/ ("funke-package-installer-tests-" + std::to_string(suffix));
			std::filesystem::create_directories(Root / "Packages");
		}

		~ScopedPackageProject() {
			std::error_code ec;
			std::filesystem::remove_all(Root, ec);
		}

		void Write(std::filesystem::path relativePath, std::string_view content) const {
			const std::filesystem::path path = Root / std::move(relativePath);
			std::filesystem::create_directories(path.parent_path());
			std::ofstream stream(path, std::ios::binary);
			stream << content;
		}

		std::string Read(std::filesystem::path relativePath) const {
			std::ifstream stream(Root / std::move(relativePath), std::ios::binary);
			std::ostringstream content;
			content << stream.rdbuf();
			return content.str();
		}

		std::filesystem::path Root;
	};

	size_t CountOccurrences(std::string_view text, std::string_view needle) {
		size_t count = 0;
		for (size_t offset = 0; (offset = text.find(needle, offset)) != std::string_view::npos;
			offset += needle.size()) {
			++count;
		}
		return count;
	}
}

TEST_CASE("Package reference props prefer isolated workspace outputs") {
	ScopedPackageProject fixture;
	fixture.Write("Packages/Game.Managed/funke-package.lua", R"(
return {
    name = "Game.Managed",
    version = "1.0.0",
    layers = {
        csharp = { sources = { "Source/**.cs" } },
    },
}
)");
	fixture.Write("Packages/Game.NativeOnly/funke-package.lua", R"(
return {
    name = "Game.NativeOnly",
    version = "1.0.0",
    layers = {
        native = { sources = { "Source/**.cpp" } },
    },
}
)");
	fixture.Write("Demo.csproj", "<Project>\n</Project>\n");

	Funke::FunkeProject project;
	project.RootDirectory = fixture.Root.string();
	project.PackagesDirectory = (fixture.Root / "Packages").string();
	project.CsprojPath = (fixture.Root / "Demo.csproj").string();
	project.Packages = { "Game.Managed", "Game.NativeOnly" };

	Funke::FunkePackageInstaller::RegeneratePackageReferences(project);
	Funke::FunkePackageInstaller::RegeneratePackageReferences(project);

	const std::string props = fixture.Read("Packages/FunkePackages.props");
	const std::string workspaceAssembly =
		"$(MSBuildThisFileDirectory)../.funke/build/packages/bin/$(Configuration)-$(FunkePackageSystem)-$(FunkePackageArch)/Pkg.Game.Managed/Pkg.Game.Managed.dll";
	const std::string prebuiltAssembly =
		"$(MSBuildThisFileDirectory)Game.Managed/Bin/$(FunkePackagePlatform)-$(FunkePackageBinArch)/Pkg.Game.Managed.dll";
	const std::string portableAssembly =
		"$(MSBuildThisFileDirectory)Game.Managed/Bin/Windows-x64/Pkg.Game.Managed.dll";

	CHECK(props.find("<Reference Include=\"Pkg.Game.Managed\"") != std::string::npos);
	CHECK(props.find("$([MSBuild]::IsOSPlatform('OSX'))") != std::string::npos);
	CHECK(props.find(workspaceAssembly) != std::string::npos);
	CHECK(props.find(prebuiltAssembly) != std::string::npos);
	// Later HintPaths win: workspace build, then this host's prebuilt, then the CPU-neutral Windows one.
	CHECK(props.find("<HintPath>" + portableAssembly + "</HintPath>")
		< props.find(">" + prebuiltAssembly + "</HintPath>"));
	CHECK(props.find(">" + prebuiltAssembly + "</HintPath>")
		< props.find(">" + workspaceAssembly + "</HintPath>"));
	CHECK(props.find("Pkg.Game.NativeOnly") == std::string::npos);

	const std::string csproj = fixture.Read("Demo.csproj");
	CHECK(CountOccurrences(csproj, "Packages/FunkePackages.props") == 2);
}

TEST_CASE("Managed-only packages never roll back for missing PackageHost modules") {
	Funke::FunkePackageManifest managed;
	managed.HasCSharpLayer = true;
	CHECK_FALSE(Funke::FunkePackageInstaller::RequiresNativeRuntimeLoad(managed));
	CHECK_FALSE(Funke::FunkePackageInstaller::ShouldRollbackAfterLoad(managed, false));

	Funke::FunkePackageManifest native;
	native.HasNativeLayer = true;
	CHECK(Funke::FunkePackageInstaller::RequiresNativeRuntimeLoad(native));
	CHECK(Funke::FunkePackageInstaller::ShouldRollbackAfterLoad(native, false));
	CHECK_FALSE(Funke::FunkePackageInstaller::ShouldRollbackAfterLoad(native, true));

	Funke::FunkePackageManifest standalone;
	standalone.HasNativeStandaloneLayer = true;
	CHECK(Funke::FunkePackageInstaller::RequiresNativeRuntimeLoad(standalone));
	CHECK(Funke::FunkePackageInstaller::ShouldRollbackAfterLoad(standalone, false));
}

TEST_CASE("Installing a package enables the engine modules it needs, and they can't be removed under it") {
	ScopedPackageProject fixture;
	fixture.Write("Packages/Game.Tiles2D/funke-package.lua", R"(
return { name = "Game.Tiles2D", version = "1.0.0", modules = { "Funke.2D" } }
)");
	fixture.Write("Packages/Game.Levels/funke-package.lua", R"(
return { name = "Game.Levels", version = "1.0.0", dependencies = { "Game.Tiles2D" } }
)");
	fixture.Write("Demo.csproj", "<Project>\n</Project>\n");

	Funke::FunkeProject project;
	project.Name = "Demo";
	project.RootDirectory = fixture.Root.string();
	project.PackagesDirectory = (fixture.Root / "Packages").string();
	project.CsprojPath = (fixture.Root / "Demo.csproj").string();
	project.ProjectFilePath = (fixture.Root / "project.json").string();
	project.Modules = { "Funke.3D" };
	project.Packages = { "Game.Tiles2D" };

	// Transitive: Game.Levels itself declares no module, its dependency does.
	const auto installed = Funke::FunkePackageInstaller::InstallToProject(project, "Game.Levels");
	REQUIRE_MESSAGE(installed.Success, installed.Message);
	REQUIRE(installed.EnabledModules.size() == 1);
	CHECK(installed.EnabledModules[0] == "Funke.2D");
	CHECK(project.HasModule("Funke.2D"));
	CHECK(fixture.Read("project.json").find("Funke.2D") != std::string::npos);

	const auto refused = Funke::FunkePackageInstaller::DisableModule(project, "Funke.2D");
	CHECK_FALSE(refused.Success);
	CHECK(refused.Message.find("Game.Levels") != std::string::npos);
	CHECK(project.HasModule("Funke.2D"));

	CHECK_FALSE(Funke::FunkePackageInstaller::EnableModule(project, "Funke.4D").Success);
	CHECK(Funke::FunkePackageInstaller::DisableModule(project, "Funke.3D").Success);
	CHECK_FALSE(project.HasModule("Funke.3D"));
	CHECK(Funke::FunkePackageInstaller::EnableModule(project, "Funke.3D").Success);
	CHECK(project.HasModule("Funke.3D"));
}

TEST_CASE("Packages of a project stay project packages when the tools run inside the project") {
	ScopedPackageProject fixture;
	fixture.Write("Packages/Game.Ads/funke-package.lua", R"(
return {
    name = "Game.Ads",
    version = "1.0.0",
    layers = { csharp = { sources = { "Source/**.cs" } } },
    android = {
        maven = { "com.example:lib:1.0.0" },
        placeholders = { appId = "test" },
    },
}
)");
	const std::filesystem::path previous = std::filesystem::current_path();
	std::filesystem::current_path(fixture.Root);
	const auto manifests = Funke::FunkePackageInstaller::EnumerateAll(fixture.Root.generic_string() + "/");
	std::filesystem::current_path(previous);

	const auto found = std::find_if(manifests.begin(), manifests.end(),
		[](const Funke::FunkePackageManifest& manifest) { return manifest.Name == "Game.Ads"; });
	REQUIRE(found != manifests.end());
	CHECK_FALSE(found->IsEngine);
	CHECK(found->HasCSharpLayer);
	CHECK(std::count_if(manifests.begin(), manifests.end(),
		[](const Funke::FunkePackageManifest& manifest) { return manifest.Name == "Game.Ads"; }) == 1);
}
