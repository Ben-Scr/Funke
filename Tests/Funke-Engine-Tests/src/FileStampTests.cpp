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

using namespace Funke;

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
	TempDir dir("FunkeFileStampTest");
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

TEST_CASE("FileStamp preserves filesystem results and observes each new file state")
{
	TempDir dir("FunkeStampSnapshot");
	const auto reference = [](const fs::path& path) {
		FileStamp stamp;
		std::error_code error;
		stamp.WriteTime = fs::last_write_time(path, error);
		if (error) return FileStamp{};
		stamp.Size = fs::file_size(path, error);
		if (error) return FileStamp{};
		stamp.Exists = true;
		return stamp;
	};
	const auto check = [&](const fs::path& path) {
		CHECK(FileStamp::Read(Path::ToUtf8(path)) == reference(path));
	};
	const auto file = dir.Root / Path::FromUtf8("größe.txt");
	CHECK(FileStamp::Read("") == FileStamp{});
	check(file);
	check(dir.Root);
	REQUIRE(File::WriteAllText(Path::ToUtf8(file), "one"));
	check(file);
	const auto before = FileStamp::Read(Path::ToUtf8(file));
	REQUIRE(before.Exists);
	CHECK(before.Size == 3);
	REQUIRE(File::WriteAllText(Path::ToUtf8(file), "longer contents"));
	fs::last_write_time(file, before.WriteTime);
	check(file);
	CHECK(FileStamp::Read(Path::ToUtf8(file)) != before);
	const auto replacement = dir.Root / "replacement.txt";
	REQUIRE(File::WriteAllText(Path::ToUtf8(replacement), "replacement"));
	fs::remove(file);
	fs::rename(replacement, file);
	check(file);
	const auto alias = dir.Root / "alias.txt";
	fs::create_hard_link(file, alias);
	check(alias);
	CHECK(FileStamp::Read(Path::ToUtf8(alias)) == FileStamp::Read(Path::ToUtf8(file)));
	std::error_code error;
	const auto link = dir.Root / "symbolic.txt";
	fs::create_symlink(file, link, error);
	if (!error) {
		check(link);
		CHECK(FileStamp::Read(Path::ToUtf8(link)) == FileStamp::Read(Path::ToUtf8(file)));
		fs::remove(file);
		check(link);
	}
	else {
		MESSAGE("Symbolic-link check unavailable: " << error.message());
		fs::remove(file);
	}
	check(file);
	check(alias);
}

TEST_CASE("Scene save and load record the file stamp; external writes show up as changes")
{
	TempDir dir("FunkeSceneStampTest");
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
	TempDir dir("FunkeConvertStampTest");
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
