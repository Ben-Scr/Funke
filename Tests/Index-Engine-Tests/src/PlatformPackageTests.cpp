// Platform packages (manifest parsing, the managed platform defines builds compile scripts with) and the
// script runtime backend registry that lets packages bring their own .NET runtime.

#include <doctest/doctest.h>

#include "Project/IndexBuildProfile.hpp"
#include "Project/IndexProject.hpp"
#include "Project/PlatformPackages.hpp"
#include "Scripting/ScriptRuntimeHost.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Index;

namespace {
	struct TempPackageDir {
		std::filesystem::path Path;

		explicit TempPackageDir(const std::string& manifest) {
			Path = std::filesystem::temp_directory_path() / ("index-platform-pkg-" + std::to_string(
				std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(Path);
			if (!manifest.empty()) {
				std::ofstream(Path / "index-package.lua", std::ios::binary) << manifest;
			}
		}
		~TempPackageDir() {
			std::error_code ec;
			std::filesystem::remove_all(Path, ec);
		}
	};

	void SetEnv(const char* name, const char* value) {
#if defined(_WIN32)
		_putenv_s(name, value ? value : "");
#else
		if (value) setenv(name, value, 1); else unsetenv(name);
#endif
	}
}

TEST_CASE("Platform package manifests are parsed from index-package.lua") {
	TempPackageDir dir(R"(return {
    name        = "Index.Platform.Web",
    version     = "0.2.0",
    kind        = "platform",
    platform    = "Web",
    description = "Browser builds.",
    builder     = "Builder/build.py",
    hosts       = "windows,macos,linux",
    protocol    = "1",
})");

	const auto info = PlatformPackages::ReadManifest(dir.Path);
	REQUIRE(info.has_value());
	CHECK(info->Name == "Index.Platform.Web");
	CHECK(info->Version == "0.2.0");
	CHECK(info->Kind == "platform");
	CHECK(info->Description == "Browser builds.");
	REQUIRE(info->Platform.has_value());
	CHECK(*info->Platform == BuildPlatform::Web);
	CHECK(info->Builder == dir.Path / "Builder" / "build.py");
	CHECK(info->Protocol == PlatformPackages::k_SupportedProtocol);
	CHECK(info->SupportsHost("windows"));
	CHECK(info->SupportsHost("linux"));
}

TEST_CASE("Build components carry no platform, and host lists restrict where builders run") {
	TempPackageDir dir(R"(return {
    name = "Index.ScriptRuntime.Mono", version = "0.1.0", kind = "build-component",
    hosts = "macos",
})");

	const auto info = PlatformPackages::ReadManifest(dir.Path);
	REQUIRE(info.has_value());
	CHECK(info->Kind == "build-component");
	CHECK_FALSE(info->Platform.has_value());
	CHECK(info->SupportsHost("macos"));
	CHECK_FALSE(info->SupportsHost("windows"));
}

TEST_CASE("Unusable platform manifests are rejected") {
	SUBCASE("missing manifest") {
		TempPackageDir dir("");
		CHECK_FALSE(PlatformPackages::ReadManifest(dir.Path).has_value());
	}
	SUBCASE("game package, not a platform package") {
		TempPackageDir dir(R"(return { name = "Index.Tilemap2D", version = "1.0.0", layers = {} })");
		CHECK_FALSE(PlatformPackages::ReadManifest(dir.Path).has_value());
	}
	SUBCASE("platform package without a builder") {
		TempPackageDir dir(R"(return { name = "Index.Platform.X", version = "1.0.0", kind = "platform", platform = "Android" })");
		CHECK_FALSE(PlatformPackages::ReadManifest(dir.Path).has_value());
	}
}

TEST_CASE("Build profiles round-trip the options handed to platform builders") {
	TempPackageDir dir("");
	IndexBuildProfile profile;
	profile.Name = "Android";
	profile.Platform = BuildPlatform::Android;
	profile.RenderBackend = IndexBuildProfile::AllowedBackends(BuildPlatform::Android).front();
	profile.PlatformOptions = { { "keystore", "C:/keys/release.jks" }, { "orientation", "landscape" } };
	const std::string file = (dir.Path / "Android.build").string();
	REQUIRE(profile.Save(file));

	const IndexBuildProfile loaded = IndexBuildProfile::Load(file);
	CHECK(loaded.Platform == BuildPlatform::Android);
	CHECK(loaded.PlatformOptions == profile.PlatformOptions);
}

TEST_CASE("Scripts are compiled with the target platform's define") {
	CHECK(IndexProject::GetManagedPlatformDefine(BuildPlatform::Windows) == "INDEX_WINDOWS");
	CHECK(IndexProject::GetManagedPlatformDefine(BuildPlatform::Android) == "INDEX_ANDROID");
	CHECK(IndexProject::GetManagedPlatformDefine(BuildPlatform::IOS) == "INDEX_IOS");
	CHECK(IndexProject::GetManagedPlatformDefine(BuildPlatform::Web) == "INDEX_WEB");
}

namespace {
	bool s_FakeRuntimeAvailable = false;

	class FakeScriptRuntime final : public ScriptRuntimeHost {
	public:
		const char* GetName() const override { return "doctest-fake"; }
		bool Initialize(const std::filesystem::path&) override { return true; }
		bool IsInitialized() const override { return true; }
		void* GetUnmanagedEntryPoint(const std::filesystem::path&, std::string_view, std::string_view, std::string_view) override {
			return nullptr;
		}
	};

	// Declines unless a test opts in, so registering it cannot steal other tests' hostfxr backend.
	std::unique_ptr<ScriptRuntimeHost> CreateFakeRuntime() {
		return s_FakeRuntimeAvailable ? std::make_unique<FakeScriptRuntime>() : nullptr;
	}
}

TEST_CASE("Script runtime backends registered by packages are selectable") {
	ScriptRuntimeHosts::Register("doctest-fake", &CreateFakeRuntime, -1000);
	const auto names = ScriptRuntimeHosts::GetRegisteredNames();
	CHECK(std::find(names.begin(), names.end(), "doctest-fake") != names.end());

	s_FakeRuntimeAvailable = true;
	SetEnv("INDEX_SCRIPT_RUNTIME", "doctest-fake");
	const auto host = ScriptRuntimeHosts::CreatePreferred();
	SetEnv("INDEX_SCRIPT_RUNTIME", nullptr);
	s_FakeRuntimeAvailable = false;

	REQUIRE(host != nullptr);
	CHECK(std::string(host->GetName()) == "doctest-fake");
}

TEST_CASE("Mobile orientation round-trips through project.json") {
	TempPackageDir dir("");
	std::filesystem::create_directories(dir.Path / "Assets");

	IndexProject project;
	project.Name = "Orientation";
	project.RootDirectory = dir.Path.string();
	project.ProjectFilePath = (dir.Path / "project.json").string();
	project.MobileOrientation = ScreenOrientation::LandscapeRight;
	REQUIRE(project.Save());
	CHECK(IndexProject::Load(dir.Path.string()).MobileOrientation == ScreenOrientation::LandscapeRight);

	for (ScreenOrientation value : { ScreenOrientation::Auto, ScreenOrientation::Landscape, ScreenOrientation::LandscapeLeft,
			ScreenOrientation::LandscapeRight, ScreenOrientation::Portrait, ScreenOrientation::PortraitUpsideDown,
			ScreenOrientation::PortraitAny, ScreenOrientation::AnyExceptUpsideDown, ScreenOrientation::Any }) {
		CHECK(IndexProject::ScreenOrientationFromString(IndexProject::ScreenOrientationToString(value)) == value);
	}
	CHECK(IndexProject::ScreenOrientationFromString("Sideways") == ScreenOrientation::Auto);
}
