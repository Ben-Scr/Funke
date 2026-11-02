#pragma once

#include "Assets/AssetKind.hpp"
#include "Core/Export.hpp"

#include <cstdint>
#include <cstddef>
#include <string>

namespace Funke {

	enum class AssetLoadStatus {
		NotFound = 0,
		Pending,
		Success,
		Failed,
		Cancelled,
		Unsupported
	};

	struct AssetLoadResult {
		AssetLoadStatus Status = AssetLoadStatus::NotFound;
		uint64_t AssetId = 0;
		std::string Message;
		bool Succeeded() const { return Status == AssetLoadStatus::Success; }
	};

	// Main-thread API; workers own CPU data, Pump publishes into the normal asset caches.
	class FUNKE_API AssetLoader {
	public:
		static void Initialize();
		static void Shutdown();
		static bool IsMainThread();
		// Texture, audio, font (32px) and mesh; 0 means the request was rejected.
		static uint64_t LoadAsync(uint64_t assetId);
		static AssetLoadResult GetResult(uint64_t operationId);
		static bool Cancel(uint64_t operationId);
		// Release every request, including failures; releasing pending work cancels that observer.
		static void Release(uint64_t operationId);
		// Uploads remain on the main thread; each publication is indivisible.
		static void Pump(double budgetMs = 2.0, size_t maxPublications = 1);
		static void Invalidate(AssetKind kind);
		static void RetainAsset(uint64_t assetId);
		static void ReleaseAsset(uint64_t assetId);
		static bool IsAssetRetained(uint64_t assetId);

	private:
		AssetLoader() = delete;
	};

}
