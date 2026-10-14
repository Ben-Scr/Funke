#include <doctest/doctest.h>

#include "Build/GameBuilder.hpp"
#include "Project/IndexBuildProfile.hpp"
#include "Project/IndexProject.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <chrono>
#include <filesystem>
#include <string>

using namespace Index;

namespace {
	struct TempProject {
		TempProject() {
			Root = std::filesystem::temp_directory_path()
				/ ("IndexGameBuilderTest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(Root / "BuildProfiles");
			Project.Name = "Test";
			Project.RootDirectory = Path::ToUtf8(Root);
		}
		~TempProject() {
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		void AddProfile(const std::string& name, BuildPlatform platform) {
			IndexBuildProfile profile;
			profile.Name = name;
			profile.Platform = platform;
			REQUIRE(profile.Save(Path::ToUtf8(Root / "BuildProfiles" / (name + ".build"))));
		}

		std::filesystem::path Root;
		IndexProject Project;
	};
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
