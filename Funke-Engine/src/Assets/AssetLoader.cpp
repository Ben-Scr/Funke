#include "pch.hpp"
#include "Assets/AssetLoader.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Core/PlatformDetection.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/FileStamp.hpp"
#if FUNKE_WITH_AUDIO
#include "Audio/Audio.hpp"
#include "Audio/AudioManager.hpp"
#endif
#if FUNKE_WITH_RENDER
#include "Graphics/TextureManager.hpp"
#include "Graphics/Text/FontManager.hpp"
#if FUNKE_WITH_3D
#include "Graphics/MeshAsset.hpp"
#endif
#endif

#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Funke {

	namespace {
		constexpr size_t k_MaxPreparations = 2;
		constexpr size_t k_MaxRequests = 256;

		struct PreparedAsset {
			bool Ready = false;
			std::string Error;
#if FUNKE_WITH_AUDIO
			std::unique_ptr<Audio> Sound;
#endif
#if FUNKE_WITH_RENDER
			std::unique_ptr<ImageData> Image;
			std::shared_ptr<const FontPreparedData> FontData;
#if FUNKE_WITH_3D
			std::unique_ptr<MeshAsset> Mesh;
#endif
#endif
		};

		struct Work {
			uint64_t AssetId = 0;
			AssetKind Kind = AssetKind::Unknown;
			std::string Path;
			std::string ProjectRoot;
			FileStamp SourceStamp;
			FileStamp TextureMetaStamp;
			uint64_t RegistryVersion = 0;
			bool Discarded = false;
#if FUNKE_WITH_RENDER
			Filter FilterMode = Filter::Point;
			Wrap WrapU = Wrap::Clamp;
			Wrap WrapV = Wrap::Clamp;
			float FontSize = 32.0f;
#endif
			std::future<PreparedAsset> Future;
		};

		struct Request {
			AssetLoadResult Result;
			std::shared_ptr<Work> Preparation;
		};

		bool g_Initialized = false;
		std::thread::id g_MainThread;
		uint64_t g_NextId = 1;
		std::unordered_map<uint64_t, Request> g_Requests;
		std::vector<std::shared_ptr<Work>> g_Work;

		bool Supports(AssetKind kind) {
			switch (kind) {
#if FUNKE_WITH_AUDIO
			case AssetKind::Audio: return true;
#endif
#if FUNKE_WITH_RENDER
			case AssetKind::Texture:
			case AssetKind::Font: return true;
#if FUNKE_WITH_3D
			case AssetKind::Mesh: return true;
#endif
#endif
			default: return false;
			}
		}

		bool IsCached(const Work& work) {
			switch (work.Kind) {
#if FUNKE_WITH_AUDIO
			case AssetKind::Audio:
				return AudioManager::FindLoadedAudio(work.Path).IsValid();
#endif
#if FUNKE_WITH_RENDER
			case AssetKind::Texture:
				return TextureManager::IsValid(TextureManager::GetTextureHandle(work.Path, work.FilterMode, work.WrapU, work.WrapV));
			case AssetKind::Font:
				return FontManager::FindLoadedFontByUUID(work.AssetId, work.FontSize).IsValid();
#if FUNKE_WITH_3D
			case AssetKind::Mesh: return MeshLibrary::FindLoaded(work.AssetId) != nullptr;
#endif
#endif
			default: return false;
			}
		}

		PreparedAsset Prepare(const Work& work) {
			PreparedAsset prepared;
			try {
				switch (work.Kind) {
#if FUNKE_WITH_AUDIO
				case AssetKind::Audio:
					prepared.Sound = std::make_unique<Audio>();
					prepared.Ready = prepared.Sound->LoadFromFile(work.Path);
					break;
#endif
#if FUNKE_WITH_RENDER
				case AssetKind::Texture:
					prepared.Image = Texture2D::DecodeFileToCpu(work.Path.c_str(), false);
					prepared.Ready = prepared.Image != nullptr;
					break;
				case AssetKind::Font:
					prepared.FontData = Font::PrepareFromFile(work.Path, work.FontSize);
					prepared.Ready = prepared.FontData != nullptr;
					break;
#if FUNKE_WITH_3D
				case AssetKind::Mesh:
					prepared.Mesh = std::make_unique<MeshAsset>();
					prepared.Ready = MeshLibrary::LoadAssetFromFile(work.Path, *prepared.Mesh);
					break;
#endif
#endif
				default: break;
				}
				if (!prepared.Ready) prepared.Error = "Could not read or decode asset: " + work.Path;
			}
			catch (const std::exception& error) { prepared.Error = error.what(); }
			catch (...) { prepared.Error = "Asset preparation failed"; }
			return prepared;
		}

		bool Publish(const Work& work, PreparedAsset& prepared) {
			if (IsCached(work)) return true;
			switch (work.Kind) {
#if FUNKE_WITH_AUDIO
			case AssetKind::Audio:
				return AudioManager::LoadDecodedAudio(std::move(prepared.Sound)).IsValid();
#endif
#if FUNKE_WITH_RENDER
			case AssetKind::Texture:
				return prepared.Image && TextureManager::IsValid(TextureManager::LoadTextureFromImage(
					work.Path, *prepared.Image, work.FilterMode, work.WrapU, work.WrapV));
			case AssetKind::Font:
				return FontManager::LoadPreparedFont(work.AssetId, prepared.FontData).IsValid();
#if FUNKE_WITH_3D
			case AssetKind::Mesh:
				return prepared.Mesh && MeshLibrary::StoreLoadedAsset(work.AssetId, work.Path, std::move(*prepared.Mesh));
#endif
#endif
			default: return false;
			}
		}

		bool HasObservers(const std::shared_ptr<Work>& work) {
			for (const auto& [id, request] : g_Requests) {
				if (request.Preparation == work && request.Result.Status == AssetLoadStatus::Pending) return true;
			}
			return false;
		}

		void Complete(const std::shared_ptr<Work>& work, AssetLoadStatus status, const std::string& message = {}) {
			for (auto& [id, request] : g_Requests) {
				if (request.Preparation != work) continue;
				if (request.Result.Status == AssetLoadStatus::Pending) {
					request.Result.Status = status;
					request.Result.Message = message;
				}
				request.Preparation.reset();
			}
		}

		bool Revalidate(const std::shared_ptr<Work>& work, const PreparedAsset* prepared = nullptr) {
			if (work->Discarded) return false;
			const bool registryChanged = work->RegistryVersion != AssetRegistry::GetChangeVersion();
			if (!registryChanged && !prepared) return true;
			const auto* project = ProjectManager::GetCurrentProject();
			bool unchanged = work->ProjectRoot == (project ? project->RootDirectory : std::string{})
				&& (!registryChanged || (AssetRegistry::ResolvePath(work->AssetId) == work->Path
					&& AssetRegistry::GetKind(work->AssetId) == work->Kind))
				&& FileStamp::Read(work->Path) == work->SourceStamp
				&& (work->Kind != AssetKind::Texture
					|| FileStamp::Read(AssetRegistry::GetMetaPath(work->Path)) == work->TextureMetaStamp);
#if FUNKE_WITH_RENDER && FUNKE_WITH_3D
			if (unchanged && prepared && prepared->Mesh) {
				for (const auto& [path, stamp] : prepared->Mesh->SourceDependencies) {
					if (FileStamp::Read(path) != stamp) { unchanged = false; break; }
				}
			}
#endif
			if (unchanged) {
				work->RegistryVersion = AssetRegistry::GetChangeVersion();
				return true;
			}
			work->Discarded = true;
			Complete(work, AssetLoadStatus::Cancelled, "Asset or project changed during loading");
			return false;
		}
	}

	void AssetLoader::Initialize() {
		if (g_Initialized) return;
		g_MainThread = std::this_thread::get_id();
		g_Requests.clear();
		g_Initialized = true;
	}

	bool AssetLoader::IsMainThread() {
		return g_Initialized && std::this_thread::get_id() == g_MainThread;
	}

	uint64_t AssetLoader::LoadAsync(uint64_t assetId) {
		if (!IsMainThread() || assetId == 0 || g_Requests.size() >= k_MaxRequests) return 0;
		const uint64_t id = g_NextId++;
		Request request{ { AssetLoadStatus::Pending, assetId, {} }, {} };
		const AssetKind kind = AssetRegistry::GetKind(assetId);
		const std::string path = AssetRegistry::ResolvePath(assetId);
		if (path.empty()) {
			request.Result.Status = AssetLoadStatus::Failed;
			request.Result.Message = "Asset GUID could not be resolved";
		}
		else if (!Supports(kind)) {
			request.Result.Status = AssetLoadStatus::Unsupported;
			request.Result.Message = "This asset type does not support asynchronous loading";
		}
		else {
			for (const auto& work : g_Work) {
				if (work->AssetId == assetId && Revalidate(work) && work->Path == path) {
					request.Preparation = work;
					break;
				}
			}
			if (!request.Preparation) {
				auto work = std::make_shared<Work>();
				work->AssetId = assetId;
				work->Kind = kind;
				work->Path = path;
				if (const auto* project = ProjectManager::GetCurrentProject()) work->ProjectRoot = project->RootDirectory;
				work->SourceStamp = FileStamp::Read(path);
				if (kind == AssetKind::Texture) work->TextureMetaStamp = FileStamp::Read(AssetRegistry::GetMetaPath(path));
				work->RegistryVersion = AssetRegistry::GetChangeVersion();
#if FUNKE_WITH_RENDER
				if (kind == AssetKind::Texture) {
					const TextureMeta meta = AssetRegistry::ReadTextureMeta(path);
					if (meta.HasImportBlock) {
						work->FilterMode = meta.Import.FilterMode;
						work->WrapU = meta.Import.WrapU;
						work->WrapV = meta.Import.WrapV;
					}
				}
				if (kind == AssetKind::Font) work->FontSize = static_cast<float>(FontManager::QuantizeBakeSize(32.0f));
#endif
				g_Work.push_back(work);
				request.Preparation = std::move(work);
			}
		}
		g_Requests.emplace(id, std::move(request));
		return id;
	}

	AssetLoadResult AssetLoader::GetResult(uint64_t operationId) {
		if (std::this_thread::get_id() != g_MainThread) return {};
		const auto found = g_Requests.find(operationId);
		return found == g_Requests.end() ? AssetLoadResult{} : found->second.Result;
	}

	bool AssetLoader::Cancel(uint64_t operationId) {
		if (!IsMainThread()) return false;
		auto found = g_Requests.find(operationId);
		if (found == g_Requests.end() || found->second.Result.Status != AssetLoadStatus::Pending) return false;
		found->second.Result.Status = AssetLoadStatus::Cancelled;
		found->second.Preparation.reset();
		return true;
	}

	void AssetLoader::Release(uint64_t operationId) {
		if (std::this_thread::get_id() == g_MainThread) g_Requests.erase(operationId);
	}

	void AssetLoader::Invalidate(AssetKind kind) {
		if (!IsMainThread()) return;
		for (const auto& work : g_Work) {
			if (work->Kind != kind) continue;
			work->Discarded = true;
			Complete(work, AssetLoadStatus::Cancelled, "Asset cache was invalidated");
		}
	}

	void AssetLoader::Pump() {
		if (!IsMainThread()) return;
		for (auto it = g_Work.begin(); it != g_Work.end();) {
			const auto& work = *it;
			Revalidate(work);
			if (work->Future.valid()) {
				if (work->Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { ++it; continue; }
				try {
					auto prepared = work->Future.get();
					if (HasObservers(work) && Revalidate(work, &prepared)) {
						const bool loaded = prepared.Ready && Publish(*work, prepared);
						Complete(work, loaded ? AssetLoadStatus::Success : AssetLoadStatus::Failed,
							loaded ? "" : (prepared.Error.empty() ? "Asset publication failed" : prepared.Error));
					}
				}
				catch (const std::exception& error) { Complete(work, AssetLoadStatus::Failed, error.what()); }
				catch (...) { Complete(work, AssetLoadStatus::Failed, "Asset publication failed"); }
				it = g_Work.erase(it);
			}
			else if (!HasObservers(work)) it = g_Work.erase(it);
			else ++it;
		}

		size_t active = 0;
		for (const auto& work : g_Work) if (work->Future.valid()) ++active;
		for (auto it = g_Work.begin(); it != g_Work.end();) {
			const auto& work = *it;
			if (work->Future.valid()) { ++it; continue; }
			try {
				if (IsCached(*work)) {
					Complete(work, AssetLoadStatus::Success);
					it = g_Work.erase(it);
					continue;
				}
#if FNK_PLATFORM_HAS_THREADS
				if (active >= k_MaxPreparations) { ++it; continue; }
				work->Future = std::async(std::launch::async, [preparation = work.get()] { return Prepare(*preparation); });
				++active;
#else
				Complete(work, AssetLoadStatus::Unsupported, "Asynchronous asset loading requires thread support");
				it = g_Work.erase(it);
				continue;
#endif
			}
			catch (const std::exception& error) {
				Complete(work, AssetLoadStatus::Failed, error.what());
				it = g_Work.erase(it);
				continue;
			}
			++it;
		}
	}

	void AssetLoader::Shutdown() {
		if (!IsMainThread()) return;
		g_Initialized = false;
		for (const auto& work : g_Work) Complete(work, AssetLoadStatus::Cancelled, "Asset loader shut down");
		// Wait while decoders and their libraries are still alive; workers never publish resources.
		for (const auto& work : g_Work) if (work->Future.valid()) work->Future.wait();
		g_Work.clear();
	}

}
