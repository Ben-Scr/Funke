#include <doctest/doctest.h>

#include "Assets/AssetRegistry.hpp"
#include "Editor/BuildAssetPruning.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializerStorage.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Funke;

namespace {
	struct ScopedPruningFixture {
		std::filesystem::path Root;
		std::filesystem::path FunkeAssets;
		std::filesystem::path Project;
		std::filesystem::path Assets;
		std::vector<std::string> BuildScenes{ "Main" };

		ScopedPruningFixture() {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Root = std::filesystem::temp_directory_path() / ("FunkeBuildAssetPruning_" + std::to_string(suffix));
			FunkeAssets = Root / "FunkeAssets";
			Project = Root / "Project";
			Assets = Project / "Assets";
			for (const char* file : { "Fonts/IBMPlexSansCondensed-Regular.ttf", "Fonts/Creepster-Regular.ttf", "Fonts/LICENSES.md",
				"Textures/Default/Square.png", "Textures/Default/Spike.png", "Textures/Editor/Play/play_32.png",
				"Localization/en.json", "Localization/de.json", "Localization/languages.json", "Localization/README.md",
				"Shader/2D/sprite.vert.glsl" }) {
				Write(FunkeAssets / file, "x");
			}
			Write(Project / "project.json", R"({ "name": "Test", "buildScenes": ["Main"] })");
			Write(Assets / "Scenes" / "Main.scene", R"({ "entities": [] })");
		}

		~ScopedPruningFixture() {
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		static void Write(const std::filesystem::path& path, const std::string& content) {
			std::filesystem::create_directories(path.parent_path());
			std::ofstream(path, std::ios::binary) << content;
		}

		// Creates the asset and its .meta; returns the GUID as written in content.
		std::string AddAsset(const std::string& relative, uint64_t guid, const std::string& content = "x") const {
			Write(Assets / relative, content);
			Write(Assets / (relative + ".meta"), R"({ "AssetGUID": ")" + std::to_string(guid) + R"(", "kind": 1 })");
			return std::to_string(guid);
		}

		static std::string BuiltInGuid(const char* relative) {
			return std::to_string(AssetRegistry::ComputeBuiltInPathGuid(relative));
		}

		BuildAssetPruning::Plan Compute(std::vector<std::filesystem::path> skipped = {}) const {
			return BuildAssetPruning::ComputeShippedAssets({ Project, Assets, FunkeAssets, BuildScenes, std::move(skipped) });
		}
	};
}

TEST_CASE("Only content reachable from build scenes ships") {
	ScopedPruningFixture f;
	const std::string used = f.AddAsset("Textures/Used.png", 1111111111111111111ull);
	f.AddAsset("Textures/Unused.png", 2222222222222222222ull);
	f.Write(f.Assets / "Scenes" / "Main.scene", R"({ "textureAsset": ")" + used + R"(" })");
	f.Write(f.Assets / "Scenes" / "Other.scene", "{}");
	f.Write(f.Assets / "Scripts" / "Player.cs", "class Player {}");

	const BuildAssetPruning::Plan plan = f.Compute();
	REQUIRE(plan.Valid);
	CHECK(plan.KeepsProjectFile("Scenes/Main.scene"));
	CHECK(plan.KeepsProjectFile("Textures/Used.png"));
	CHECK(plan.KeepsProjectFile("Textures/Used.png.meta"));
	CHECK_FALSE(plan.KeepsProjectFile("Textures/Unused.png"));
	CHECK_FALSE(plan.KeepsProjectFile("Textures/Unused.png.meta"));
	CHECK_FALSE(plan.KeepsProjectFile("Scenes/Other.scene"));
	CHECK_FALSE(plan.KeepsProjectFile("Scripts/Player.cs"));
}

TEST_CASE("References are followed through prefabs and data assets") {
	ScopedPruningFixture f;
	const std::string icon = f.AddAsset("Icons/Coin.png", 3333333333333333333ull);
	const std::string item = f.AddAsset("Data/Coin.dataasset", 4444444444444444444ull,
		R"({ "type": "Item", "fields": { "Icons": "\n)" + icon + R"(" } })");
	const std::string prefab = f.AddAsset("Prefabs/Coin.prefab", 5555555555555555555ull,
		R"({ "ScriptFields": { "Coin": [ { "type": "dataasset:Item", "value": ")" + item + R"(" } ] } })");
	f.Write(f.Assets / "Scenes" / "Main.scene", R"({ "spawn": "prefab:)" + prefab + R"(" })");

	const BuildAssetPruning::Plan plan = f.Compute();
	REQUIRE(plan.Valid);
	CHECK(plan.KeepsProjectFile("Prefabs/Coin.prefab"));
	CHECK(plan.KeepsProjectFile("Data/Coin.dataasset"));
	CHECK(plan.KeepsProjectFile("Icons/Coin.png"));
}

TEST_CASE("Binary scenes are scanned through the serializer") {
	ScopedPruningFixture f;
	const std::string clip = f.AddAsset("Audio/Hit.wav", 6666666666666666666ull);
	Json::Value root = Json::Value::MakeObject();
	root.AddMember("clipAsset", clip);
	REQUIRE(SceneSerializerStorage::WriteRootToFile((f.Assets / "Scenes" / "Main.scene").string(), root,
		SceneSerializationFormat::Binary));

	const BuildAssetPruning::Plan plan = f.Compute();
	REQUIRE(plan.Valid);
	CHECK(plan.KeepsProjectFile("Audio/Hit.wav"));
}

TEST_CASE("Folders and files named in C# literals ship") {
	ScopedPruningFixture f;
	f.Write(f.Assets / "Maps" / "Official" / "01.json", "{}");
	f.Write(f.Assets / "Maps" / "Official" / "02.json", "{}");
	f.Write(f.Assets / "Maps" / "Drafts" / "03.json", "{}");
	f.Write(f.Assets / "Config" / "Balance.json", "{}");
	f.Write(f.Assets / "Scenes" / "Credits.scene", "{}");
	f.Write(f.Assets / "Scripts" / "Catalog.cs", R"(
		// don't confuse the lexer
		char quote = '"';
		var dir = Path.Combine(Application.DataPath, "Maps\\Official");
		var cfg = $"{Application.DataPath}/Config/Balance.json";
		SceneManager.LoadScene(@"Credits");
	)");

	const BuildAssetPruning::Plan plan = f.Compute();
	REQUIRE(plan.Valid);
	CHECK(plan.KeepsProjectFile("Maps/Official/01.json"));
	CHECK(plan.KeepsProjectFile("Maps/Official/02.json"));
	CHECK_FALSE(plan.KeepsProjectFile("Maps/Drafts/03.json"));
	CHECK(plan.KeepsProjectFile("Config/Balance.json"));
	CHECK(plan.KeepsProjectFile("Scenes/Credits.scene"));
}

TEST_CASE("FindAll keeps every asset of its type under the prefix") {
	ScopedPruningFixture f;
	f.AddAsset("Textures/Icons/A.png", 7000000000000000001ull);
	f.AddAsset("Textures/Other/B.png", 7000000000000000002ull);
	f.AddAsset("Audio/C.wav", 7000000000000000003ull);
	f.Write(f.Assets / "Scripts" / "Icons.cs", R"(var icons = AssetManager.FindAll<Texture>("Textures/Icons");)");

	BuildAssetPruning::Plan plan = f.Compute();
	REQUIRE(plan.Valid);
	CHECK(plan.KeepsProjectFile("Textures/Icons/A.png"));
	CHECK_FALSE(plan.KeepsProjectFile("Textures/Other/B.png"));

	f.Write(f.Assets / "Scripts" / "Icons.cs", "var all = AssetManager.FindAll<Texture>(folder);");
	plan = f.Compute();
	REQUIRE(plan.Valid);
	CHECK(plan.KeepsProjectFile("Textures/Other/B.png"));
	CHECK_FALSE(plan.KeepsProjectFile("Audio/C.wav"));
}

TEST_CASE("StreamingAssets always ships") {
	ScopedPruningFixture f;
	f.Write(f.Assets / "StreamingAssets" / "Levels" / "1.txt", "x");
	const BuildAssetPruning::Plan plan = f.Compute();
	REQUIRE(plan.Valid);
	CHECK(plan.KeepsProjectFile("StreamingAssets/Levels/1.txt"));
}

TEST_CASE("glTF dependencies resolve relative segments and encoded filenames") {
	const std::vector<std::pair<std::string, std::string>> cases{
		{ "../Buffers/ship.bin", "Meshes/Buffers/ship.bin" },
		{ "../Buffers/./ship.bin", "Meshes/Buffers/ship.bin" },
		{ "./ship.bin", "Meshes/Ship/ship.bin" },
		{ "../Buffers/ship%20data.bin", "Meshes/Buffers/ship data.bin" },
		{ "../Buffers/ship%2520.bin", "Meshes/Buffers/ship%20.bin" },
	};
	for (const auto& [uri, path] : cases) {
		CAPTURE(uri);
		ScopedPruningFixture f;
		Json::Value buffer = Json::Value::MakeObject();
		buffer.AddMember("uri", uri);
		Json::Value buffers = Json::Value::MakeArray();
		buffers.Append(std::move(buffer));
		Json::Value model = Json::Value::MakeObject();
		model.AddMember("buffers", std::move(buffers));
		const auto guid = f.AddAsset("Meshes/Ship/ship.gltf", 129001ull, Json::Stringify(model));
		f.AddAsset(path, 129002ull);
		f.AddAsset("Meshes/Buffers/unused.bin", 129003ull);
		f.Write(f.Assets / "Scenes" / "Main.scene", R"({ "meshAsset": ")" + guid + R"(" })");
		const auto plan = f.Compute();
		REQUIRE(plan.Valid);
		CHECK(plan.KeepsProjectFile("Meshes/Ship/ship.gltf"));
		CHECK(plan.KeepsProjectFile(path));
		CHECK(plan.KeepsProjectFile(path + ".meta"));
		CHECK_FALSE(plan.KeepsProjectFile("Meshes/Buffers/unused.bin"));
	}
}

TEST_CASE("Game language files always ship") {
	ScopedPruningFixture f;
	f.AddAsset("Localization/en.json", 7777777777777777777ull, R"({ "strings": { "menu.play": "Play" } })");
	f.AddAsset("Localization/de.json", 7777777777777777778ull, R"({ "strings": { "menu.play": "Spielen" } })");
	f.AddAsset("Data/Unused.json", 7777777777777777779ull);
	const BuildAssetPruning::Plan plan = f.Compute();
	REQUIRE(plan.Valid);
	CHECK(plan.KeepsProjectFile("Localization/en.json"));
	CHECK(plan.KeepsProjectFile("Localization/de.json.meta"));
	CHECK_FALSE(plan.KeepsProjectFile("Data/Unused.json"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Localization/en.json"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Localization/de.json"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Localization/languages.json"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Localization/README.md"));
}

TEST_CASE("Built-in assets ship when loaded by fixed path or referenced") {
	ScopedPruningFixture f;
	f.Write(f.Assets / "Scenes" / "Main.scene",
		R"({ "fontAsset": ")" + f.BuiltInGuid("Fonts/Creepster-Regular.ttf") + R"(" })");

	const BuildAssetPruning::Plan plan = f.Compute();
	REQUIRE(plan.Valid);
	CHECK(plan.KeepsBuiltInFile("Fonts/IBMPlexSansCondensed-Regular.ttf"));
	CHECK(plan.KeepsBuiltInFile("Fonts/Creepster-Regular.ttf"));
	CHECK(plan.KeepsBuiltInFile("Fonts/LICENSES.md"));
	CHECK(plan.KeepsBuiltInFile("Textures/Default/Square.png"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Localization/en.json"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Textures/Default/Spike.png"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Textures/Editor/Play/play_32.png"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Shader/2D/sprite.vert.glsl"));
}

TEST_CASE("Project settings paths and index: references ship") {
	ScopedPruningFixture f;
	f.Write(f.Assets / "UI" / "Cursor.png", "x");
	f.Write(f.Project / "project.json", R"({ "buildScenes": ["Main"], "cursorImagePath": "Assets/UI/Cursor.png",
		"splashScreen": { "imagePath": "funke:Textures/Default/Spike.png" } })");

	const BuildAssetPruning::Plan plan = f.Compute();
	REQUIRE(plan.Valid);
	CHECK(plan.KeepsProjectFile("UI/Cursor.png"));
	CHECK(plan.KeepsBuiltInFile("Textures/Default/Spike.png"));
}

TEST_CASE("Launcher language files stay excluded when referenced") {
	ScopedPruningFixture f;
	f.Write(f.Assets / "Scenes" / "Main.scene",
		R"({ "languageAsset": ")" + f.BuiltInGuid("Localization/en.json") + R"(" })");
	f.Write(f.Assets / "Scripts" / "Languages.cs", R"(var path = "funke:Localization";)");
	const BuildAssetPruning::Plan plan = f.Compute();
	REQUIRE(plan.Valid);
	CHECK_FALSE(plan.KeepsBuiltInFile("Localization/en.json"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Localization/README.md"));
}

TEST_CASE("Old build outputs are not treated as references") {
	ScopedPruningFixture f;
	f.Write(f.Assets / "Maps" / "A.json", "{}");
	f.Write(f.Project / "Builds" / "Windows" / "Assets" / "Scripts" / "Old.cs", R"(var p = "Maps/A.json";)");
	f.Write(f.Project / "Out" / "Old.cs", R"(var p = "Maps/A.json";)");

	const BuildAssetPruning::Plan plan = f.Compute({ f.Project / "Out" });
	REQUIRE(plan.Valid);
	CHECK_FALSE(plan.KeepsProjectFile("Maps/A.json"));
}

TEST_CASE("An unscannable project ships everything") {
	ScopedPruningFixture f;
	f.BuildScenes = { "Missing" };
	const BuildAssetPruning::Plan plan = f.Compute();
	REQUIRE_FALSE(plan.Valid);
	CHECK(plan.KeepsBuiltInFile("Fonts/IBMPlexSansCondensed-Regular.ttf"));
	CHECK(plan.KeepsBuiltInFile("Textures/Default/Spike.png"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Textures/Editor/Play/play_32.png"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Localization/en.json"));
	CHECK_FALSE(plan.KeepsBuiltInFile("LOCALIZATION\\de.json"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Localization/languages.json"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Localization/README.md"));
	CHECK_FALSE(plan.KeepsBuiltInFile("Localization/en.json.meta"));

	ScopedPruningFixture g;
	std::filesystem::remove(g.Project / "project.json");
	CHECK_FALSE(g.Compute().Valid);
}

TEST_CASE("The registry manifest only lists shipped files") {
	ScopedPruningFixture f;
	const std::string used = f.AddAsset("Textures/Used.png", 1111111111111111111ull);
	f.AddAsset("Textures/Unused.png", 2222222222222222222ull);
	f.Write(f.Assets / "Scenes" / "Main.scene", R"({ "textureAsset": ")" + used + R"(" })");
	const BuildAssetPruning::Plan plan = f.Compute();
	REQUIRE(plan.Valid);

	const std::string manifest =
		"FNKAR1\n"
		"1111111111111111111\t1\t0\t0\tTextures/Used.png\n"
		"2222222222222222222\t1\t0\t0\tTextures/Unused.png\n";
	CHECK(BuildAssetPruning::FilterRegistryManifest(manifest, plan)
		== "FNKAR1\n1111111111111111111\t1\t0\t0\tTextures/Used.png\n");

	BuildAssetPruning::Plan invalid;
	CHECK(BuildAssetPruning::FilterRegistryManifest(manifest, invalid) == manifest);
}
