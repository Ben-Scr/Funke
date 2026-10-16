#pragma once
#include "Graphics/Framebuffer.hpp"
#include "Serialization/FileStamp.hpp"

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Funke {

	class Scene;

	// Asset-browser previews of .prefab files: the prefab is loaded into a detached scene, framed and rendered
	// into its own framebuffer. Until that render is done (fonts baked) the tile keeps its file icon.
	class PrefabThumbnailCache {
	public:
		static constexpr int k_Resolution = 256;
		static constexpr size_t k_MaxEntries = 64;

		PrefabThumbnailCache();
		~PrefabThumbnailCache();

		PrefabThumbnailCache(const PrefabThumbnailCache&) = delete;
		PrefabThumbnailCache& operator=(const PrefabThumbnailCache&) = delete;

		// WGPUTextureView for ImGui::Image (top row first, no UV flip); 0 while loading or when the prefab draws nothing.
		uint64_t GetThumbnail(const std::string& absolutePath);

		// Once per frame before the tiles draw: renders at most one prefab, picked from last frame's requests.
		void Update();

		void Invalidate(const std::string& absolutePath);
		// Re-renders every preview (e.g. a texture changed); old images stay up until their replacement is done.
		void MarkAllStale();
		void Clear();

	private:
		struct Entry {
			Framebuffer Preview;
			bool HasPreview = false;
			bool NeedsRender = true;
			FileStamp Stamp;
			uint64_t LastStampCheckFrame = 0;
			// Entries drawn this frame are never evicted: ImGui still holds their texture view.
			uint64_t LastUsedFrame = 0;
			std::list<std::string>::iterator LruIt;
		};

		struct PendingRender;

		void StartRender(const std::string& absolutePath, Entry& entry);
		// Renders the pending scene once more and hands it to its entry once its fonts are in.
		void StepPendingRender();
		void EnforceCapacity();

		std::unordered_map<std::string, Entry> m_Entries;
		std::list<std::string> m_LRU;
		std::vector<std::string> m_Requests;
		std::unique_ptr<PendingRender> m_Pending;
	};

}
