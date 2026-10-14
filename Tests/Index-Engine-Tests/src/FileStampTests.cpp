#include <doctest/doctest.h>

#include "Scene/Scene.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/SceneSerializerShared.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>

using namespace Index;

namespace {
	namespace fs = std::filesystem;

	struct TempDir {
		fs::path Root;
		explicit TempDir(const char* name) {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Root = fs::temp_directory_path() / (std::string(name) + "_" + std::to_string(suffix));
			fs::create_directories(Root);
		}
		~TempDir() {
			std::error_code ec;
			fs::remove_all(Root, ec);
		}
		std::string File(const char* name) const { return Path::ToUtf8(Root / name); }
	};
}

TEST_CASE("KnownFileStamps tells external edits apart from recorded state")
{
	TempDir dir("IndexFileStampTest");
	const std::string path = dir.File("clip.txt");
	REQUIRE(File::WriteAllText(path, "one"));

	CHECK_FALSE(KnownFileStamps::HasChangedOnDisk(path)); // never recorded

	KnownFileStamps::Record(path);
	CHECK_FALSE(KnownFileStamps::HasChangedOnDisk(path));

	REQUIRE(File::WriteAllText(path, "changed by another tool"));
	CHECK(KnownFileStamps::HasChangedOnDisk(path));

	KnownFileStamps::Record(path);
	CHECK_FALSE(KnownFileStamps::HasChangedOnDisk(path));

	std::error_code ec;
	fs::remove(Path::FromUtf8(path), ec);
	CHECK(KnownFileStamps::HasChangedOnDisk(path));

	KnownFileStamps::Forget(path);
	CHECK_FALSE(KnownFileStamps::HasChangedOnDisk(path));
}

TEST_CASE("Scene save and load record the file stamp; external writes show up as changes")
{
	TempDir dir("IndexSceneStampTest");
	const std::string path = dir.File("Level.scene");

	auto scene = Scene::CreateDetachedScene("Level");
	scene->CreateEntity("Hero");
	REQUIRE(SceneSerializer::SaveToFile(*scene, path, SceneSerializationFormat::Json, false));
	CHECK_FALSE(KnownFileStamps::HasChangedOnDisk(path));

	std::string text = File::ReadAllText(path);
	text += "\n";
	REQUIRE(File::WriteAllText(path, text));
	CHECK(KnownFileStamps::HasChangedOnDisk(path));

	auto reloaded = Scene::CreateDetachedScene("Level");
	REQUIRE(SceneSerializer::LoadFromFile(*reloaded, path));
	CHECK_FALSE(KnownFileStamps::HasChangedOnDisk(path));
}

TEST_CASE("Format conversion keeps a current stamp current but never hides a pending external edit")
{
	TempDir dir("IndexConvertStampTest");
	const std::string path = dir.File("Level.scene");

	auto scene = Scene::CreateDetachedScene("Level");
	scene->CreateEntity("Hero");
	REQUIRE(SceneSerializer::SaveToFile(*scene, path, SceneSerializationFormat::Json, false));

	REQUIRE(SceneSerializer::ConvertFileFormat(path, SceneSerializationFormat::Binary));
	CHECK_FALSE(KnownFileStamps::HasChangedOnDisk(path));

	REQUIRE(SceneSerializer::ConvertFileFormat(path, SceneSerializationFormat::Json));
	std::string text = File::ReadAllText(path);
	text += "\n";
	REQUIRE(File::WriteAllText(path, text));
	REQUIRE(SceneSerializer::ConvertFileFormat(path, SceneSerializationFormat::Binary));
	CHECK(KnownFileStamps::HasChangedOnDisk(path));
}
