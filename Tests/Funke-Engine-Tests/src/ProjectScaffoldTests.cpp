#include <doctest/doctest.h>

#ifdef FNK_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Match the engine PCH's GetObject macro in the existing Windows JSON ABI.
#include <Windows.h>
#endif

#include "Project/FunkeProject.hpp"
#include "Project/EngineSdk.hpp"
#include "Project/ManagedSdk.hpp"
#include "Core/Version.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>

using namespace Funke;

namespace {
	struct SdkResolverDirectory {
		std::filesystem::path Root = std::filesystem::temp_directory_path() / ("funke-sdk-resolver-" + std::to_string(
			std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::path Source = Root / "checkout";
		std::filesystem::path Executable = Source / "bin/Release-windows-x86_64/Funke-Editor";
		std::filesystem::path Sdk = Executable / "EngineSDK";

		~SdkResolverDirectory() {
			std::error_code ec;
			std::filesystem::remove_all(Root, ec);
		}

		void Write(const std::filesystem::path& file, const std::string& contents) const {
			std::filesystem::create_directories(file.parent_path());
			REQUIRE(File::WriteAllText(Path::ToUtf8(file), contents));
		}

		Json::Value CreateSdk() const {
			for (const char* file : { "Funke-ScriptCore.dll", "Funke-ScriptCore.deps.json", "Funke-ScriptCore.runtimeconfig.json" })
				Write(Sdk / "Funke-ScriptCore" / file, "fixture");
			std::filesystem::create_directories(Sdk / "Runtime-Development");
			std::filesystem::create_directories(Sdk / "Runtime-Dist");
			Json::Value manifest = Json::Parse(R"({"schemaVersion":1,"engineVersion":"1.2.3","platform":"windows","architecture":"x86_64",
"capabilities":{"managedProjects":true,"nativeDevelopment":false,"sourceBuild":false},
"runtimes":{"Development":"Runtime-Development","Release":"Runtime-Dist"}})");
			Save(manifest);
			return manifest;
		}

		void Save(const Json::Value& manifest) const { Write(Sdk / "FunkeSDK.json", Json::Stringify(manifest)); }

		void CreateSource() const {
			for (const char* file : { "premake5.lua", "Funke-Engine/premake5.lua", "Funke-Engine/src/Core/Application.cpp",
				"Funke-ScriptCore/Funke-ScriptCore.csproj" }) Write(Source / file, "fixture");
			std::filesystem::create_directories(Source / "Funke-ScriptCore/Source");
			std::filesystem::create_directories(Executable);
		}

		EngineSdk::Installation Resolve() const { return EngineSdk::Resolve(Executable, "windows", "x86_64", "1.2.3"); }
	};

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

TEST_CASE("Binary SDK resolves without a checkout source markers or optional templates") {
	SdkResolverDirectory directory;
	directory.CreateSdk();
	const auto installation = directory.Resolve();
	REQUIRE_MESSAGE(installation.IsValid(), installation.Error);
	CHECK(installation.ManifestPresent);
	CHECK(installation.ManagedProjects);
	CHECK_FALSE(installation.NativeDevelopment);
	CHECK_FALSE(installation.SourceBuild);
	CHECK(installation.Root == std::filesystem::weakly_canonical(directory.Sdk));
	CHECK(installation.Runtimes.at("Release") == installation.Root / "Runtime-Dist");
	CHECK_FALSE(std::filesystem::exists(directory.Sdk / "Funke-Engine"));
	CHECK_FALSE(std::filesystem::exists(directory.Sdk / "Funke-ScriptCore/Funke-ScriptCore.csproj"));
	CHECK_FALSE(std::filesystem::exists(directory.Sdk / "templates"));
}

TEST_CASE("Installed SDK wins over a checkout and accepts partial runtime variants") {
	SdkResolverDirectory directory;
	directory.CreateSource();
	auto manifest = directory.CreateSdk();
	std::filesystem::create_directories(directory.Sdk / "variants/2d/Dist");
	manifest.AddMember("runtimeVariants", Json::Parse(R"({"2d":{"Release":"variants/2d/Dist"}})"));
	directory.Save(manifest);
	const auto installation = directory.Resolve();
	REQUIRE_MESSAGE(installation.IsValid(), installation.Error);
	CHECK(installation.ManifestPresent);
	CHECK_FALSE(installation.SourceBuild);
	CHECK(installation.Root == std::filesystem::weakly_canonical(directory.Sdk));
	CHECK(installation.RuntimeVariants.at("2d").size() == 1);
	CHECK(installation.RuntimeVariants.at("2d").at("Release") == installation.Root / "variants/2d/Dist");
}

TEST_CASE("Developer SDK resolves with a single installed runtime profile") {
	SdkResolverDirectory directory;
	auto manifest = directory.CreateSdk();
	std::filesystem::remove(directory.Sdk / "Runtime-Dist");
	for (const char* profile : { "Development", "Release", "Debug" }) {
		CAPTURE(profile);
		Json::Value runtimes = Json::Value::MakeObject();
		runtimes.AddMember(profile, "Runtime-Development");
		manifest.AddMember("runtimes", std::move(runtimes));
		directory.Save(manifest);
		const auto installation = directory.Resolve();
		REQUIRE_MESSAGE(installation.IsValid(), installation.Error);
		CHECK(installation.ManagedProjects);
		CHECK(installation.ManifestPresent);
		CHECK_FALSE(installation.SourceBuild);
		REQUIRE(installation.Runtimes.size() == 1);
		CHECK(installation.Runtimes.at(profile) == installation.Root / "Runtime-Development");
	}
}

TEST_CASE("Incomplete or malformed installed SDK never falls back to a nearby checkout") {
	SdkResolverDirectory directory;
	directory.CreateSource();
	auto manifest = directory.CreateSdk();
	SUBCASE("Missing manifest") { std::filesystem::remove(directory.Sdk / "FunkeSDK.json"); }
	SUBCASE("Broken JSON") { directory.Write(directory.Sdk / "FunkeSDK.json", "{"); }
	SUBCASE("New schema") { manifest.AddMember("schemaVersion", 2); directory.Save(manifest); }
	SUBCASE("Fractional schema") { manifest.AddMember("schemaVersion", 1.5); directory.Save(manifest); }
	SUBCASE("Wrong platform") { manifest.AddMember("platform", "linux"); directory.Save(manifest); }
	SUBCASE("Wrong architecture") { manifest.AddMember("architecture", "arm64"); directory.Save(manifest); }
	SUBCASE("Different engine version") { manifest.AddMember("engineVersion", "1.2.4"); directory.Save(manifest); }
	SUBCASE("Missing engine version") { manifest.AddMember("engineVersion", ""); directory.Save(manifest); }
	SUBCASE("Capability is not a boolean") { manifest.FindMember("capabilities")->AddMember("managedProjects", "true"); directory.Save(manifest); }
	SUBCASE("Source build without source tree") { manifest.FindMember("capabilities")->AddMember("sourceBuild", true); directory.Save(manifest); }
	SUBCASE("Missing ScriptCore DLL") { std::filesystem::remove(directory.Sdk / "Funke-ScriptCore/Funke-ScriptCore.dll"); }
	SUBCASE("Missing runtime config") { std::filesystem::remove(directory.Sdk / "Funke-ScriptCore/Funke-ScriptCore.runtimeconfig.json"); }
	SUBCASE("Missing deps") { std::filesystem::remove(directory.Sdk / "Funke-ScriptCore/Funke-ScriptCore.deps.json"); }
	SUBCASE("Empty runtime profiles") { manifest.AddMember("runtimes", Json::Parse("{}")); directory.Save(manifest); }
	SUBCASE("Missing declared runtime directory") { std::filesystem::remove(directory.Sdk / "Runtime-Dist"); }
	SUBCASE("Unknown runtime profile") { manifest.FindMember("runtimes")->AddMember("Dist", "Runtime-Dist"); directory.Save(manifest); }
	SUBCASE("Invalid runtime value") { manifest.FindMember("runtimes")->AddMember("Release", 3); directory.Save(manifest); }
	SUBCASE("Unknown variant") { manifest.AddMember("runtimeVariants", Json::Parse(R"({"other":{"Release":"Runtime-Dist"}})")); directory.Save(manifest); }
	const auto installation = directory.Resolve();
	CHECK_FALSE(installation.IsValid());
	CHECK(installation.ManifestPresent);
	CHECK_FALSE(installation.Error.empty());
	CHECK(installation.Root != std::filesystem::weakly_canonical(directory.Source));
}

TEST_CASE("SDK runtime paths reject escaping absolute missing and linked directories") {
	SdkResolverDirectory directory;
	auto manifest = directory.CreateSdk();
	std::filesystem::create_directories(directory.Executable / "outside");
	for (const char* path : { "../outside", "Runtime-Dist/../../outside", "/outside", "C:/outside",
		"C:\\outside", "..\\outside", ".", "missing" }) {
		CAPTURE(path);
		manifest.FindMember("runtimes")->AddMember("Release", path);
		directory.Save(manifest);
		CHECK_FALSE(directory.Resolve().IsValid());
	}
	std::error_code ec;
	std::filesystem::create_directory_symlink(directory.Executable / "outside", directory.Sdk / "linked", ec);
	if (!ec) {
		manifest.FindMember("runtimes")->AddMember("Release", "linked");
		directory.Save(manifest);
		CHECK_FALSE(directory.Resolve().IsValid());
	}
	manifest.FindMember("runtimes")->AddMember("Release", "Runtime-Dist");
	manifest.AddMember("runtimeVariants", Json::Parse(R"({"2d":{"Release":"../outside"}})"));
	directory.Save(manifest);
	CHECK_FALSE(directory.Resolve().IsValid());
}

TEST_CASE("Source fallback requires a real source tree and no adjacent SDK") {
	SdkResolverDirectory directory;
	directory.Write(directory.Source / "Funke-ScriptCore/Funke-ScriptCore.csproj", "marker");
	std::filesystem::create_directories(directory.Source / "Funke-Engine/src");
	std::filesystem::create_directories(directory.Executable);
	CHECK_FALSE(directory.Resolve().IsValid());
	directory.CreateSource();
	const auto source = directory.Resolve();
	REQUIRE(source.IsValid());
	CHECK_FALSE(source.ManifestPresent);
	CHECK(source.SourceBuild);
	CHECK(source.NativeDevelopment);
	CHECK(source.ManagedProjects);
	CHECK(source.Root == std::filesystem::weakly_canonical(directory.Source));
	std::filesystem::create_directories(directory.Sdk);
	CHECK_FALSE(directory.Resolve().IsValid());
}

TEST_CASE("Launcher SDK selection matches stable net10 latestFeature policy") {
	std::string version = "stale";
	CHECK_FALSE(ManagedSdk::FindCompatibleSdk("9.0.100 [sdk]\n11.0.100 [sdk]\n10.0.100-preview.1 [sdk]\n10.1.100 [sdk]\n", version));
	CHECK(version.empty());
	CHECK(ManagedSdk::FindCompatibleSdk("11.0.100 [sdk]\r\n10.0.100 [sdk]\r\n 10.0.302 [sdk]\r\n10.0.201 [sdk]", version));
	CHECK(version == "10.0.302");
	for (const char* value : { "", "10.0.", "10.0.99", "10.0.100-rc.1", "10.0.100oops", "10.0.-100", "10.0.99999999999999999999", "10.1.100", "11.0.100" }) {
		CAPTURE(value);
		CHECK_FALSE(ManagedSdk::IsCompatibleVersion(value));
	}
	CHECK(ManagedSdk::IsCompatibleVersion(ManagedSdk::k_Version));
}

TEST_CASE("Package builds ignore the empty workspace utility but still require a native SDK for C++ projects") {
	SdkResolverDirectory directory;
	auto manifest = directory.CreateSdk();
	manifest.AddMember("engineVersion", FNK_VERSION);
	manifest.AddMember("platform", std::string(EngineSdk::HostPlatform()));
	manifest.AddMember("architecture", std::string(EngineSdk::HostArchitecture()));
	directory.Save(manifest);
	struct RestoreExecutableDirectory {
		std::string Previous = Path::ExecutableDir();
		~RestoreExecutableDirectory() { Path::SetExecutableDirOverride(Previous); }
	} restoreExecutableDirectory;
	Path::SetExecutableDirOverride(Path::ToUtf8(directory.Executable));
	REQUIRE(FunkeProject::GetEngineInstallation().IsValid());
	const auto project = directory.Root / "Game";
	const auto generated = project / ".funke/build/packages/generated";
	directory.Write(generated / "FunkeProjectPackages.Empty/FunkeProjectPackages.Empty.vcxproj",
		"<Project><PropertyGroup><ConfigurationType>Utility</ConfigurationType></PropertyGroup></Project>");
	bool expectsNative = false;
	SUBCASE("Empty workspace needs no native toolchain") {}
	SUBCASE("Real native package retains the capability check") {
		directory.Write(generated / "Pkg.Test.Native/Pkg.Test.Native.vcxproj", "<Project />");
		expectsNative = true;
	}
	SUBCASE("Other utility projects are not silently skipped") {
		directory.Write(generated / "Pkg.Test.Native/Utility.vcxproj", "<Project />");
		expectsNative = true;
	}
	const auto result = FunkeProject::BuildProjectPackages(Path::ToUtf8(project), "Release", "x64");
	if (expectsNative) {
		CHECK_FALSE(result.Succeeded);
		CHECK(result.Output.find("Install the matching Native SDK component") != std::string::npos);
	} else {
		CHECK_MESSAGE(result.Succeeded, result.Output);
		CHECK(result.ExitCode == 0);
	}
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
	CHECK(cmake.find("capabilities nativeDevelopment") != std::string::npos);
	CHECK(cmake.find("Install the matching Native SDK component") != std::string::npos);
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
