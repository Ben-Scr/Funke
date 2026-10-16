#include <doctest/doctest.h>

#include "Project/FunkeProject.hpp"

#include <filesystem>
#include <string_view>

TEST_CASE("Project package output paths are deterministic and traversal-safe") {
	const std::string root = (std::filesystem::path("FixtureProject") / "Root").string();
	const std::filesystem::path output =
		Funke::FunkeProject::GetProjectPackageOutputDirectory(
			root, "Pkg.Game.Tools", "Dist");

	REQUIRE_FALSE(output.empty());
	CHECK(output.filename() == "Pkg.Game.Tools");
#if defined(FNK_PLATFORM_WINDOWS)
	CHECK(output.parent_path().filename() == "Dist-windows-x86_64");
#elif defined(FNK_PLATFORM_LINUX) && (defined(__x86_64__) || defined(_M_X64))
	CHECK(output.parent_path().filename() == "Dist-linux-x86_64");
#elif defined(FNK_PLATFORM_MACOS) && defined(__aarch64__)
	CHECK(output.parent_path().filename() == "Dist-macosx-arm64");
#endif

	for (const std::string_view invalidTarget : {
		"", ".", "..", ".Pkg.Game", "Pkg.Game.", "Pkg..Game",
		"../Pkg.Game", "Pkg/Game", "Pkg\\Game", "Pkg Game" }) {
		CHECK(Funke::FunkeProject::GetProjectPackageOutputDirectory(
			root, std::string(invalidTarget), "Release").empty());
	}
	CHECK(Funke::FunkeProject::GetProjectPackageOutputDirectory(
		root, "Pkg.Game.Tools", "../Release").empty());
}

TEST_CASE("Project native package paths use platform library names") {
	const std::filesystem::path library =
		Funke::FunkeProject::GetProjectNativePackageLibraryPath(
			"FixtureProject", "Game.Tools", "Release");

#if defined(FNK_PLATFORM_WINDOWS)
	CHECK(library.filename() == "Pkg.Game.Tools.Native.dll");
#elif defined(__APPLE__)
	CHECK(library.filename() == "libPkg.Game.Tools.Native.dylib");
#else
	CHECK(library.filename() == "libPkg.Game.Tools.Native.so");
#endif

	CHECK(Funke::FunkeProject::GetProjectNativePackageLibraryPath(
		"FixtureProject", "../Game.Tools", "Release").empty());
}
