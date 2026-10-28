#include <doctest/doctest.h>

#include "Project/FunkeProject.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>

using namespace Funke;

namespace {
	struct NativeScaffoldDirectory {
		std::filesystem::path Root = std::filesystem::temp_directory_path() / ("funke-native-scaffold-" + std::to_string(
			std::chrono::steady_clock::now().time_since_epoch().count()));
		FunkeProject Project;

		NativeScaffoldDirectory() {
			Project.Name = "NativeTest";
			Project.RootDirectory = Path::ToUtf8(Root);
			Project.AssetsDirectory = Path::ToUtf8(Root / "Assets");
			Project.NativeScriptsDir = Path::ToUtf8(Root / "NativeScripts");
			Project.NativeSourceDir = Path::ToUtf8(Root / "NativeScripts" / "Source");
			std::filesystem::create_directories(Root / "Assets" / "Scripts");
			std::filesystem::create_directories(Root / "NativeScripts" / "Source");
		}

		~NativeScaffoldDirectory() {
			std::error_code ec;
			std::filesystem::remove_all(Root, ec);
		}

		void Write(const std::filesystem::path& relative, const std::string& contents) const {
			std::filesystem::create_directories((Root / relative).parent_path());
			REQUIRE(File::WriteAllText(Path::ToUtf8(Root / relative), contents));
		}

		std::string Read(const std::filesystem::path& relative) const {
			return File::ReadAllText(Path::ToUtf8(Root / relative));
		}

		void WriteLegacyProject() const {
			const auto engineRoot = Path::FromUtf8(FunkeProject::GetEngineRootDir());
			REQUIRE_FALSE(engineRoot.empty());
			std::string legacy = File::ReadAllText(Path::ToUtf8(engineRoot / "Tests" / "Fixtures" / "NativeScripts" / "LegacyCMakeLists.txt"));
			REQUIRE_FALSE(legacy.empty());
			const std::string nameToken = "@PROJECT_NAME@";
			legacy.replace(legacy.find(nameToken), nameToken.size(), Project.Name);
			const std::string rootToken = "@ENGINE_ROOT_RELATIVE@";
			legacy.replace(legacy.find(rootToken), rootToken.size(), Path::ToGenericUtf8(
				std::filesystem::relative(engineRoot, Root / "NativeScripts")));
			Write("NativeScripts/CMakeLists.txt", legacy);
		}
	};
}

TEST_CASE("Native script scaffold compiles asset sources with matching engine ABI settings") {
	NativeScaffoldDirectory directory;
	directory.Project.EnsureNativeScriptProjectFiles();
	CHECK_FALSE(directory.Project.HasNativeScriptSources());

	directory.Write("Assets/Scripts/Player.cc", "REGISTER_SCRIPT(Player)\n");
	CHECK(directory.Project.HasNativeScriptSources());
	const std::string cmake = directory.Read("NativeScripts/FunkeNativeScripts.cmake");
	CHECK(cmake.find("../Assets/*.cc") != std::string::npos);
	CHECK(cmake.find("../Assets/*.cxx") != std::string::npos);
	CHECK(cmake.find("Source/*.cpp") != std::string::npos);
	CHECK(cmake.find("FNK_IMPORT_DLL") != std::string::npos);
	CHECK(cmake.find("Funke-Engine/src/Generated") != std::string::npos);
	CHECK(cmake.find("MSVC_RUNTIME_LIBRARY") != std::string::npos);
	CHECK(cmake.find("FUNKE_ENGINE_LIBRARY_DIR") != std::string::npos);
	CHECK(cmake.find("FUNKE_NATIVE_OUTPUT_ROOT") != std::string::npos);
	CHECK(directory.Read("NativeScripts/Source/NativeScriptExports.cpp").find("FunkeNativeScriptAbiVersion") != std::string::npos);
	CHECK(directory.Read("NativeScripts/CMakeLists.txt").find("LANGUAGES C CXX") != std::string::npos);

	std::filesystem::remove(directory.Root / "Assets" / "Scripts" / "Player.cc");
	directory.Write("Assets/Scripts/Player.hpp", "// Only a header.\n");
	CHECK_FALSE(directory.Project.HasNativeScriptSources());
	directory.Write("NativeScripts/Source/Manual.cxx", "REGISTER_SCRIPT(Manual)\n");
	CHECK(directory.Project.HasNativeScriptSources());
}

TEST_CASE("Native script scaffold preserves user CMake and avoids unchanged watcher writes") {
	NativeScaffoldDirectory directory;
	directory.Project.EnsureNativeScriptProjectFiles();
	const auto module = directory.Root / "NativeScripts" / "FunkeNativeScripts.cmake";
	const auto exports = directory.Root / "NativeScripts" / "Source" / "NativeScriptExports.cpp";
	const auto moduleTime = std::filesystem::last_write_time(module);
	const auto exportsTime = std::filesystem::last_write_time(exports);
	const std::string custom = directory.Read("NativeScripts/CMakeLists.txt") + "target_compile_definitions(${PROJECT_NAME} PRIVATE CUSTOM_FEATURE=1)\n";
	directory.Write("NativeScripts/CMakeLists.txt", custom);
	directory.Project.EnsureNativeScriptProjectFiles();
	CHECK(directory.Read("NativeScripts/CMakeLists.txt") == custom);
	CHECK(std::filesystem::last_write_time(module) == moduleTime);
	CHECK(std::filesystem::last_write_time(exports) == exportsTime);

	directory.Project.CustomDefines.push_back("GAME_FEATURE");
	directory.Project.EnsureNativeScriptProjectFiles();
	CHECK(directory.Read("NativeScripts/FunkeNativeScripts.cmake").find("GAME_FEATURE") != std::string::npos);
	CHECK(directory.Read("NativeScripts/CMakeLists.txt") == custom);
	directory.Project.Name = "Renamed Game";
	directory.Project.EnsureNativeScriptProjectFiles();
	CHECK(directory.Read("NativeScripts/CMakeLists.txt").find("project(Renamed_Game-NativeScripts LANGUAGES C CXX)") != std::string::npos);
	CHECK(directory.Read("NativeScripts/CMakeLists.txt").find("CUSTOM_FEATURE=1") != std::string::npos);
}

TEST_CASE("Native script targets support project names containing spaces and punctuation") {
	NativeScaffoldDirectory directory;
	directory.Project.Name = "My Game (Test);$[1].+";
	CHECK(FunkeProject::IsValidProjectName(directory.Project.Name));
	CHECK(directory.Project.GetNativeScriptTargetName() == "My_Game__Test____1_.+-NativeScripts");
	directory.Project.EnsureNativeScriptProjectFiles();
	const auto target = directory.Project.GetNativeScriptTargetName();
	CHECK(directory.Read("NativeScripts/CMakeLists.txt").find("project(" + target + " LANGUAGES C CXX)") != std::string::npos);
	CHECK(directory.Project.GetNativeDllPath().find(target) != std::string::npos);
}

TEST_CASE("Native script DLL lookup does not fall back across build configurations") {
	NativeScaffoldDirectory directory;
	const std::string current = FunkeProject::GetActiveBuildConfiguration();
	const std::string other = current == "Debug" ? "Release" : "Debug";
	const auto expected = Path::FromUtf8(directory.Project.GetNativeDllPath());
	const auto filename = expected.filename();
	directory.Write(std::filesystem::path("NativeScripts") / "build" / other / filename, "different runtime ABI");
	CHECK(Path::FromUtf8(directory.Project.GetNativeDllPath()) == expected);
	CHECK(Path::FromUtf8(directory.Project.GetNativeDllPath(current)) == expected);
	directory.Write(std::filesystem::path("NativeScripts") / "build" / current / filename, "current runtime ABI");
	CHECK(Path::FromUtf8(directory.Project.GetNativeDllPath()).parent_path().filename() == current);
}

TEST_CASE("Native script legacy migration preserves both versions and uses the newer edited source") {
	NativeScaffoldDirectory directory;
	directory.WriteLegacyProject();
	const std::string oldAsset = "class Player {}; // Old asset\nREGISTER_SCRIPT(Player)\n";
	const std::string editedMirror = "class Player {}; // Edited in external IDE\nREGISTER_SCRIPT(Player)\n";
	directory.Write("Assets/Scripts/Player.cpp", oldAsset);
	directory.Write("NativeScripts/Source/Player.cpp", editedMirror);
	std::filesystem::last_write_time(directory.Root / "NativeScripts" / "Source" / "Player.cpp",
		std::filesystem::last_write_time(directory.Root / "Assets" / "Scripts" / "Player.cpp") + std::chrono::seconds(2));
	directory.Project.EnsureNativeScriptProjectFiles();
	CHECK(directory.Read("Assets/Scripts/Player.cpp") == editedMirror);
	CHECK_FALSE(std::filesystem::exists(directory.Root / "NativeScripts" / "Source" / "Player.cpp"));
	CHECK(directory.Read("NativeScripts/LegacySourceBackup/Player.cpp/Asset.original") == oldAsset);
	CHECK(directory.Read("NativeScripts/LegacySourceBackup/Player.cpp/Source.original") == editedMirror);
	CHECK(directory.Read("NativeScripts/CMakeLists.txt").find("FunkeNativeScripts.cmake") != std::string::npos);

	std::filesystem::rename(directory.Root / "Assets" / "Scripts" / "Player.cpp", directory.Root / "Assets" / "Scripts" / "Renamed.cpp");
	directory.Project.EnsureNativeScriptProjectFiles();
	CHECK(directory.Project.HasNativeScriptSources());
	std::filesystem::remove(directory.Root / "Assets" / "Scripts" / "Renamed.cpp");
	directory.Project.EnsureNativeScriptProjectFiles();
	CHECK_FALSE(directory.Project.HasNativeScriptSources());
}

TEST_CASE("Native script legacy migration leaves distinct and ambiguous source files intact") {
	NativeScaffoldDirectory directory;
	directory.WriteLegacyProject();
	directory.Write("Assets/Scripts/Different.cpp", "// REGISTER_SCRIPT(Old)\nREGISTER_SCRIPT(New)\n");
	directory.Write("NativeScripts/Source/Different.cpp", "REGISTER_SCRIPT(Old)\n");
	directory.Write("Assets/First/Ambiguous.cpp", "REGISTER_SCRIPT(Ambiguous)\n");
	directory.Write("Assets/Second/Ambiguous.cpp", "REGISTER_SCRIPT(Ambiguous)\n");
	directory.Write("NativeScripts/Source/Ambiguous.cpp", "REGISTER_SCRIPT(Ambiguous)\n");
	directory.Project.EnsureNativeScriptProjectFiles();
	CHECK(directory.Read("NativeScripts/Source/Different.cpp") == "REGISTER_SCRIPT(Old)\n");
	CHECK(directory.Read("Assets/Scripts/Different.cpp") == "// REGISTER_SCRIPT(Old)\nREGISTER_SCRIPT(New)\n");
	CHECK(std::filesystem::exists(directory.Root / "NativeScripts" / "Source" / "Ambiguous.cpp"));
}

// FUNKE_TEST_KEEP_PROJECT_DIR=<dir> keeps the scaffold there (used to produce a starter project for
// platform export smoke tests); otherwise it goes to a temp folder and is removed.
TEST_CASE("New project scaffold contains the starter scene, scripts and C# project") {
	const char* keepDir = std::getenv("FUNKE_TEST_KEEP_PROJECT_DIR");
	const std::filesystem::path parent = keepDir && *keepDir
		? std::filesystem::path(keepDir)
		: std::filesystem::temp_directory_path() / ("funke-scaffold-" + std::to_string(
			std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(parent);

	const FunkeProject project = FunkeProject::Create("StarterGame", Path::ToUtf8(parent));

	CHECK(std::filesystem::is_regular_file(Path::FromUtf8(project.ProjectFilePath)));
	CHECK(std::filesystem::is_regular_file(Path::FromUtf8(project.GetSceneFilePath(project.StartupScene))));
	CHECK(std::filesystem::is_regular_file(Path::FromUtf8(project.CsprojPath)));
	CHECK(std::filesystem::is_regular_file(Path::FromUtf8(project.ScriptsDirectory) / "GameScript.cs"));

	if (!keepDir || !*keepDir) {
		std::error_code ec;
		std::filesystem::remove_all(parent, ec);
	}
}
