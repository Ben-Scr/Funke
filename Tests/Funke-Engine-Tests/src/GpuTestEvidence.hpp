#pragma once

#include "Graphics/RenderApi.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace GpuTestEvidence {

	inline std::string Environment(const char* name) {
		const char* value = std::getenv(name);
		return value ? value : "";
	}

	inline bool Requested(const char* flag = "FUNKE_TEST_RENDER_GPU") {
		return Environment(flag) == "1" || Environment("FUNKE_TEST_GPU_REQUIRED") == "1";
	}

	inline bool WriteJson(const std::string& name, const Funke::Json::Value& value) {
		const auto directory = Environment("FUNKE_TEST_GPU_EVIDENCE");
		if (directory.empty()) return Environment("FUNKE_TEST_GPU_REQUIRED") != "1";
		const auto root = Funke::Path::FromUtf8(directory);
		std::filesystem::create_directories(root);
		return Funke::File::WriteAllText(Funke::Path::ToUtf8(root / (name + ".json")), Funke::Json::Stringify(value, true));
	}

	inline bool Adapter(const char* name) {
		using namespace Funke;
		const auto version = RenderApi::GetVersionString();
		const auto expected = Environment("FUNKE_TEST_GPU_ADAPTER");
		const bool software = version.ends_with(" / CPU");
		const bool hardware = version.ends_with(" / DiscreteGPU") || version.ends_with(" / IntegratedGPU");
		const bool valid = RenderApi::IsInitialized() && !RenderApi::GetRendererString().empty()
			&& (expected.empty() || (expected == "software" && software) || (expected == "hardware" && hardware));
		auto info = Json::Value::MakeObject();
		info.AddMember("initialized", RenderApi::IsInitialized());
		info.AddMember("renderer", RenderApi::GetRendererString());
		info.AddMember("vendor", RenderApi::GetVendorString());
		info.AddMember("driver", RenderApi::GetDriverDescription());
		info.AddMember("version", version);
		info.AddMember("adapterClass", software ? "software" : hardware ? "hardware" : "unknown");
		info.AddMember("expectedClass", expected);
		info.AddMember("valid", valid);
		return WriteJson(std::string(name) + "-adapter", info) && valid;
	}

	inline std::vector<uint8_t> Solid(int width, int height, std::array<uint8_t, 4> color) {
		std::vector<uint8_t> image(static_cast<size_t>(width) * height * 4);
		for (size_t offset = 0; offset < image.size(); offset += 4) {
			std::copy(color.begin(), color.end(), image.begin() + offset);
		}
		return image;
	}

	inline void Rectangle(std::vector<uint8_t>& image, int width, int x0, int y0, int x1, int y1,
		std::array<uint8_t, 4> color) {
		for (int y = y0; y < y1; ++y) {
			for (int x = x0; x < x1; ++x) {
				std::copy(color.begin(), color.end(), image.begin() + (y * width + x) * 4);
			}
		}
	}

	inline bool Compare(const char* name, int width, int height, const std::vector<uint8_t>& actual,
		std::vector<uint8_t> reference, int tolerance = 2) {
		using namespace Funke;
		const bool negative = std::string(name) == "sprite2d" && Environment("FUNKE_TEST_GPU_NEGATIVE") == "1";
		if (negative) Rectangle(reference, width, 16, 16, 48, 48, { 0, 255, 0, 255 });
		const bool complete = actual.size() == static_cast<size_t>(width) * height * 4 && reference.size() == actual.size();
		uint64_t mismatches = 0;
		int maximumError = 0;
		if (complete) {
			for (size_t offset = 0; offset < actual.size(); offset += 4) {
				bool mismatch = false;
				for (size_t channel = 0; channel < 4; ++channel) {
					const int error = std::abs(int(actual[offset + channel]) - int(reference[offset + channel]));
					maximumError = std::max(maximumError, error);
					mismatch = mismatch || error > tolerance;
				}
				mismatches += mismatch;
			}
		}
		bool saved = true;
		const auto directory = Environment("FUNKE_TEST_GPU_EVIDENCE");
		if (!directory.empty()) {
			const auto root = Path::FromUtf8(directory);
			std::filesystem::create_directories(root);
			saved = File::WriteAllBytes(Path::ToUtf8(root / (std::string(name) + "-actual.rgba")), actual)
				&& File::WriteAllBytes(Path::ToUtf8(root / (std::string(name) + "-reference.rgba")), reference);
		}
		auto result = Json::Value::MakeObject();
		result.AddMember("name", name);
		result.AddMember("width", width);
		result.AddMember("height", height);
		result.AddMember("tolerance", tolerance);
		result.AddMember("gpuReadback", complete);
		result.AddMember("mismatchedPixels", mismatches);
		result.AddMember("maximumChannelError", maximumError);
		result.AddMember("negativeProbe", negative);
		result.AddMember("passed", complete && mismatches == 0);
		return WriteJson(name, result) && saved && complete && mismatches == 0;
	}
}
