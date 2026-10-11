#include <doctest/doctest.h>

#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Core/Version.hpp"
#include "Project/IndexProject.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <chrono>
#include <filesystem>
#include <limits>
#include <stdexcept>

using namespace Index;

namespace {
	class TimeTestApplication final : public Application {};
}

// project.json round-trip for the default physics-world Hz setting. Mirrors the
// write-if-non-default serialization contract: a non-default value survives
// Save() -> Load(), the default is omitted and reconstructed, and the load-time
// clamp keeps out-of-range input inside Time::SetFixedDeltaTime's [1, 240] Hz range.
TEST_CASE("IndexProject round-trips physics tick rate through project.json") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("IndexPhysicsRT_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code ec; fs::remove_all(Root, ec); }
	} cleanup{ root };

	auto makeProject = [&](const char* name) {
		IndexProject p;
		p.Name = name;
		p.RootDirectory = root.string();
		p.ProjectFilePath = Path::Combine(root.string(), "project.json");
		return p;
	};

	SUBCASE("non-default value survives a round-trip") {
		IndexProject p = makeProject("Physics RT");
		p.PhysicsTickRate = 120;
		REQUIRE(p.Save());

		IndexProject loaded = IndexProject::Load(root.string());
		CHECK(loaded.PhysicsTickRate == 120);
	}

	SUBCASE("default is omitted and reconstructed") {
		IndexProject p = makeProject("Physics Defaults");
		// Leave PhysicsTickRate = 50 (default).
		REQUIRE(p.Save());

		IndexProject loaded = IndexProject::Load(root.string());
		CHECK(loaded.PhysicsTickRate == 50);
	}

	SUBCASE("out-of-range value is clamped on load") {
		IndexProject p = makeProject("Physics Clamp");
		p.PhysicsTickRate = 100000; // absurd; loader clamps to the [1, 240] Hz range
		REQUIRE(p.Save());

		IndexProject loaded = IndexProject::Load(root.string());
		CHECK(loaded.PhysicsTickRate == IndexProject::k_MaxPhysicsHz);
	}
}

// Guards the apply path itself, not just serialization: the Hz the panel/startup
// push into Time must round-trip to the expected fixed timestep. The Hz bounds are
// chosen to land exactly on Time::SetFixedDeltaTime's [1/240, 1]s clamp endpoints, so
// this also locks the IndexProject bounds and the Time clamp together.
TEST_CASE("PhysicsTickRate maps to the fixed timestep Time actually uses") {
	auto fixedStepFor = [](int hz) {
		IndexProject p;
		p.PhysicsTickRate = hz;
		TimeTestApplication application;
		Time::SetFixedDeltaTime(p.GetFixedDeltaTimeSeconds());
		return Time::GetFixedDeltaTime();
	};

	CHECK(fixedStepFor(50) == doctest::Approx(1.0f / 50.0f));
	CHECK(fixedStepFor(60) == doctest::Approx(1.0f / 60.0f));
	// Endpoints sit on Time's clamp bounds, so they pass through unchanged.
	CHECK(fixedStepFor(IndexProject::k_MaxPhysicsHz) == doctest::Approx(1.0f / 240.0f));
	CHECK(fixedStepFor(IndexProject::k_MinPhysicsHz) == doctest::Approx(1.0f));
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

TEST_CASE("IndexProject round-trips a non-default build resolution through project.json") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("IndexDisplayRT_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	IndexProject project;
	project.Name = "Display RT";
	project.RootDirectory = root.string();
	project.ProjectFilePath = Path::Combine(root.string(), "project.json");
	project.BuildWidth = 3440;
	project.BuildHeight = 1440;
	REQUIRE(project.Save());

	const IndexProject loaded = IndexProject::Load(root.string());
	CHECK(loaded.BuildWidth == 3440);
	CHECK(loaded.BuildHeight == 1440);
}

TEST_CASE("IndexProject rejects unreadable and newer project formats") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("IndexProjectReject_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	const fs::path projectFile = root / "project.json";
	IndexProject loaded;
	loaded.Name = "unchanged";
	std::string error;

	REQUIRE(File::WriteAllText(projectFile.string(), "{ invalid json"));
	CHECK_FALSE(IndexProject::TryLoad(root.string(), loaded, &error));
	CHECK_FALSE(error.empty());
	CHECK(loaded.Name == "unchanged");
	REQUIRE(File::WriteAllText(projectFile.string(),
		"{\"projectFormatVersion\":1,\"name\":42}"));
	CHECK_FALSE(IndexProject::TryLoad(root.string(), loaded, &error));
	CHECK(error.find("name") != std::string::npos);
	REQUIRE(File::WriteAllText(projectFile.string(),
		"{\"projectFormatVersion\":1,\"name\":\"Invalid\",\"engineVersion\":\"old\","
		"\"graphics\":{\"renderScale\":\"fast\"}}"));
	CHECK_FALSE(IndexProject::TryLoad(root.string(), loaded, &error));
	CHECK(error.find("graphics.renderScale") != std::string::npos);

	const std::string newerJson =
		"{\"projectFormatVersion\":"
		+ std::to_string(IndexProject::k_CurrentProjectFormatVersion + 1)
		+ ",\"name\":\"Future\"}";
	REQUIRE(File::WriteAllText(projectFile.string(), newerJson));
	CHECK_FALSE(IndexProject::TryLoad(root.string(), loaded, &error));
	CHECK(error.find("newer") != std::string::npos);
	CHECK_THROWS_AS(IndexProject::Load(root.string()), std::runtime_error);
	CHECK(File::ReadAllText(projectFile.string()) == newerJson);
}

TEST_CASE("IndexProject rejects project names that escape derived paths") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path container = fs::temp_directory_path() / ("IndexProjectNameReject_" + std::to_string(suffix));
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

	IndexProject project;
	std::string error;
	CHECK_FALSE(IndexProject::TryLoad(root.string(), project, &error));
	CHECK(error.find("project name") != std::string::npos);
	CHECK(File::ReadAllText(victim.string()) == originalVictim);
	CHECK_FALSE(IndexProject::IsValidProjectName("CON.txt"));
	CHECK_FALSE(IndexProject::IsValidProjectName("Editor."));
}

TEST_CASE("IndexProject migrates legacy JSON with a backup and preserves unknown members") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("IndexProjectMigration_" + std::to_string(suffix));
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

	IndexProject project;
	std::string error;
	REQUIRE(IndexProject::TryLoad(root.string(), project, &error));
	CHECK(project.ProjectFormatVersion == IndexProject::k_CurrentProjectFormatVersion);
	CHECK(project.EngineVersion == IDX_VERSION);
	REQUIRE(File::Exists(backupFile.string()));
	CHECK(File::ReadAllText(backupFile.string()) == legacyJson);

	project.BuildWidth = 2560;
	project.EngineVersion = "0.0.0-stale";
	REQUIRE(project.Save());
	CHECK(project.EngineVersion == IDX_VERSION);
	Json::Value saved;
	REQUIRE(Json::TryParse(File::ReadAllText(projectFile.string()), saved, &error));
	REQUIRE(saved.IsObject());
	REQUIRE(saved.FindMember("projectFormatVersion"));
	CHECK(saved.FindMember("projectFormatVersion")->AsIntOr(-1)
		== IndexProject::k_CurrentProjectFormatVersion);
	REQUIRE(saved.FindMember("engineVersion"));
	CHECK(saved.FindMember("engineVersion")->AsStringOr() == IDX_VERSION);
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
	IndexProject reloaded;
	REQUIRE(IndexProject::TryLoad(root.string(), reloaded, &error));
	CHECK_FALSE(File::Exists((root / "project.json.pre-v0.1.bak").string()));
}

TEST_CASE("IndexProject can read legacy runtime data without mutating the package") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("IndexProjectReadOnly_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	const fs::path projectFile = root / "project.json";
	const std::string legacyJson = "{\"name\":\"Packaged Legacy\"}";
	REQUIRE(File::WriteAllText(projectFile.string(), legacyJson));
	IndexProject project;
	std::string error;
	REQUIRE(IndexProject::TryLoad(root.string(), project, &error, false));
	CHECK(project.ProjectFormatVersion == 0);
	CHECK(File::ReadAllText(projectFile.string()) == legacyJson);
	CHECK_FALSE(File::Exists((root / "project.json.pre-v0.bak").string()));
}

TEST_CASE("IndexProject migrates managed tooling metadata for net10") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("IndexManagedTooling_" + std::to_string(suffix));
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

	IndexProject project;
	std::string error;
	REQUIRE(IndexProject::TryLoad(root.string(), project, &error));
	CHECK(File::ReadAllText((root / "ManagedTools.sln").string())
		.find("# Visual Studio Version 18") != std::string::npos);
	CHECK(File::ReadAllText((root / "ManagedTools.csproj").string())
		.find("Packages/IndexDefines.props") != std::string::npos);
	CHECK(File::Exists((root / "Packages" / "IndexDefines.props").string()));
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

TEST_CASE("IndexProject refuses to overwrite a project upgraded on disk") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("IndexProjectFutureSave_" + std::to_string(suffix));
	fs::create_directories(root / "Assets");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	IndexProject project;
	project.Name = "Future Save";
	project.RootDirectory = root.string();
	project.ProjectFilePath = (root / "project.json").string();
	REQUIRE(project.Save());

	const std::string futureJson =
		"{\"projectFormatVersion\":"
		+ std::to_string(IndexProject::k_CurrentProjectFormatVersion + 1)
		+ ",\"name\":\"Future Save\",\"futureOnly\":true}";
	REQUIRE(File::WriteAllText(project.ProjectFilePath, futureJson));
	CHECK_FALSE(project.Save());
	CHECK(File::ReadAllText(project.ProjectFilePath) == futureJson);
}

TEST_CASE("IndexProject scene lookup fails on duplicate basenames") {
	namespace fs = std::filesystem;
	const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path root = fs::temp_directory_path() / ("IndexSceneLookup_" + std::to_string(suffix));
	const fs::path assets = root / "Assets";
	fs::create_directories(assets / "One");
	fs::create_directories(assets / "Two");
	struct Cleanup {
		fs::path Root;
		~Cleanup() { std::error_code error; fs::remove_all(Root, error); }
	} cleanup{ root };

	REQUIRE(File::WriteAllText((assets / "One" / "Shared.scene").string(), "{}"));
	REQUIRE(File::WriteAllText((assets / "Two" / "shared.scene").string(), "{}"));

	IndexProject project;
	project.AssetsDirectory = assets.string();
	project.ScenesDirectory = (assets / "Scenes").string();
	CHECK(project.GetSceneFilePath("Shared").empty());
}
