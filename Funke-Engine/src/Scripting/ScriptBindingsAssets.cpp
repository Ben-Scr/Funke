#include "pch.hpp"
#include "Scripting/ScriptBindings.hpp"

#include "Assets/AssetLoader.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Scripting/DataAssetManager.hpp"
#include "Scripting/ScriptBindingGuard.hpp"
#include "Scripting/ScriptEngine.hpp"
#if FUNKE_WITH_AUDIO
#include "Audio/AudioManager.hpp"
#endif
#if FUNKE_WITH_RENDER
#include "Graphics/TextureManager.hpp"
#endif

#include <thread>
#include <unordered_set>
#include <vector>

namespace Funke {

	namespace {
		std::unordered_set<uint64_t> g_AssetLoadRequests;
		std::thread::id g_AssetLoadThread;

		bool SupportsTypedLoad(AssetKind kind, bool asynchronous) {
			if (asynchronous) {
				return kind == AssetKind::Texture || kind == AssetKind::Audio
					|| kind == AssetKind::Font || kind == AssetKind::Mesh;
			}
			switch (kind) {
#if FUNKE_WITH_RENDER
			case AssetKind::Texture: return true;
#endif
#if FUNKE_WITH_AUDIO
			case AssetKind::Audio: return true;
#endif
			case AssetKind::Font:
			case AssetKind::Mesh:
			case AssetKind::Prefab:
			case AssetKind::DataAsset:
			case AssetKind::Material:
			case AssetKind::AnimationClip:
			case AssetKind::AnimatorController:
				return true;
			default: return false;
			}
		}

		uint64_t ResolveTypedAsset(const char* path, uint64_t assetId, AssetKind kind, bool asynchronous) {
			if (!SupportsTypedLoad(kind, asynchronous)) return 0;
			if (path && path[0] != '\0') assetId = AssetRegistry::GetOrCreateAssetUUID(path);
			if (assetId == 0) return 0;
#if FUNKE_WITH_RENDER
			if (!asynchronous && kind == AssetKind::Texture && TextureManager::IsRuntimeTextureUUID(assetId)) return assetId;
#endif
			return AssetRegistry::Exists(assetId) && AssetRegistry::GetKind(assetId) == kind ? assetId : 0;
		}

		uint64_t Funke_Asset_LoadTyped(const char* path, uint64_t assetId, int expectedKind) {
			const AssetKind kind = static_cast<AssetKind>(expectedKind);
			assetId = ResolveTypedAsset(path, assetId, kind, false);
			if (assetId == 0) return 0;
			switch (kind) {
#if FUNKE_WITH_RENDER
			case AssetKind::Texture:
				return TextureManager::IsRuntimeTextureUUID(assetId)
					|| TextureManager::LoadTextureByUUID(assetId).IsValid() ? assetId : 0;
#endif
#if FUNKE_WITH_AUDIO
			case AssetKind::Audio:
				return AudioManager::LoadAudioByUUID(assetId).IsValid() ? assetId : 0;
#endif
			case AssetKind::DataAsset:
				return DataAssetManager::Load(assetId) ? assetId : 0;
			// Reference-only assets keep their lazy parsing; font atlases depend on the eventual text size.
			default: return assetId;
			}
		}

		uint64_t Funke_Asset_BeginLoadTypedAsync(const char* path, uint64_t assetId, int expectedKind) {
			if (!AssetLoader::IsMainThread()) return 0;
			assetId = ResolveTypedAsset(path, assetId, static_cast<AssetKind>(expectedKind), true);
			if (assetId == 0) return 0;
			const uint64_t request = AssetLoader::LoadAsync(assetId);
			if (request != 0) {
				try { g_AssetLoadRequests.insert(request); }
				catch (...) { AssetLoader::Release(request); throw; }
				g_AssetLoadThread = std::this_thread::get_id();
			}
			return request;
		}

		void Funke_Asset_CancelLoads(const uint64_t* ids, int count, int release) {
			if (std::this_thread::get_id() != g_AssetLoadThread || !ids || count <= 0) return;
			for (int i = 0; i < count; ++i) {
				if (!g_AssetLoadRequests.contains(ids[i])) continue;
				if (release != 0) {
					AssetLoader::Release(ids[i]);
					g_AssetLoadRequests.erase(ids[i]);
				}
				else AssetLoader::Cancel(ids[i]);
			}
		}
	}

	void ScriptBindings::PopulateAssetBindings(NativeBindings& bindings) {
		bindings.Asset_LoadTyped = FNK_GUARDED_BINDING(Funke_Asset_LoadTyped);
		bindings.Asset_BeginLoadTypedAsync = FNK_GUARDED_BINDING(Funke_Asset_BeginLoadTypedAsync);
		bindings.Asset_CancelLoads = FNK_GUARDED_BINDING(Funke_Asset_CancelLoads);
	}

	void ScriptBindings::PumpAssetLoadCompletions() {
		if (g_AssetLoadRequests.empty() || !AssetLoader::IsMainThread()) return;
		std::vector<uint64_t> ids;
		std::vector<uint64_t> assetIds;
		std::vector<int> statuses;
		for (const uint64_t request : g_AssetLoadRequests) {
			const AssetLoadResult result = AssetLoader::GetResult(request);
			if (result.Status == AssetLoadStatus::Pending) continue;
			ids.push_back(request);
			assetIds.push_back(result.AssetId);
			statuses.push_back(static_cast<int>(result.Status));
		}
		if (ids.empty()) return;
		// Managed completions may start or cancel loads reentrantly, so retire the entire batch first.
		for (const uint64_t request : ids) {
			AssetLoader::Release(request);
			g_AssetLoadRequests.erase(request);
		}
		ScriptEngine::NotifyAssetLoadsCompleted(ids.data(), assetIds.data(), statuses.data(), static_cast<int>(ids.size()));
	}

}
