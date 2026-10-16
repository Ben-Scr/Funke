#include <doctest/doctest.h>

#include "Gui/AssetSaveDialog.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Index;
namespace fs = std::filesystem;

namespace {
	struct ScopedTempDir {
		fs::path Root;
		ScopedTempDir() {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Root = fs::temp_directory_path() / ("IndexAssetSaveDialog_" + std::to_string(suffix));
			fs::create_directories(Root / "Assets" / "Characters");
		}
		~ScopedTempDir() {
			std::error_code ec;
			fs::remove_all(Root, ec);
		}
		void Touch(const fs::path& path) const { std::ofstream(path, std::ios::binary) << "{}"; }
	};
}

TEST_CASE("AssetSaveDialog::UniquePath numbers taken names") {
	ScopedTempDir dir;
	const fs::path folder = dir.Root / "Assets" / "Characters";
	CHECK(AssetSaveDialog::UniquePath(folder, "Player", ".anim") == folder / "Player.anim");
	dir.Touch(folder / "Player.anim");
	dir.Touch(folder / "Player 1.anim");
	CHECK(AssetSaveDialog::UniquePath(folder, "Player", ".anim") == folder / "Player 2.anim");
}

TEST_CASE("AssetSaveDialog::WithExtension appends a missing extension without replacing existing files") {
	ScopedTempDir dir;
	const fs::path folder = dir.Root / "Assets" / "Characters";
	CHECK(AssetSaveDialog::WithExtension(folder / "Walk", ".anim") == folder / "Walk.anim");
	CHECK(AssetSaveDialog::WithExtension(folder / "Walk.v2", ".anim") == folder / "Walk.v2.anim");

	// Typed with the extension: the dialog already asked about overwriting.
	dir.Touch(folder / "Walk.anim");
	CHECK(AssetSaveDialog::WithExtension(folder / "Walk.anim", ".anim") == folder / "Walk.anim");
	CHECK(AssetSaveDialog::WithExtension(folder / "Walk.ANIM", ".anim") == folder / "Walk.ANIM");

	// Typed without it: the prompt never covered Walk.anim.
	CHECK(AssetSaveDialog::WithExtension(folder / "Walk", ".anim") == folder / "Walk 1.anim");
}

TEST_CASE("AssetSaveDialog::IsInside accepts only paths below the folder") {
	ScopedTempDir dir;
	const fs::path assets = dir.Root / "Assets";
	CHECK(AssetSaveDialog::IsInside(assets / "New.anim", assets));
	CHECK(AssetSaveDialog::IsInside(assets / "Characters" / "New.anim", assets));
	CHECK(AssetSaveDialog::IsInside(assets / "NotYetCreated" / "New.anim", assets));

	CHECK_FALSE(AssetSaveDialog::IsInside(assets, assets));
	CHECK_FALSE(AssetSaveDialog::IsInside(dir.Root / "New.anim", assets));
	CHECK_FALSE(AssetSaveDialog::IsInside(assets / ".." / "New.anim", assets));
	CHECK_FALSE(AssetSaveDialog::IsInside(assets / "Characters" / ".." / ".." / "New.anim", assets));
	// Shares the prefix "Assets" but is a sibling folder.
	CHECK_FALSE(AssetSaveDialog::IsInside(dir.Root / "AssetsBackup" / "New.anim", assets));
}
