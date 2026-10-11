#include <doctest/doctest.h>

#include "Project/IndexProject.hpp"

#include <filesystem>
#include <string_view>

TEST_CASE("Project package output paths are deterministic and traversal-safe") {
	const std::string root = (std::filesystem::path("FixtureProject") / "Root").string();
	const std::filesystem::path output =
		Index::IndexProject::GetProjectPackageOutputDirectory(
			root, "Pkg.Game.Tools", "Dist");

	REQUIRE_FALSE(output.empty());
	CHECK(output.filename() == "Pkg.Game.Tools");
#if defined(IDX_PLATFORM_WINDOWS)
	CHECK(output.parent_path().filename() == "Dist-windows-x86_64");
#elif defined(IDX_PLATFORM_LINUX)
	CHECK(output.parent_path().filename() == "Dist-linux-x86_64");
#endif

	for (const std::string_view invalidTarget : {
		"", ".", "..", ".Pkg.Game", "Pkg.Game.", "Pkg..Game",
		"../Pkg.Game", "Pkg/Game", "Pkg\\Game", "Pkg Game" }) {
		CHECK(Index::IndexProject::GetProjectPackageOutputDirectory(
			root, std::string(invalidTarget), "Release").empty());
	}
	CHECK(Index::IndexProject::GetProjectPackageOutputDirectory(
		root, "Pkg.Game.Tools", "../Release").empty());
}

TEST_CASE("Project native package paths use platform library names") {
	const std::filesystem::path library =
		Index::IndexProject::GetProjectNativePackageLibraryPath(
			"FixtureProject", "Game.Tools", "Release");

#if defined(IDX_PLATFORM_WINDOWS)
	CHECK(library.filename() == "Pkg.Game.Tools.Native.dll");
#elif defined(__APPLE__)
	CHECK(library.filename() == "libPkg.Game.Tools.Native.dylib");
#else
	CHECK(library.filename() == "libPkg.Game.Tools.Native.so");
#endif

	CHECK(Index::IndexProject::GetProjectNativePackageLibraryPath(
		"FixtureProject", "../Game.Tools", "Release").empty());
}
