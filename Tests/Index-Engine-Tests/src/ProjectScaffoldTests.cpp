#include <doctest/doctest.h>

#include "Project/IndexProject.hpp"
#include "Serialization/Path.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>

using namespace Index;

// INDEX_TEST_KEEP_PROJECT_DIR=<dir> keeps the scaffold there (used to produce a starter project for
// platform export smoke tests); otherwise it goes to a temp folder and is removed.
TEST_CASE("New project scaffold contains the starter scene, scripts and C# project") {
	const char* keepDir = std::getenv("INDEX_TEST_KEEP_PROJECT_DIR");
	const std::filesystem::path parent = keepDir && *keepDir
		? std::filesystem::path(keepDir)
		: std::filesystem::temp_directory_path() / ("index-scaffold-" + std::to_string(
			std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(parent);

	const IndexProject project = IndexProject::Create("StarterGame", Path::ToUtf8(parent));

	CHECK(std::filesystem::is_regular_file(Path::FromUtf8(project.ProjectFilePath)));
	CHECK(std::filesystem::is_regular_file(Path::FromUtf8(project.GetSceneFilePath(project.StartupScene))));
	CHECK(std::filesystem::is_regular_file(Path::FromUtf8(project.CsprojPath)));
	CHECK(std::filesystem::is_regular_file(Path::FromUtf8(project.ScriptsDirectory) / "GameScript.cs"));

	if (!keepDir || !*keepDir) {
		std::error_code ec;
		std::filesystem::remove_all(parent, ec);
	}
}
