#include "pch.hpp"
#include "DevTools/ScreenCapture.hpp"

#include "Core/Log.hpp"
#include "Core/Platform.hpp"
#include "Serialization/Path.hpp"
#if FUNKE_WITH_RENDER
#include "Graphics/Backend/WebGPUBackend.hpp"
#endif

// Static: the editor and CLI link their own stb_image_write implementation.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <filesystem>
#include <fstream>

namespace Funke {

	namespace {
		std::vector<std::string> g_Pending;
		std::string g_Directory;
	}

	void ScreenCapture::Request(const std::string& path) {
		g_Pending.push_back(ResolvePath(path));
	}

	bool ScreenCapture::HasPending() {
		return !g_Pending.empty();
	}

	std::string ScreenCapture::GetDirectory() {
		if (!g_Directory.empty()) return g_Directory;
		return Path::ToUtf8(Platform::GetUserDataRoot() / "Funke" / "Screenshots");
	}

	void ScreenCapture::SetDirectory(const std::string& directory) {
		g_Directory = directory;
	}

	std::string ScreenCapture::ResolvePath(const std::string& path) {
		std::filesystem::path resolved = Path::FromUtf8(path.empty() ? std::string("screenshot.png") : path);
		if (resolved.extension().empty()) resolved += ".png";
		if (resolved.is_relative()) resolved = Path::FromUtf8(GetDirectory()) / resolved;
		return Path::ToUtf8(resolved.lexically_normal());
	}

	bool ScreenCapture::WritePng(const std::string& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba) {
		if (width == 0 || height == 0 || rgba.size() < static_cast<size_t>(width) * height * 4) return false;
		std::vector<uint8_t> bytes;
		const auto append = [](void* context, void* data, int length) {
			auto* out = static_cast<std::vector<uint8_t>*>(context);
			out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + length);
		};
		if (!stbi_write_png_to_func(append, &bytes, static_cast<int>(width), static_cast<int>(height), 4,
				rgba.data(), static_cast<int>(width * 4))) {
			return false;
		}
		const std::filesystem::path target = Path::FromUtf8(path);
		std::error_code ec;
		if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);
		std::ofstream out(target, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return out.good();
	}

	void ScreenCapture::ProcessPending() {
		if (g_Pending.empty()) return;
		std::vector<std::string> pending;
		pending.swap(g_Pending);
#if FUNKE_WITH_RENDER
		uint32_t width = 0;
		uint32_t height = 0;
		std::vector<uint8_t> rgba;
		if (!WebGPUBackend::ReadSwapChainRGBA8(width, height, rgba)) {
			FNK_CORE_WARN_TAG("Screenshot", "Could not read the frame (the surface does not allow copies on this backend).");
			return;
		}
		// The swap chain's alpha is meaningless (often 0 behind UI); screenshots are opaque.
		for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
		for (const std::string& path : pending) {
			if (WritePng(path, width, height, rgba)) FNK_CORE_INFO_TAG("Screenshot", "Saved {} ({}x{})", path, width, height);
			else FNK_CORE_WARN_TAG("Screenshot", "Could not write {}", path);
		}
#else
		FNK_CORE_WARN_TAG("Screenshot", "Screenshots need the render module.");
#endif
	}

} // namespace Funke
