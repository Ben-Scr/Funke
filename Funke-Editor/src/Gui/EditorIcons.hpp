#pragma once
#include "Graphics/Texture2D.hpp"
#include "Serialization/Path.hpp"
#include "Core/Export.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>

struct ImDrawList;
struct ImVec2;

namespace Funke {
	enum class BuildPlatform : uint8_t;
	class EditorIcons {
	public:
		static void Initialize();
		static void Shutdown();

		/// Returns WGPUTextureView cast to uint64_t for ImGui::Image; size snaps to nearest of 16,24,32,48,64,128,256,512.
		static uint64_t Get(const std::string& name, int size = 32);

		static void DrawPlatform(ImDrawList* drawList, BuildPlatform platform, const ImVec2& boxMin,
			float boxSize, float alpha = 1.0f);

	private:
		struct IconEntry {
			Texture2D Texture;
			int Size = 0;
		};

		static std::string MakeKey(const std::string& name, int size);
		static int SnapSize(int requested);

		static std::unordered_map<std::string, IconEntry> s_Icons;
		static bool s_Initialized;
	};
}
