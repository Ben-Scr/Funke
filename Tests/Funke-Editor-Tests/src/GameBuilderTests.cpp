#include <doctest/doctest.h>

#include "Build/GameBuilder.hpp"
#include "Build/DotNetRuntimeBundle.hpp"
#include "Editor/PlatformBuilder.hpp"
#include "Project/FunkeBuildProfile.hpp"
#include "Project/FunkeProject.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Utils/Process.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Funke;

TEST_CASE("Bundled .NET runtime selects the newest stable compatible patch") {
	const auto root = std::filesystem::temp_directory_path() / "Funke .NET Runtime";
	const std::string frameworkRoot = Path::ToUtf8(root / "shared" / "Microsoft.NETCore.App");
	const auto line = [&](const char* version) { return "Microsoft.NETCore.App " + std::string(version) + " [" + frameworkRoot + "]\r\n"; };
	DotNetRuntimeBundle::Runtime runtime;
	std::string error;
	REQUIRE(DotNetRuntimeBundle::SelectRuntime(line("10.0.9") + line("10.0.11")
		+ line("10.0.12-preview.1") + line("11.0.0") + line("10.1.0"), "10.0.0", runtime, error));
	CHECK(runtime.Version == "10.0.11");
	CHECK(runtime.Root == root);
	CHECK(runtime.FrameworkDirectory == root / "shared" / "Microsoft.NETCore.App" / "10.0.11");
	CHECK_FALSE(DotNetRuntimeBundle::SelectRuntime(line("10.0.9"), "10.0.10", runtime, error));
	CHECK_FALSE(error.empty());
	CHECK_FALSE(DotNetRuntimeBundle::SelectRuntime(line("10.0.11"), "10.0.0-preview", runtime, error));
	CHECK_FALSE(DotNetRuntimeBundle::SelectRuntime("Microsoft.NETCore.App 10.0.11 [../shared/Microsoft.NETCore.App]",
		"10.0.0", runtime, error));
}

namespace {
	struct TempProject {
		TempProject() {
			Root = std::filesystem::temp_directory_path()
				/ ("FunkeGameBuilderTest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(Root / "BuildProfiles");
			Project.Name = "Test";
			Project.RootDirectory = Path::ToUtf8(Root);
		}
		~TempProject() {
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		void AddProfile(const std::string& name, BuildPlatform platform) {
			FunkeBuildProfile profile;
			profile.Name = name;
			profile.Platform = platform;
			REQUIRE(profile.Save(Path::ToUtf8(Root / "BuildProfiles" / (name + ".build"))));
		}

		std::filesystem::path Root;
		FunkeProject Project;
	};
}

TEST_CASE("Bundled .NET runtime refuses incomplete or incompatible installations before copying") {
	TempProject temp;
	DotNetRuntimeBundle::Runtime runtime{ "10.0.11", temp.Root / "dotnet",
		temp.Root / "dotnet" / "shared" / "Microsoft.NETCore.App" / "10.0.11" };
	const auto destination = temp.Root / "output";
	std::string error;
	CHECK_FALSE(DotNetRuntimeBundle::Stage(runtime, destination, error));
	CHECK_FALSE(std::filesystem::exists(destination));
#if defined(FNK_PLATFORM_WINDOWS)
	const char* core = "coreclr.dll";
	const char* host = "hostfxr.dll"; const char* policy = "hostpolicy.dll";
#elif defined(FNK_PLATFORM_MACOS)
	const char* core = "libcoreclr.dylib";
	const char* host = "libhostfxr.dylib"; const char* policy = "libhostpolicy.dylib";
#else
	const char* core = "libcoreclr.so";
	const char* host = "libhostfxr.so"; const char* policy = "libhostpolicy.so";
#endif
	const auto hostDirectory = runtime.Root / "host" / "fxr" / "10.0.3";
	std::filesystem::create_directories(hostDirectory);
	std::filesystem::create_directories(runtime.FrameworkDirectory);
	for (const auto& file : { runtime.FrameworkDirectory / core, hostDirectory / host, runtime.FrameworkDirectory / policy, runtime.FrameworkDirectory / "Microsoft.NETCore.App.deps.json",
		runtime.FrameworkDirectory / "System.Private.CoreLib.dll", runtime.Root / "LICENSE.txt",
		runtime.Root / "ThirdPartyNotices.txt" }) {
		REQUIRE(File::WriteAllText(Path::ToUtf8(file), std::string(128, 'x')));
	}
	CHECK_FALSE(DotNetRuntimeBundle::Stage(runtime, destination, error));
	CHECK(error.find("architecture") != std::string::npos);
	CHECK_FALSE(std::filesystem::exists(destination));
}

namespace {
	EngineSdk::Installation MakeInstalledSdk(const std::filesystem::path& root) {
		EngineSdk::Installation installation;
		installation.Root = root;
		installation.ManifestPresent = true;
		installation.ManagedProjects = true;
		return installation;
	}

	void WriteInstalledRuntime(const std::filesystem::path& directory) {
		std::filesystem::create_directories(directory / "FunkeAssets");
		std::filesystem::create_directories(directory / "Licenses");
		std::vector<std::string> files{ "Funke-ScriptCore.dll", "Funke-ScriptCore.deps.json", "Funke-ScriptCore.runtimeconfig.json" };
#if defined(FNK_PLATFORM_WINDOWS)
		files.insert(files.end(), { "Funke-Runtime.exe", "Funke-Engine.dll", "SDL3.dll", "nethost.dll" });
#elif defined(FNK_PLATFORM_MACOS)
		files.insert(files.end(), { "Funke-Runtime", "libFunke-Engine.dylib", "libSDL3.0.dylib", "libwebgpu_dawn.dylib" });
#else
		files.insert(files.end(), { "Funke-Runtime", "libFunke-Engine.so", "libSDL3.so.0", "libwebgpu_dawn.so" });
#endif
		for (const auto& name : files) REQUIRE(File::WriteAllText(Path::ToUtf8(directory / name), "fixture"));
	}
}

TEST_CASE("Installed export selects the declared configuration and refuses configuration fallback") {
	TempProject temp;
	auto installation = MakeInstalledSdk(temp.Root);
	const auto development = temp.Root / "Runtime-Development";
	const auto release = temp.Root / "Runtime-Dist";
	WriteInstalledRuntime(development);
	WriteInstalledRuntime(release);
	installation.Runtimes = { { "Development", development }, { "Release", release } };
	CHECK(GameBuilder::SelectInstalledRuntime(installation, FunkeProject::BuildProfile::Development).Directory == development);
	CHECK(GameBuilder::SelectInstalledRuntime(installation, FunkeProject::BuildProfile::Release).Directory == release);
	const auto debug = GameBuilder::SelectInstalledRuntime(installation, FunkeProject::BuildProfile::Debug);
	CHECK_FALSE(debug.Succeeded());
	CHECK(debug.Error.find("Debug") != std::string::npos);
	installation.Runtimes.erase("Development");
	CHECK_FALSE(GameBuilder::SelectInstalledRuntime(installation, FunkeProject::BuildProfile::Development).Succeeded());
}

TEST_CASE("Installed export uses an available module variant and rejects a damaged declared runtime") {
	TempProject temp;
	auto installation = MakeInstalledSdk(temp.Root);
	const auto full = temp.Root / "Runtime-Dist";
	const auto reduced = temp.Root / "Runtime-Dist-2d";
	WriteInstalledRuntime(full);
	WriteInstalledRuntime(reduced);
	installation.Runtimes = { { "Release", full } };
	installation.RuntimeVariants = { { "2d", { { "Release", reduced } } } };
	const auto variant = GameBuilder::SelectInstalledRuntime(installation, FunkeProject::BuildProfile::Release, "2d");
	CHECK(variant.Directory == reduced);
	CHECK(variant.Variant == "2d");
	const auto available = GameBuilder::SelectInstalledRuntime(installation, FunkeProject::BuildProfile::Release, "3d");
	CHECK(available.Directory == full);
	CHECK(available.Variant.empty());
	std::filesystem::remove(reduced / "Funke-ScriptCore.dll");
	const auto damaged = GameBuilder::SelectInstalledRuntime(installation, FunkeProject::BuildProfile::Release, "2d");
	CHECK_FALSE(damaged.Succeeded());
	CHECK(damaged.Error.find("Funke-ScriptCore.dll") != std::string::npos);
}

TEST_CASE("Installed export preserves manifest errors instead of resolving unrelated artifacts") {
	TempProject temp;
	auto installation = MakeInstalledSdk(temp.Root);
	WriteInstalledRuntime(temp.Root);
	installation.Runtimes = { { "Release", temp.Root } };
	installation.Error = "unsupported SDK schema";
	CHECK(GameBuilder::SelectInstalledRuntime(installation, FunkeProject::BuildProfile::Release).Error == installation.Error);
}

TEST_CASE("Build profiles include .NET by default and preserve the explicit opt out") {
	TempProject temp;
	const auto file = Path::ToUtf8(temp.Root / "BuildProfiles" / "Runtime.build");
	REQUIRE(File::WriteAllText(file, R"({"name":"Runtime","platform":"Windows"})"));
	auto profile = FunkeBuildProfile::Load(file);
	CHECK(profile.BundleDotNetRuntime);
	profile.BundleDotNetRuntime = false;
	REQUIRE(profile.Save(file));
	CHECK_FALSE(FunkeBuildProfile::Load(file).BundleDotNetRuntime);
}

TEST_CASE("GameBuilder::Prepare folds the profile's configuration, defines and test switches into the snapshot only") {
	TempProject temp;
	std::filesystem::create_directories(temp.Root / "Assets" / "Scenes");
	REQUIRE(File::WriteAllText(Path::ToUtf8(temp.Root / "Assets" / "Scenes" / "Main.scene"),
		R"({ "version": 1, "name": "Main", "entities": [] })"));
	temp.Project.AssetsDirectory = Path::ToUtf8(temp.Root / "Assets");
	temp.Project.BuildSceneList = { { "Main", true } };
	temp.Project.CustomDefines = { "STEAM" };

	FunkeBuildProfile qa;
	qa.Name = "QA";
	qa.Platform = BuildPlatform::Windows;
	qa.RenderBackend = FunkeProject::RenderBackend::Direct3D12;
	qa.Configuration = FunkeProject::BuildProfile::Debug;
	qa.Defines = { "FUNKE_TEST" };
	qa.DevConsole = false;
	qa.ShowRuntimeLogs = true;
	qa.IsolatedPrefs = true;
	qa.BundleDotNetRuntime = false;
	REQUIRE(qa.Save(Path::ToUtf8(temp.Root / "BuildProfiles" / "QA.build")));
	temp.Project.ActiveBuildProfileName = "QA";

	std::string error;
	GameBuilder::Request request;
	REQUIRE(GameBuilder::Prepare(temp.Project, request, error));
	CHECK(request.Project.ActiveBuildProfile == FunkeProject::BuildProfile::Debug);
	CHECK(request.Project.CustomDefines == std::vector<std::string>{ "STEAM", "FUNKE_TEST" });
	CHECK(request.Project.DevConsole == false);
	CHECK(request.Project.ShowRuntimeLogs == true);
	CHECK_FALSE(request.Project.ShowRuntimeStats.has_value());
	CHECK(request.Project.PrefsProfile == "QA");
	CHECK_FALSE(request.BundleDotNetRuntime);
	// The live project keeps its own values.
	CHECK(temp.Project.ActiveBuildProfile == FunkeProject::BuildProfile::Development);
	CHECK(temp.Project.CustomDefines == std::vector<std::string>{ "STEAM" });
	CHECK(temp.Project.PrefsProfile.empty());

	// --release/--development/--debug on the CLI beat the profile.
	GameBuilder::Request overridden;
	overridden.ConfigurationOverride = FunkeProject::BuildProfile::Release;
	REQUIRE(GameBuilder::Prepare(temp.Project, overridden, error));
	CHECK(overridden.Project.ActiveBuildProfile == FunkeProject::BuildProfile::Release);
}

TEST_CASE("GameBuilder resolves the requested, active or first matching build profile") {
	TempProject temp;
	temp.AddProfile("Android", BuildPlatform::Android);
	temp.AddProfile("Desktop", BuildPlatform::Windows);
	temp.AddProfile("Web", BuildPlatform::Web);
	std::string error;

	CHECK(GameBuilder::ResolveProfileName(temp.Project, "Web", std::nullopt, error) == "Web");
	CHECK(GameBuilder::ResolveProfileName(temp.Project, "", BuildPlatform::Android, error) == "Android");

	temp.Project.ActiveBuildProfileName = "Web";
	CHECK(GameBuilder::ResolveProfileName(temp.Project, "", std::nullopt, error) == "Web");
	// The active profile only wins while it targets the requested platform.
	CHECK(GameBuilder::ResolveProfileName(temp.Project, "", BuildPlatform::Android, error) == "Android");

	CHECK(GameBuilder::ResolveProfileName(temp.Project, "Missing", std::nullopt, error).empty());
	CHECK(error.find("Missing") != std::string::npos);
	CHECK(GameBuilder::ResolveProfileName(temp.Project, "Web", BuildPlatform::Android, error).empty());
	CHECK(GameBuilder::ResolveProfileName(temp.Project, "", BuildPlatform::IOS, error).empty());
}

TEST_CASE("GameBuilder refuses unsupported native script targets instead of silently omitting their code") {
	TempProject temp;
	std::filesystem::create_directories(temp.Root / "Assets");
	temp.Project.AssetsDirectory = Path::ToUtf8(temp.Root / "Assets");
	temp.Project.NativeScriptsDir = Path::ToUtf8(temp.Root / "NativeScripts");
	temp.Project.NativeSourceDir = Path::ToUtf8(temp.Root / "NativeScripts" / "Source");
	REQUIRE(File::WriteAllText(Path::ToUtf8(temp.Root / "Assets" / "Player.cpp"), "REGISTER_SCRIPT(Player)\n"));
	GameBuilder::Request request;
	request.Project = temp.Project;
	request.Platform = BuildPlatform::Android;
	request.OutputDirectory = Path::ToUtf8(temp.Root / "Builds" / "Android");
	const auto result = GameBuilder::Build(std::move(request), {});
	CHECK_FALSE(result.Succeeded);
	CHECK(result.Stage == "Native scripts need a host desktop target");
	CHECK_FALSE(std::filesystem::exists(temp.Root / "Builds" / "Android"));
}

TEST_CASE("GameBuilder converts scenes, prefabs and data assets between JSON and binary") {
	TempProject temp;
	const std::filesystem::path assets = temp.Root / "Assets";
	std::filesystem::create_directories(assets / "Data");
	const std::string scene = Path::ToUtf8(assets / "Main.scene");
	const std::string data = Path::ToUtf8(assets / "Data" / "Stats.dataasset");
	const std::string script = Path::ToUtf8(assets / "Player.cs");
	REQUIRE(File::WriteAllText(scene, R"({"version":1,"name":"Main","entities":[]})"));
	REQUIRE(File::WriteAllText(data, R"({"type":"Stats","fields":{"Health":"10"}})"));
	REQUIRE(File::WriteAllText(script, "class Player {}"));

	std::string failure;
	CHECK(GameBuilder::ConvertSerializedAssets(assets, SceneSerializationFormat::Binary, {}, &failure) == 2);
	CHECK(failure.empty());
	CHECK(SceneSerializer::IsBinarySerializedFile(scene));
	CHECK(SceneSerializer::IsBinarySerializedFile(data));
	CHECK(File::ReadAllText(script) == "class Player {}");

	CHECK(GameBuilder::ConvertSerializedAssets(assets, SceneSerializationFormat::Json) == 2);
	CHECK_FALSE(SceneSerializer::IsBinarySerializedFile(data));
	CHECK(File::ReadAllText(data).find("\"Health\"") != std::string::npos);
}

TEST_CASE("Web preview serves the published entry and reuses its server after a rebuild") {
	TempProject temp;
	const std::filesystem::path site = temp.Root / Path::FromUtf8("Web preview Caf\xc3\xa9");
	std::filesystem::create_directories(site);
	const std::filesystem::path entry = site / "index.html";
	REQUIRE(File::WriteAllText(Path::ToUtf8(entry), "first build"));
	const std::filesystem::path helper = Path::FromUtf8(FunkeProject::GetEngineRootDir())
		/ "platforms/Funke.Platform.Web/Builder/preview.py";
	const PlatformBuilder::WebPreview preview = PlatformBuilder::StartWebPreview(helper, entry);
	INFO(preview.Error);
	REQUIRE(preview.Error.empty());
	REQUIRE_FALSE(preview.Url.empty());
	REQUIRE(Process::IsProcessRunning(preview.ProcessId));

	struct StopPreview {
		unsigned long Pid;
		~StopPreview() {
			std::vector<std::string> command = PlatformBuilder::FindPython();
			command.insert(command.end(), { "-c", "import os, signal, sys; os.kill(int(sys.argv[1]), signal.SIGTERM)",
				std::to_string(Pid) });
			Process::Run(command);
		}
	} stop{ preview.ProcessId };

	auto fetch = [&](const std::string& url) {
		std::vector<std::string> command = PlatformBuilder::FindPython();
		command.insert(command.end(), { "-c", "import sys, urllib.request; "
			"print(urllib.request.urlopen(sys.argv[1], timeout=5).read().decode(), end='')", url });
		return Process::Run(command, {}, std::chrono::seconds(10));
	};
	const Process::Result first = fetch(preview.Url);
	CHECK(first.Succeeded());
	CHECK(first.Output == "first build");

	REQUIRE(File::WriteAllText(Path::ToUtf8(entry), "rebuilt game"));
	const PlatformBuilder::WebPreview again = PlatformBuilder::StartWebPreview(helper, entry, preview);
	CHECK(again.Error.empty());
	CHECK(again.ProcessId == preview.ProcessId);
	CHECK(again.Url == preview.Url);
	const Process::Result rebuilt = fetch(again.Url);
	CHECK(rebuilt.Succeeded());
	CHECK(rebuilt.Output == "rebuilt game");
}

TEST_CASE("Web preview reports a missing entry or platform helper") {
	TempProject temp;
	const std::filesystem::path entry = temp.Root / "index.html";
	const std::filesystem::path helper = temp.Root / "missing-preview.py";
	CHECK_FALSE(PlatformBuilder::StartWebPreview(helper, entry).Error.empty());
	REQUIRE(File::WriteAllText(Path::ToUtf8(entry), "game"));
	CHECK_FALSE(PlatformBuilder::StartWebPreview(helper, entry).Error.empty());
}
