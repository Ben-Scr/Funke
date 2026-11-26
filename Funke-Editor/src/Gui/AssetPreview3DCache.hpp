#pragma once
#include "Graphics/Framebuffer.hpp"
#include "Serialization/FileStamp.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Funke {

	class Renderer3D;

	class AssetPreview3DCache {
	public:
		AssetPreview3DCache();
		~AssetPreview3DCache();
		uint64_t GetThumbnail(const std::string& absolutePath);
		void Update();
		void Invalidate(const std::string& absolutePath);
		void MarkAllStale();
		void Clear();

	private:
		struct Entry {
			Framebuffer Preview;
			FileStamp Stamp;
			uint64_t Shader = 0;
			std::vector<std::pair<std::string, FileStamp>> Dependencies;
			uint64_t LastUsedFrame = 0;
			uint64_t LastStampCheckFrame = 0;
			bool NeedsRender = true;
		};

		void Render(const std::string& path, Entry& entry);
		void EnforceCapacity();

		std::unordered_map<std::string, Entry> m_Entries;
		std::vector<std::string> m_Requests;
		std::unique_ptr<Renderer3D> m_Renderer;
		uint32_t m_ShaderGeneration = 0;
		bool m_ReloadModels = false;
	};
}
