#pragma once

#include "Core/Log.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/File.hpp"
#include "Serialization/SpecialFolder.hpp"

#include <imgui.h>

#include <filesystem>
#include <string>
#include <cstring>
#include <limits>

namespace Funke {

	inline constexpr float k_FunkeImGuiFontSize = 15.0f;

	inline ImFont* AddFunkeFont(ImFontAtlas& atlas, const std::string& path, float size,
		const ImFontConfig& config, const ImWchar* ranges) {
		const auto bytes = File::ReadAllBytes(path);
		if (bytes.empty() || bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) return nullptr;
		void* data = IM_ALLOC(bytes.size());
		if (!data) return nullptr;
		std::memcpy(data, bytes.data(), bytes.size());
		ImFontConfig ownedConfig = config;
		ownedConfig.FontDataOwnedByAtlas = true;
		return atlas.AddFontFromMemoryTTF(data, static_cast<int>(bytes.size()), size, &ownedConfig, ranges);
	}

	inline ImFont* LoadFunkeImGuiFont(ImGuiIO& io, float dpiScale = 1.0f,
									  float baseSize = k_FunkeImGuiFontSize) {
		const float fontSize = baseSize * dpiScale;

		// Explicit glyph ranges required: each binary links its own ImGui; the launcher bakes a static atlas so nullptr range would produce nothing.
		ImFontConfig primaryCfg;
		primaryCfg.SizePixels = fontSize;
		primaryCfg.PixelSnapH = true;

		ImFont* primary = nullptr;
		const std::string ibmSansPath = Path::Combine(
			Path::ResolveFunkeAssets("Fonts"), "IBMPlexSansCondensed-Regular.ttf");
		if (File::Exists(ibmSansPath)) {
			primary = AddFunkeFont(*io.Fonts,
				ibmSansPath, fontSize, primaryCfg,
				io.Fonts->GetGlyphRangesDefault());
		}

		// Merged CJK fallback. The Localization service downloads this font
		// on-demand to the user-writable location; we prefer that copy so
		// the bundle can ship without it. Fall back to the bundled FunkeAssets
		// path for the dev-layout install (still useful while iterating).
		std::string userCjkPath;
		try {
			userCjkPath = (std::filesystem::path(Path::GetSpecialFolderPath(SpecialFolder::LocalAppData))
				/ "Funke" / "Fonts" / "NotoSansCJK" / "NotoSansCJK-Regular.ttc").string();
		}
		catch (...) {
			userCjkPath.clear();
		}
		const std::string bundledCjkPath = Path::Combine(
			Path::ResolveFunkeAssets("Fonts"), "NotoSansCJK", "NotoSansCJK-Regular.ttc");

		std::string cjkPath;
		if (!userCjkPath.empty() && std::filesystem::exists(userCjkPath)) {
			cjkPath = userCjkPath;
		}
		else if (File::Exists(bundledCjkPath)) {
			cjkPath = bundledCjkPath;
		}

		ImFontConfig cjkCfg;
		cjkCfg.SizePixels = fontSize;
		cjkCfg.PixelSnapH = true;
		cjkCfg.MergeMode = true;
		// MergeMode requires an already-added base font; with no primary
		// ImGui reads Fonts.back() of an empty vector → 0xC0000005.
		if (primary && !cjkPath.empty()) {
			AddFunkeFont(*io.Fonts,
				cjkPath, fontSize, cjkCfg,
				io.Fonts->GetGlyphRangesJapanese());
		}
		else if (cjkPath.empty()) {
			FNK_CORE_WARN_TAG("Localization",
				"CJK font not installed; CJK characters will render as boxes. "
				"It will be downloaded the first time you select a CJK language.");
		}

		if (primary) return primary;

		ImFontConfig fallbackCfg;
		fallbackCfg.SizePixels = fontSize;
		fallbackCfg.PixelSnapH = true;
		return io.Fonts->AddFontDefault(&fallbackCfg);
	}

}
