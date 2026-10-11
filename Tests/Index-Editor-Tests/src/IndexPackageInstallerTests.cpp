#include <doctest/doctest.h>

#include "Editor/IndexPackageInstaller.hpp"
#include "Project/IndexProject.hpp"

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
				/ ("index-package-installer-tests-" + std::to_string(suffix));
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
	fixture.Write("Packages/Game.Managed/index-package.lua", R"(
return {
    name = "Game.Managed",
    version = "1.0.0",
    layers = {
        csharp = { sources = { "Source/**.cs" } },
    },
}
)");
	fixture.Write("Packages/Game.NativeOnly/index-package.lua", R"(
return {
    name = "Game.NativeOnly",
    version = "1.0.0",
    layers = {
        native = { sources = { "Source/**.cpp" } },
    },
}
)");
	fixture.Write("Demo.csproj", "<Project>\n</Project>\n");

	Index::IndexProject project;
	project.RootDirectory = fixture.Root.string();
	project.PackagesDirectory = (fixture.Root / "Packages").string();
	project.CsprojPath = (fixture.Root / "Demo.csproj").string();
	project.Packages = { "Game.Managed", "Game.NativeOnly" };

	Index::IndexPackageInstaller::RegeneratePackageReferences(project);
	Index::IndexPackageInstaller::RegeneratePackageReferences(project);

	const std::string props = fixture.Read("Packages/IndexPackages.props");
	const std::string workspaceAssembly =
		"$(MSBuildThisFileDirectory)..\\.index\\build\\packages\\bin\\$(Configuration)-windows-x86_64\\Pkg.Game.Managed\\Pkg.Game.Managed.dll";
	const std::string prebuiltAssembly =
		"$(MSBuildThisFileDirectory)Game.Managed\\Bin\\Windows-x64\\Pkg.Game.Managed.dll";

	CHECK(props.find("<Reference Include=\"Pkg.Game.Managed\"") != std::string::npos);
	CHECK(props.find(workspaceAssembly) != std::string::npos);
	CHECK(props.find(prebuiltAssembly) != std::string::npos);
	CHECK(props.find("<HintPath>" + prebuiltAssembly + "</HintPath>")
		< props.find(">" + workspaceAssembly + "</HintPath>"));
	CHECK(props.find("Pkg.Game.NativeOnly") == std::string::npos);

	const std::string csproj = fixture.Read("Demo.csproj");
	CHECK(CountOccurrences(csproj, "Packages/IndexPackages.props") == 2);
}

TEST_CASE("Managed-only packages never roll back for missing PackageHost modules") {
	Index::IndexPackageManifest managed;
	managed.HasCSharpLayer = true;
	CHECK_FALSE(Index::IndexPackageInstaller::RequiresNativeRuntimeLoad(managed));
	CHECK_FALSE(Index::IndexPackageInstaller::ShouldRollbackAfterLoad(managed, false));

	Index::IndexPackageManifest native;
	native.HasNativeLayer = true;
	CHECK(Index::IndexPackageInstaller::RequiresNativeRuntimeLoad(native));
	CHECK(Index::IndexPackageInstaller::ShouldRollbackAfterLoad(native, false));
	CHECK_FALSE(Index::IndexPackageInstaller::ShouldRollbackAfterLoad(native, true));

	Index::IndexPackageManifest standalone;
	standalone.HasNativeStandaloneLayer = true;
	CHECK(Index::IndexPackageInstaller::RequiresNativeRuntimeLoad(standalone));
	CHECK(Index::IndexPackageInstaller::ShouldRollbackAfterLoad(standalone, false));
}
