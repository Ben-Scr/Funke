#include <doctest/doctest.h>

#include "Assets/AssetRegistry.hpp"
#include "Editor/BuildAssetPruning.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using Index::BuildAssetPruning::FindUnreferencedBuiltInFonts;

namespace {
	struct ScopedPruningFixture {
		std::filesystem::path Root;
		std::filesystem::path IndexAssets;
		std::filesystem::path Project;

		ScopedPruningFixture() {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Root = std::filesystem::temp_directory_path() / ("IndexBuildAssetPruning_" + std::to_string(suffix));
			IndexAssets = Root / "IndexAssets";
			Project = Root / "Project";
			std::filesystem::create_directories(IndexAssets / "Fonts");
			std::filesystem::create_directories(Project / "Assets" / "Scenes");
			for (const char* font : { "GoogleSans-Regular.ttf", "Inter_28pt-Regular.ttf", "Creepster-Regular.ttf" }) {
				Write(IndexAssets / "Fonts" / font, "font");
			}
			Write(Project / "project.json", R"({ "name": "Test" })");
		}

		~ScopedPruningFixture() {
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}

		static void Write(const std::filesystem::path& path, const std::string& content) {
			std::filesystem::create_directories(path.parent_path());
			std::ofstream(path, std::ios::binary) << content;
		}

		static std::string GuidOf(const char* relative) {
			return std::to_string(Index::AssetRegistry::ComputeBuiltInPathGuid(relative));
		}

		std::vector<std::string> Unused(const std::vector<std::filesystem::path>& skipped = {}) const {
			std::vector<std::string> names;
			for (const auto& p : FindUnreferencedBuiltInFonts(IndexAssets, Project, skipped)) {
				names.push_back(p.generic_string());
			}
			std::sort(names.begin(), names.end());
			return names;
		}
	};
}

TEST_CASE("Unreferenced built-in fonts are pruned, the default font never is") {
	ScopedPruningFixture f;
	CHECK(f.Unused() == std::vector<std::string>{ "Fonts/Creepster-Regular.ttf", "Fonts/Inter_28pt-Regular.ttf" });
}

TEST_CASE("A font GUID in a text scene keeps the font") {
	ScopedPruningFixture f;
	f.Write(f.Project / "Assets" / "Scenes" / "Main.scene",
		R"({ "fontAsset": ")" + f.GuidOf("Fonts/Inter_28pt-Regular.ttf") + R"(" })");
	CHECK(f.Unused() == std::vector<std::string>{ "Fonts/Creepster-Regular.ttf" });
}

TEST_CASE("A font GUID inside binary data keeps the font") {
	ScopedPruningFixture f;
	const uint64_t guid = Index::AssetRegistry::ComputeBuiltInPathGuid("Fonts/Creepster-Regular.ttf");
	std::string bytes("\x00\x01\x02", 3);
	bytes.append(reinterpret_cast<const char*>(&guid), sizeof(guid));
	f.Write(f.Project / "Assets" / "Scenes" / "Main.scene", bytes);
	CHECK(f.Unused() == std::vector<std::string>{ "Fonts/Inter_28pt-Regular.ttf" });
}

TEST_CASE("A font file name in a script keeps the font regardless of case") {
	ScopedPruningFixture f;
	f.Write(f.Project / "Assets" / "Scripts" / "Hud.cs", R"(Font.Load("IndexAssets/Fonts/CREEPSTER-regular.ttf");)");
	CHECK(f.Unused() == std::vector<std::string>{ "Fonts/Inter_28pt-Regular.ttf" });
}

TEST_CASE("Build outputs and caches are not treated as references") {
	ScopedPruningFixture f;
	const std::string reference = R"({ "fontAsset": ")" + f.GuidOf("Fonts/Creepster-Regular.ttf") + R"(" })";
	f.Write(f.Project / "bin" / "Debug" / "Old.scene", reference);
	f.Write(f.Project / "Builds" / "Windows" / "Assets" / "Old.scene", reference);
	CHECK(f.Unused({ f.Project / "Builds" / "Windows" }).size() == 2);
}

TEST_CASE("A missing project root prunes nothing") {
	ScopedPruningFixture f;
	std::filesystem::remove_all(f.Project);
	CHECK(f.Unused().empty());
}
