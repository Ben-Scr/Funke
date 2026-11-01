#include <doctest/doctest.h>
#include "Assets/AssetBundle.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Audio/Audio.hpp"
#include "Graphics/ImageProcessing.hpp"
#include "Graphics/ImageData.hpp"
#include "Graphics/Texture2D.hpp"
#include "Graphics/Text/Font.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"
#include <chrono>
#include <future>
#include <fstream>

using namespace Funke;

namespace {
	struct BundleFixture {
		std::filesystem::path Root = std::filesystem::temp_directory_path() / ("funke-bundles-"
			+ std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		BundleFixture() { std::filesystem::create_directories(Root / "Assets"); }
		~BundleFixture() {
			ProjectManager::SetCurrentProject(nullptr);
			AssetBundle::UnmountAll();
			std::error_code error;
			std::filesystem::remove_all(Root, error);
		}
		void Write(const std::string& name, const std::string& text) {
			const auto file = Root / "Assets" / Path::FromUtf8(name);
			std::filesystem::create_directories(file.parent_path());
			REQUIRE(File::WriteAllText(Path::ToUtf8(file), text));
		}
		void Pack(bool compressed) {
			std::vector<std::filesystem::path> files;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(Root / "Assets"))
				if (entry.is_regular_file()) files.push_back(entry.path().lexically_relative(Root / "Assets"));
			std::string error;
			REQUIRE_MESSAGE(AssetBundle::Write(Root / "Assets", Root / "Assets.fbundle", files, compressed, error), error);
			std::filesystem::remove_all(Root / "Assets");
			REQUIRE_MESSAGE(AssetBundle::MountProject(Root, error), error);
		}
	};
}

TEST_CASE("Asset bundles round trip compressed and stored files without extraction") {
	for (bool compressed : { false, true }) {
		BundleFixture fixture;
		const std::string payload = std::string(16000, 'x') + std::string("\0\1\2", 3);
		fixture.Write("Nested/test.bin", payload);
		fixture.Write("empty.txt", "");
		fixture.Write("umlaute-\xc3\xa4.txt", "unicode");
		fixture.Pack(compressed);
		if (compressed) CHECK(std::filesystem::file_size(fixture.Root / "Assets.fbundle") < payload.size());
		const auto file = fixture.Root / "Assets/Nested/test.bin";
		CHECK_FALSE(std::filesystem::exists(file));
		CHECK(File::Exists(Path::ToUtf8(file)));
		CHECK(File::IsRegularFile(file));
		CHECK(File::IsDirectory(fixture.Root / "Assets/Nested"));
		CHECK(File::ReadAllText(Path::ToUtf8(file)) == payload);
		CHECK(File::ReadAllText(Path::ToUtf8(fixture.Root / "Assets" / Path::FromUtf8("umlaute-\xc3\xa4.txt"))) == "unicode");
		CHECK(File::ReadAllBytes(Path::ToUtf8(fixture.Root / "Assets/empty.txt")).empty());
		CHECK(FileStamp::Read(Path::ToUtf8(file)).Exists);
		CHECK(FileStamp::Read(Path::ToUtf8(file)).Size == payload.size());
		std::error_code error;
		CHECK(File::ListFiles(fixture.Root / "Assets", false, error).size() == 2);
		CHECK_FALSE(error);
		CHECK(File::ListFiles(fixture.Root / "Assets", true, error).size() == 3);
		CHECK_FALSE(error);
		fixture.Write("StreamingAssets/raw.txt", "loose");
		CHECK(File::ListFiles(fixture.Root / "Assets", true, error).size() == 4);
		std::vector<std::future<std::string>> reads;
		for (int i = 0; i < 8; ++i) reads.push_back(std::async(std::launch::async, [file] { return File::ReadAllText(Path::ToUtf8(file)); }));
		for (auto& read : reads) CHECK(read.get() == payload);
		AssetBundle::UnmountAll();
		CHECK_FALSE(File::Exists(Path::ToUtf8(file)));
		CHECK(File::Exists(Path::ToUtf8(fixture.Root / "Assets/StreamingAssets/raw.txt")));
	}
}

TEST_CASE("Asset bundles reject unsafe paths truncated indices and corrupt payloads") {
	BundleFixture fixture;
	fixture.Write("one.bin", std::string(4096, 'a'));
	std::string error;
	CHECK_FALSE(AssetBundle::Write(fixture.Root / "Assets", fixture.Root / "bad.fbundle", { "../outside" }, false, error));
	CHECK_FALSE(AssetBundle::Write(fixture.Root / "Assets", fixture.Root / "bad.fbundle", { "one.bin", "one.bin" }, false, error));
	for (bool compressed : { false, true }) {
		const auto archive = fixture.Root / (compressed ? "packed.fbundle" : "stored.fbundle");
		REQUIRE(AssetBundle::Write(fixture.Root / "Assets", archive, { "one.bin" }, compressed, error));
		AssetBundle bundle;
		REQUIRE(bundle.Open(archive, error));
		auto bytes = File::ReadAllBytes(Path::ToUtf8(archive));
		bytes[22] ^= 0x40;
		REQUIRE(File::WriteAllBytes(Path::ToUtf8(archive), bytes));
		std::vector<std::uint8_t> result;
		CHECK_FALSE(bundle.Read("one.bin", result, error));
		CHECK_FALSE(error.empty());
		CHECK(result.empty());
		bytes.pop_back();
		REQUIRE(File::WriteAllBytes(Path::ToUtf8(archive), bytes));
		CHECK_FALSE(bundle.Open(archive, error));
		CHECK_FALSE(bundle.Contains("one.bin"));
	}
}

TEST_CASE("Asset bundles discover GUIDs and nested scene names without a registry manifest") {
	BundleFixture fixture;
	fixture.Write("Levels/Nested.scene", "{}");
	fixture.Write("Levels/Nested.scene.meta", R"({"AssetGUID":"123456789","kind":4})");
	fixture.Pack(true);
	auto project = std::make_unique<FunkeProject>();
	project->RootDirectory = Path::ToUtf8(fixture.Root);
	project->AssetsDirectory = Path::ToUtf8(fixture.Root / "Assets");
	project->ScenesDirectory = Path::ToUtf8(fixture.Root / "Assets/Scenes");
	CHECK(File::Exists(project->GetSceneFilePath("Nested")));
	ProjectManager::SetCurrentProject(std::move(project), true);
	AssetRegistry::Sync();
	CHECK(File::Exists(AssetRegistry::ResolvePath(123456789)));
	CHECK(AssetRegistry::GetKind(123456789) == AssetKind::Scene);
}

TEST_CASE("Asset bundles feed image audio and font decoders") {
	BundleFixture fixture;
	const auto repo = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	const auto font = repo / "Funke-Runtime/FunkeAssets/Fonts/IBMPlexSansCondensed-Regular.ttf";
	REQUIRE(std::filesystem::is_regular_file(font));
	std::filesystem::copy_file(font, fixture.Root / "Assets/font.ttf");
	const unsigned char png[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,4,0,0,0,181,28,12,2,0,0,0,11,73,68,65,84,120,218,99,252,255,31,0,3,3,2,0,239,191,105,107,0,0,0,0,73,69,78,68,174,66,96,130};
	REQUIRE(File::WriteAllBytes(Path::ToUtf8(fixture.Root / "Assets/image.png"), std::vector<std::uint8_t>(std::begin(png), std::end(png))));
	std::string wav = "RIFF";
	auto number = [&](std::uint32_t value, int count) { for (int i = 0; i < count; ++i) wav += static_cast<char>((value >> (i * 8)) & 255); };
	number(40, 4); wav += "WAVEfmt "; number(16, 4); number(1, 2); number(1, 2);
	number(8000, 4); number(16000, 4); number(2, 2); number(16, 2); wav += "data"; number(4, 4); number(0, 4);
	fixture.Write("sound.wav", wav);
	fixture.Pack(true);
	const auto imagePath = Path::ToUtf8(fixture.Root / "Assets/image.png");
	REQUIRE(ImageProcessing::LoadFile(Path::FromUtf8(imagePath)) != nullptr);
	REQUIRE(Texture2D::DecodeFileToCpu(imagePath.c_str(), false) != nullptr);
	REQUIRE(Font::PrepareFromFile(Path::ToUtf8(fixture.Root / "Assets/font.ttf"), 16) != nullptr);
	Audio audio;
	REQUIRE(audio.LoadFromFile(Path::ToUtf8(fixture.Root / "Assets/sound.wav")));
	CHECK(audio.GetFrameCount() == 2);
}
