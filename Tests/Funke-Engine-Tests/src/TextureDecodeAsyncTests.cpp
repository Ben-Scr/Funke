#include <doctest/doctest.h>

#include "Core/UUID.hpp"
#include "Graphics/Texture2D.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"

#include <atomic>
#include <barrier>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace Funke;

namespace {

	struct TextureDecodeFile {
		std::filesystem::path Path = std::filesystem::temp_directory_path()
			/ ("funke_async_texture_" + std::to_string(static_cast<uint64_t>(UUID())) + ".tga");

		~TextureDecodeFile() {
			std::error_code error;
			std::filesystem::remove(Path, error);
		}

		bool Write() const {
			std::vector<uint8_t> bytes(18, 0);
			bytes[2] = 2;
			bytes[12] = 7;
			bytes[14] = 9;
			bytes[16] = 32;
			bytes[17] = 0x28;
			for (int y = 0; y < 9; ++y) {
				for (int x = 0; x < 7; ++x) {
					bytes.push_back(31);
					bytes.push_back(static_cast<uint8_t>(x));
					bytes.push_back(static_cast<uint8_t>(y));
					bytes.push_back(static_cast<uint8_t>(x + y));
				}
			}
			return File::WriteAllBytes(Funke::Path::ToUtf8(Path), bytes);
		}
	};

	bool MatchesPixels(const ImageData* image, bool flipped) {
		if (!image || image->Width != 7 || image->Height != 9 || image->OwnedPixels.size() != 7 * 9 * 4)
			return false;
		for (int y = 0; y < 9; ++y) {
			const int sourceY = flipped ? 8 - y : y;
			for (int x = 0; x < 7; ++x) {
				const auto* pixel = image->Pixels + (y * 7 + x) * 4;
				if (pixel[0] != sourceY || pixel[1] != x || pixel[2] != 31 || pixel[3] != x + sourceY)
					return false;
			}
		}
		return true;
	}

}

TEST_CASE("Async texture decoding preserves orientation and alpha across concurrent requests") {
	TextureDecodeFile file;
	REQUIRE(file.Write());
	const std::string path = Path::ToUtf8(file.Path);
	std::atomic<int> failures = 0;
	std::barrier start(5);
	std::vector<std::thread> workers;
	for (int worker = 0; worker < 4; ++worker) {
		workers.emplace_back([&, worker] {
			start.arrive_and_wait();
			for (int iteration = 0; iteration < 128; ++iteration) {
				const bool flipped = (worker + iteration) % 2 == 0;
				const auto image = Texture2D::DecodeFileToCpu(path.c_str(), flipped);
				if (!MatchesPixels(image.get(), flipped)) ++failures;
			}
		});
	}
	start.arrive_and_wait();
	for (auto& worker : workers) worker.join();
	CHECK(failures.load() == 0);
	CHECK(MatchesPixels(Texture2D::DecodeFileToCpu(path.c_str(), false).get(), false));
}

TEST_CASE("Async texture decoding rejects missing and malformed files without GPU state") {
	TextureDecodeFile file;
	const std::string path = Path::ToUtf8(file.Path);
	CHECK_FALSE(Texture2D::DecodeFileToCpu(nullptr));
	CHECK_FALSE(Texture2D::DecodeFileToCpu(""));
	CHECK_FALSE(Texture2D::DecodeFileToCpu(path.c_str()));
	REQUIRE(File::WriteAllText(path, "invalid texture contents"));
	CHECK_FALSE(Texture2D::DecodeFileToCpu(path.c_str(), true));
	REQUIRE(file.Write());
	CHECK(MatchesPixels(Texture2D::DecodeFileToCpu(path.c_str(), true).get(), true));
}
