#include <doctest/doctest.h>

#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Core/Version.hpp"
#include "Project/FunkeProject.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>

using namespace Funke;

namespace {
	class TimeTestApplication final : public Application {};
}

// project.json round-trip for the default physics-world Hz setting. Mirrors the
// write-if-non-default serialization contract: a non-default value survives
// Save() -> Load(), the default is omitted and reconstructed, and the load-time
// clamp keeps out-of-range input inside Time::SetFixedDeltaTime's [1, 240] Hz range.
TEST_CASE("FunkeProject round-trips physics tick rate through project.json") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("IndexPhysicsRT_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code ec; fs::remove_all(Root, ec); }
	} cleanup{ root };

	auto makeProject = [&](const char* name) {
		FunkeProject p;
		p.Name = name;
		p.RootDirectory = root.string();
		p.ProjectFilePath = Path::Combine(root.string(), "project.json");
		return p;
	};

	SUBCASE("non-default value survives a round-trip") {
		FunkeProject p = makeProject("Physics RT");
		p.PhysicsTickRate = 120;
		REQUIRE(p.Save());

		FunkeProject loaded = FunkeProject::Load(root.string());
		CHECK(loaded.PhysicsTickRate == 120);
	}

	SUBCASE("default is omitted and reconstructed") {
		FunkeProject p = makeProject("Physics Defaults");
		// Leave PhysicsTickRate = 50 (default).
		REQUIRE(p.Save());

		FunkeProject loaded = FunkeProject::Load(root.string());
		CHECK(loaded.PhysicsTickRate == 50);
	}

	SUBCASE("out-of-range value is clamped on load") {
		FunkeProject p = makeProject("Physics Clamp");
		p.PhysicsTickRate = 100000; // absurd; loader clamps to the [1, 240] Hz range
		REQUIRE(p.Save());

		FunkeProject loaded = FunkeProject::Load(root.string());
		CHECK(loaded.PhysicsTickRate == FunkeProject::k_MaxPhysicsHz);
	}
}

// Guards the apply path itself, not just serialization: the Hz the panel/startup
// push into Time must round-trip to the expected fixed timestep. The Hz bounds are
// chosen to land exactly on Time::SetFixedDeltaTime's [1/240, 1]s clamp endpoints, so
// this also locks the FunkeProject bounds and the Time clamp together.
TEST_CASE("PhysicsTickRate maps to the fixed timestep Time actually uses") {
	auto fixedStepFor = [](int hz) {
		FunkeProject p;
		p.PhysicsTickRate = hz;
		TimeTestApplication application;
		Time::SetFixedDeltaTime(p.GetFixedDeltaTimeSeconds());
		return Time::GetFixedDeltaTime();
	};

	CHECK(fixedStepFor(50) == doctest::Approx(1.0f / 50.0f));
	CHECK(fixedStepFor(60) == doctest::Approx(1.0f / 60.0f));
	// Endpoints sit on Time's clamp bounds, so they pass through unchanged.
	CHECK(fixedStepFor(FunkeProject::k_MaxPhysicsHz) == doctest::Approx(1.0f / 240.0f));
	CHECK(fixedStepFor(FunkeProject::k_MinPhysicsHz) == doctest::Approx(1.0f));
}

TEST_CASE("Time::SetTimeScale clamps to [0, 100]") {
	TimeTestApplication application;

	Time::SetTimeScale(2.5f);
	CHECK(Time::GetTimeScale() == doctest::Approx(2.5f));
	Time::SetTimeScale(-3.0f);
	CHECK(Time::GetTimeScale() == 0.0f);
	Time::SetTimeScale(1.0e9f);
	CHECK(Time::GetTimeScale() == doctest::Approx(100.0f));
	Time::SetTimeScale(std::numeric_limits<float>::infinity());
	CHECK(Time::GetTimeScale() == doctest::Approx(100.0f));
	Time::SetTimeScale(std::numeric_limits<float>::quiet_NaN());
	CHECK(Time::GetTimeScale() == 0.0f);
}

TEST_CASE("FunkeProject round-trips a non-default build resolution through project.json") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkeDisplayRT_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	FunkeProject project;
	project.Name = "Display RT";
	project.RootDirectory = root.string();
	project.ProjectFilePath = Path::Combine(root.string(), "project.json");
	project.BuildWidth = 3440;
	project.BuildHeight = 1440;
	REQUIRE(project.Save());

	const FunkeProject loaded = FunkeProject::Load(root.string());
	CHECK(loaded.BuildWidth == 3440);
	CHECK(loaded.BuildHeight == 1440);
}

TEST_CASE("FunkeProject persists the Editor View 3D toggle only once it was set") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkeEditorView3DRT_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	FunkeProject project;
	project.Name = "Editor View RT";
	project.RootDirectory = root.string();
	project.ProjectFilePath = Path::Combine(root.string(), "project.json");
	REQUIRE(project.Save());
	CHECK_FALSE(FunkeProject::Load(root.string()).EditorView3D.has_value());

	for (bool value : { true, false }) {
		project.EditorView3D = value;
		REQUIRE(project.Save());
		const FunkeProject loaded = FunkeProject::Load(root.string());
		REQUIRE(loaded.EditorView3D.has_value());
		CHECK(*loaded.EditorView3D == value);
	}
}

TEST_CASE("UI scale modes map the reference resolution onto any aspect ratio") {
	FunkeProject project;
	project.UIReferenceWidth = 1920;
	project.UIReferenceHeight = 1080;

	SUBCASE("Fit keeps the whole reference visible on a wide phone and a tall window") {
		project.UIScaling = FunkeProject::UIScaleMode::Fit;
		CHECK(project.ComputeUIScale(2424.0f, 1080.0f) == doctest::Approx(1.0f));
		CHECK(project.ComputeUIScale(960.0f, 1080.0f) == doctest::Approx(0.5f));
		CHECK(1080.0f * project.ComputeUIScale(2424.0f, 1080.0f) <= 1080.0f + 0.01f);
	}

	SUBCASE("Fill covers the screen") {
		project.UIScaling = FunkeProject::UIScaleMode::Fill;
		CHECK(project.ComputeUIScale(2424.0f, 1080.0f) == doctest::Approx(2424.0f / 1920.0f));
	}

	SUBCASE("Match blends width and height in log space") {
		project.UIScaling = FunkeProject::UIScaleMode::MatchWidthOrHeight;
		project.UIScaleMatch = 0.0f;
		CHECK(project.ComputeUIScale(3840.0f, 1080.0f) == doctest::Approx(2.0f));
		project.UIScaleMatch = 1.0f;
		CHECK(project.ComputeUIScale(3840.0f, 1080.0f) == doctest::Approx(1.0f));
		project.UIScaleMatch = 0.5f;
		CHECK(project.ComputeUIScale(3840.0f, 1080.0f) == doctest::Approx(std::sqrt(2.0f)));
	}

	SUBCASE("A zero-sized surface falls back to 1") {
		project.UIScaling = FunkeProject::UIScaleMode::Fit;
		CHECK(project.ComputeUIScale(0.0f, 1080.0f) == doctest::Approx(1.0f));
	}
}

TEST_CASE("FunkeProject round-trips the UI scale mode through project.json") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkeUIScaleRT_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	FunkeProject project;
	project.Name = "UI Scale RT";
	project.RootDirectory = root.string();
	project.ProjectFilePath = Path::Combine(root.string(), "project.json");
	project.UIScaling = FunkeProject::UIScaleMode::Fit;
	REQUIRE(project.Save());
	CHECK(FunkeProject::Load(root.string()).UIScaling == FunkeProject::UIScaleMode::Fit);

	for (FunkeProject::UIScaleMode mode : { FunkeProject::UIScaleMode::MatchWidthOrHeight,
			FunkeProject::UIScaleMode::Fit, FunkeProject::UIScaleMode::Fill }) {
		CHECK(FunkeProject::UIScaleModeFromString(FunkeProject::UIScaleModeToString(mode)) == mode);
	}
	CHECK(FunkeProject::UIScaleModeFromString("Stretch") == FunkeProject::UIScaleMode::MatchWidthOrHeight);
}

TEST_CASE("FunkeProject rejects unreadable and newer project formats") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkeProjectReject_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	const fs::path projectFile = root / "project.json";
	FunkeProject loaded;
	loaded.Name = "unchanged";
	std::string error;

	REQUIRE(File::WriteAllText(projectFile.string(), "{ invalid json"));
	CHECK_FALSE(FunkeProject::TryLoad(root.string(), loaded, &error));
	CHECK_FALSE(error.empty());
	CHECK(loaded.Name == "unchanged");
	REQUIRE(File::WriteAllText(projectFile.string(),
		"{\"projectFormatVersion\":1,\"name\":42}"));
	CHECK_FALSE(FunkeProject::TryLoad(root.string(), loaded, &error));
	CHECK(error.find("name") != std::string::npos);
	REQUIRE(File::WriteAllText(projectFile.string(),
		"{\"projectFormatVersion\":1,\"name\":\"Invalid\",\"engineVersion\":\"old\","
		"\"graphics\":{\"renderScale\":\"fast\"}}"));
	CHECK_FALSE(FunkeProject::TryLoad(root.string(), loaded, &error));
	CHECK(error.find("graphics.renderScale") != std::string::npos);

	const std::string newerJson =
		"{\"projectFormatVersion\":"
		+ std::to_string(FunkeProject::k_CurrentProjectFormatVersion + 1)
		+ ",\"name\":\"Future\"}";
	REQUIRE(File::WriteAllText(projectFile.string(), newerJson));
	CHECK_FALSE(FunkeProject::TryLoad(root.string(), loaded, &error));
	CHECK(error.find("newer") != std::string::npos);
	CHECK_THROWS_AS(FunkeProject::Load(root.string()), std::runtime_error);
	CHECK(File::ReadAllText(projectFile.string()) == newerJson);
}

TEST_CASE("FunkeProject rejects project names that escape derived paths") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path container = fs::temp_directory_path() / ("FunkeProjectNameReject_" + std::to_string(suffix));
	const fs::path root = container / "Project";
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ container };

	const fs::path victim = container / "victim.csproj";
	const std::string originalVictim = "<Project><PropertyGroup /></Project>";
	REQUIRE(File::WriteAllText(victim.string(), originalVictim));
	REQUIRE(File::WriteAllText((root / "project.json").string(),
		"{\"projectFormatVersion\":1,\"name\":\"../victim\",\"engineVersion\":\"old\"}"));

	FunkeProject project;
	std::string error;
	CHECK_FALSE(FunkeProject::TryLoad(root.string(), project, &error));
	CHECK(error.find("project name") != std::string::npos);
	CHECK(File::ReadAllText(victim.string()) == originalVictim);
	CHECK_FALSE(FunkeProject::IsValidProjectName("CON.txt"));
	CHECK_FALSE(FunkeProject::IsValidProjectName("Editor."));
}

TEST_CASE("FunkeProject migrates legacy JSON with a backup and preserves unknown members") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkeProjectMigration_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	const fs::path projectFile = root / "project.json";
	const fs::path backupFile = root / "project.json.pre-v0.bak";
	const std::string legacyJson =
		"{\"name\":\"Legacy\",\"engineVersion\":\"0.0.0-old\","
		"\"thirdPartySettings\":{\"mode\":\"kept\"},"
		"\"graphics\":{\"vendorMode\":\"kept\"}}";
	REQUIRE(File::WriteAllText(projectFile.string(), legacyJson));

	FunkeProject project;
	std::string error;
	REQUIRE(FunkeProject::TryLoad(root.string(), project, &error));
	CHECK(project.ProjectFormatVersion == FunkeProject::k_CurrentProjectFormatVersion);
	CHECK(project.EngineVersion == FNK_VERSION);
	REQUIRE(File::Exists(backupFile.string()));
	CHECK(File::ReadAllText(backupFile.string()) == legacyJson);

	project.BuildWidth = 2560;
	project.EngineVersion = "0.0.0-stale";
	REQUIRE(project.Save());
	CHECK(project.EngineVersion == FNK_VERSION);
	Json::Value saved;
	REQUIRE(Json::TryParse(File::ReadAllText(projectFile.string()), saved, &error));
	REQUIRE(saved.IsObject());
	REQUIRE(saved.FindMember("projectFormatVersion"));
	CHECK(saved.FindMember("projectFormatVersion")->AsIntOr(-1)
		== FunkeProject::k_CurrentProjectFormatVersion);
	REQUIRE(saved.FindMember("engineVersion"));
	CHECK(saved.FindMember("engineVersion")->AsStringOr() == FNK_VERSION);
	const Json::Value* thirdParty = saved.FindMember("thirdPartySettings");
	REQUIRE(thirdParty);
	REQUIRE(thirdParty->IsObject());
	REQUIRE(thirdParty->FindMember("mode"));
	CHECK(thirdParty->FindMember("mode")->AsStringOr() == "kept");
	const Json::Value* graphics = saved.FindMember("graphics");
	REQUIRE(graphics);
	REQUIRE(graphics->IsObject());
	REQUIRE(graphics->FindMember("vendorMode"));
	CHECK(graphics->FindMember("vendorMode")->AsStringOr() == "kept");
	FunkeProject reloaded;
	REQUIRE(FunkeProject::TryLoad(root.string(), reloaded, &error));
	CHECK_FALSE(File::Exists((root / "project.json.pre-v0.1.bak").string()));
}

TEST_CASE("FunkeProject can read legacy runtime data without mutating the package") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkeProjectReadOnly_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	const fs::path projectFile = root / "project.json";
	const std::string legacyJson = "{\"name\":\"Packaged Legacy\"}";
	REQUIRE(File::WriteAllText(projectFile.string(), legacyJson));
	FunkeProject project;
	std::string error;
	REQUIRE(FunkeProject::TryLoad(root.string(), project, &error, false));
	CHECK(project.ProjectFormatVersion == 0);
	CHECK(File::ReadAllText(projectFile.string()) == legacyJson);
	CHECK_FALSE(File::Exists((root / "project.json.pre-v0.bak").string()));
}

TEST_CASE("FunkeProject migrates managed tooling metadata for net10") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkeManagedTooling_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	const std::string originalGlobal =
		"{\"sdk\":{\"version\":\"9.0.100\",\"custom\":\"kept\"},\"customRoot\":true}";
	REQUIRE(File::WriteAllText((root / "project.json").string(),
		"{\"projectFormatVersion\":1,\"name\":\"ManagedTools\",\"engineVersion\":\"old\"}"));
	REQUIRE(File::WriteAllText((root / "ManagedTools.csproj").string(),
		"<Project Sdk=\"Microsoft.NET.Sdk\"><PropertyGroup>"
		"<TargetFramework>net10.0</TargetFramework></PropertyGroup></Project>"));
	REQUIRE(File::WriteAllText((root / "ManagedTools.sln").string(),
		"Microsoft Visual Studio Solution File, Format Version 12.00\n"
		"# Visual Studio Version 17\n"));
	REQUIRE(File::WriteAllText((root / "global.json").string(), originalGlobal));

	FunkeProject project;
	std::string error;
	REQUIRE(FunkeProject::TryLoad(root.string(), project, &error));
	CHECK(File::ReadAllText((root / "ManagedTools.sln").string())
		.find("# Visual Studio Version 18") != std::string::npos);
	CHECK(File::ReadAllText((root / "ManagedTools.csproj").string())
		.find("Packages/FunkeDefines.props") != std::string::npos);
	CHECK(File::Exists((root / "Packages" / "FunkeDefines.props").string()));
	CHECK(File::ReadAllText((root / "global.json.pre-index-net10.bak").string()) == originalGlobal);

	Json::Value global;
	REQUIRE(Json::TryParse(File::ReadAllText((root / "global.json").string()), global, &error));
	const Json::Value* sdk = global.FindMember("sdk");
	REQUIRE(sdk != nullptr);
	const Json::Value* version = sdk->FindMember("version");
	const Json::Value* rollForward = sdk->FindMember("rollForward");
	const Json::Value* allowPrerelease = sdk->FindMember("allowPrerelease");
	const Json::Value* custom = sdk->FindMember("custom");
	const Json::Value* customRoot = global.FindMember("customRoot");
	REQUIRE(version != nullptr);
	REQUIRE(rollForward != nullptr);
	REQUIRE(allowPrerelease != nullptr);
	REQUIRE(custom != nullptr);
	REQUIRE(customRoot != nullptr);
	CHECK(version->AsStringOr() == "10.0.100");
	CHECK(rollForward->AsStringOr() == "latestFeature");
	CHECK_FALSE(allowPrerelease->AsBoolOr(true));
	CHECK(custom->AsStringOr() == "kept");
	CHECK(customRoot->AsBoolOr(false));
}

TEST_CASE("FunkeProject refuses to overwrite a project upgraded on disk") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkeProjectFutureSave_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	FunkeProject project;
	project.Name = "Future Save";
	project.RootDirectory = root.string();
	project.ProjectFilePath = (root / "project.json").string();
	REQUIRE(project.Save());

	const std::string futureJson =
		"{\"projectFormatVersion\":"
		+ std::to_string(FunkeProject::k_CurrentProjectFormatVersion + 1)
		+ ",\"name\":\"Future Save\",\"futureOnly\":true}";
	REQUIRE(File::WriteAllText(project.ProjectFilePath, futureJson));
	CHECK_FALSE(project.Save());
	CHECK(File::ReadAllText(project.ProjectFilePath) == futureJson);
}

TEST_CASE("FunkeProject scene lookup fails on duplicate basenames") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkeSceneLookup_" + std::to_string(suffix));
	const fs::path assets = root / "Assets";
	fs::create_directories(assets / "One");
	fs::create_directories(assets / "Two");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	REQUIRE(File::WriteAllText((assets / "One" / "Shared.scene").string(), "{}"));
	REQUIRE(File::WriteAllText((assets / "Two" / "shared.scene").string(), "{}"));

	FunkeProject project;
	project.AssetsDirectory = assets.string();
	project.ScenesDirectory = (assets / "Scenes").string();
	CHECK(project.GetSceneFilePath("Shared").empty());
}

// Shipped builds: unset overlays follow the build profile (Release = off), explicit values win and round-trip.
TEST_CASE("Runtime overlays default to off for Release builds unless project.json sets them") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("FunkeOverlayRT_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code ec; fs::remove_all(Root, ec); }
	} cleanup{ root };

	FunkeProject p;
	p.Name = "Overlay RT";
	p.RootDirectory = root.string();
	p.ProjectFilePath = Path::Combine(root.string(), "project.json");

	CHECK(p.ResolveShowRuntimeStats());
	CHECK(p.ResolveShowRuntimeLogs());
	p.ActiveBuildProfile = FunkeProject::BuildProfile::Release;
	CHECK_FALSE(p.ResolveShowRuntimeStats());
	CHECK_FALSE(p.ResolveShowRuntimeLogs());

	SUBCASE("unset stays unset") {
		REQUIRE(p.Save());
		FunkeProject loaded = FunkeProject::Load(root.string());
		CHECK_FALSE(loaded.ShowRuntimeStats.has_value());
		CHECK_FALSE(loaded.ResolveShowRuntimeStats());
	}

	SUBCASE("explicit true overrides the Release default") {
		p.ShowRuntimeStats = true;
		p.ShowRuntimeLogs = false;
		REQUIRE(p.Save());
		FunkeProject loaded = FunkeProject::Load(root.string());
		CHECK(loaded.ResolveShowRuntimeStats());
		CHECK_FALSE(loaded.ResolveShowRuntimeLogs());
		loaded.ActiveBuildProfile = FunkeProject::BuildProfile::Development;
		CHECK_FALSE(loaded.ResolveShowRuntimeLogs());
	}
}
