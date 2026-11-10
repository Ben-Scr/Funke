#include "pch.hpp"
#include "EditorIcons.hpp"
#include "Graphics/TextureManager.hpp"
#include "Project/FunkeBuildProfile.hpp"

#include <imgui.h>
#include <filesystem>

namespace Funke {
	namespace {
		struct PlatformIcon {
			uint64_t handle = 0;
			float width = 0.0f;
			float height = 0.0f;
		};

		PlatformIcon LoadPlatformIcon(BuildPlatform platform) {
			const char* iconName = FunkeBuildProfile::PlatformIconName(platform);
			if (!iconName) return {};

			static std::string platformIconsDir = []() {
				const std::string editorTexturesDir = Path::ResolveFunkeAssets("Textures");
				if (editorTexturesDir.empty()) return std::string{};
				return (std::filesystem::path(editorTexturesDir) / "Editor" / "PlatformIcons").string();
			}();
			if (platformIconsDir.empty()) return {};

			const std::string fullPath = (std::filesystem::path(platformIconsDir) /
				(std::string(iconName) + ".png")).string();
			TextureHandle handle = TextureManager::LoadTexture(
				fullPath, Filter::Bilinear, Wrap::Clamp, Wrap::Clamp);
			Texture2D* tex = TextureManager::GetTexture(handle);
			if (tex && tex->IsValid()) return { tex->GetHandle(), tex->GetWidth(), tex->GetHeight() };
			return {};
		}
	}

	void EditorIcons::DrawPlatform(ImDrawList* drawList, BuildPlatform platform, const ImVec2& boxMin, float boxSize, float alpha) {
		const PlatformIcon icon = LoadPlatformIcon(platform);
		if (icon.handle == 0 || icon.width <= 0.0f || icon.height <= 0.0f) return;
		float drawW = boxSize;
		float drawH = boxSize;
		if (icon.width > icon.height) drawH = boxSize * (icon.height / icon.width);
		else if (icon.height > icon.width) drawW = boxSize * (icon.width / icon.height);
		const ImVec2 min(boxMin.x + (boxSize - drawW) * 0.5f, boxMin.y + (boxSize - drawH) * 0.5f);
		drawList->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(icon.handle)),
			min, ImVec2(min.x + drawW, min.y + drawH), ImVec2(0, 0), ImVec2(1, 1),
			IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f)));
	}

	std::unordered_map<std::string, EditorIcons::IconEntry> EditorIcons::s_Icons;
	bool EditorIcons::s_Initialized = false;

	// Must mirror the actual sizes shipped under
	// FunkeAssets/Textures/Editor/<group>/<name>/<name>_<size>.png. Listing a size here
	// that isn't on disk causes `Editor icon not found` warnings every frame for
	// any callsite whose snap-up lands on the missing size.
	static constexpr int k_AvailableSizes[] = { 16, 32, 64, 128 };

	static const char* GroupForName(const std::string& name) {
		if (name.rfind("file_", 0) == 0 || name.rfind("folder_", 0) == 0)
			return "FileIcons";
		return "General";
	}

	void EditorIcons::Initialize() {
		if (s_Initialized) return;
		s_Initialized = true;
	}

	void EditorIcons::Shutdown() {
		for (auto& [key, entry] : s_Icons)
			entry.Texture.Destroy();
		s_Icons.clear();
		s_Initialized = false;
	}

	int EditorIcons::SnapSize(int requested) {
		int best = k_AvailableSizes[0];
		for (int s : k_AvailableSizes) {
			best = s;
			if (s >= requested) break;
		}
		return best;
	}

	std::string EditorIcons::MakeKey(const std::string& name, int size) {
		return name + "_" + std::to_string(size);
	}

	uint64_t EditorIcons::Get(const std::string& name, int size) {
		int snapped = SnapSize(size);
		std::string key = MakeKey(name, snapped);

		auto it = s_Icons.find(key);
		if (it != s_Icons.end())
			return it->second.Texture.GetHandle();   // 0 for negative-cache entries; caller already treats 0 as "no icon"

		// Lazy-load from FunkeAssets/Textures/Editor/<group>/<name>/<name>_<size>.png
		std::string editorDir = Path::ResolveFunkeAssets("Textures");
		if (editorDir.empty()) {
			// Cache the failure so subsequent frames don't keep retrying.
			s_Icons[key] = IconEntry{};
			return 0;
		}

		std::string filename = name + "_" + std::to_string(snapped) + ".png";
		std::string fullpath = Path::Combine(editorDir, "Editor", GroupForName(name), name, filename);

		IconEntry entry;
		entry.Texture = Texture2D(fullpath.c_str(), Filter::Bilinear, Wrap::Clamp, Wrap::Clamp);
		entry.Size = snapped;

		if (!entry.Texture.IsValid()) {
			// Negative-cache: empty entry prevents per-frame retry (Texture2D construction + stb_image + warn-log on every call).
			FNK_CORE_WARN("Editor icon not found: {}", fullpath);
			s_Icons[key] = std::move(entry);
			return 0;
		}

		uint64_t handle = entry.Texture.GetHandle();
		s_Icons[key] = std::move(entry);
		return handle;
	}
}
