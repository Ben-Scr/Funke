#include "pch.hpp"
#include "Profiling/Profiler.hpp"
#include "Assets/AssetLoader.hpp"
#include "Diagnostics/LoadingDiagnostics.hpp"

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
			size_t Observers = 0;
#if FUNKE_WITH_RENDER
			Filter FilterMode = Filter::Point;
			Wrap WrapU = Wrap::Clamp;
			Wrap WrapV = Wrap::Clamp;
			float FontSize = 32.0f;
#endif
			std::future<PreparedAsset> Future;
			std::unique_ptr<PreparedAsset> Prepared;
#if FUNKE_WITH_RENDER
			std::unique_ptr<Texture2D> Upload;
			int UploadedRows = 0;
			bool MipsFinished = false;
#endif
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
		std::unordered_map<uint64_t, size_t> g_RetainedAssets;

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
			LoadingScope timing("Assets.Decode");
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
			LoadingScope timing("Assets.Publish");
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

		enum class Publication { Pending, Success, Failed };
		Publication PublishStep(Work& work, double budget) {
			if (IsCached(work)) return Publication::Success;
			auto& prepared = *work.Prepared;
			if (!prepared.Ready) return Publication::Failed;
#if FUNKE_WITH_RENDER
			if (work.Kind == AssetKind::Texture) {
				if (!prepared.Image) return Publication::Failed;
				const auto& image = *prepared.Image;
				if (!work.Upload) {
					LoadingScope timing("Assets.TextureAllocate", budget);
					work.Upload = std::make_unique<Texture2D>();
					return work.Upload->CreateForUpload(image.Width, image.Height,
						work.FilterMode, work.WrapU, work.WrapV, true) ? Publication::Pending : Publication::Failed;
				}
				if (work.UploadedRows < image.Height) {
					constexpr size_t k_ChunkBytes = 1024 * 1024;
					const size_t stride = static_cast<size_t>(image.Width) * 4u;
					const int rows = std::min(image.Height - work.UploadedRows,
						static_cast<int>(std::max<size_t>(1, k_ChunkBytes / stride)));
					LoadingScope timing("Assets.TextureTransfer", budget, stride * rows);
					if (!work.Upload->UploadRows(work.UploadedRows, rows,
						image.Pixels + stride * work.UploadedRows, stride * rows)) return Publication::Failed;
					work.UploadedRows += rows;
					return Publication::Pending;
				}
				if (!work.MipsFinished) {
					LoadingScope timing("Assets.TextureMips", budget);
					work.MipsFinished = work.Upload->FinishUpload();
					return work.MipsFinished ? Publication::Pending : Publication::Failed;
				}
				LoadingScope timing("Assets.Publish", budget);
				return TextureManager::IsValid(TextureManager::PublishUploadedTexture(work.Path, std::move(*work.Upload)))
					? Publication::Success : Publication::Failed;
			}
#endif
			return Publish(work, prepared) ? Publication::Success : Publication::Failed;
		}

		bool HasObservers(const std::shared_ptr<Work>& work) {
			return work->Observers != 0;
		}

		void ReleasePreparation(Request& request) {
			if (!request.Preparation) return;
			--request.Preparation->Observers;
			request.Preparation.reset();
		}

		void Complete(const std::shared_ptr<Work>& work, AssetLoadStatus status, const std::string& message = {}) {
			FUNKE_PROFILE_SCOPE("Assets.Complete");
			for (auto& [id, request] : g_Requests) {
				if (request.Preparation != work) continue;
				if (request.Result.Status == AssetLoadStatus::Pending) {
					request.Result.Status = status;
					request.Result.Message = message;
				}
				ReleasePreparation(request);
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
		g_RetainedAssets.clear();
		g_Initialized = true;
	}

	bool AssetLoader::IsMainThread() {
		return g_Initialized && std::this_thread::get_id() == g_MainThread;
	}

	uint64_t AssetLoader::LoadAsync(uint64_t assetId) {
		FUNKE_PROFILE_SCOPE("Assets.Request");
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
						work->FilterMode = meta.Import.FilterMode == Filter::Default ? Filter::Point : meta.Import.FilterMode;
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
		auto& stored = g_Requests.emplace(id, std::move(request)).first->second;
		if (stored.Preparation) ++stored.Preparation->Observers;
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
		ReleasePreparation(found->second);
		return true;
	}

	void AssetLoader::Release(uint64_t operationId) {
		if (std::this_thread::get_id() != g_MainThread) return;
		const auto found = g_Requests.find(operationId);
		if (found == g_Requests.end()) return;
		ReleasePreparation(found->second);
		g_Requests.erase(found);
	}

	void AssetLoader::Invalidate(AssetKind kind) {
		if (!IsMainThread()) return;
		for (const auto& work : g_Work) {
			if (work->Kind != kind) continue;
			work->Discarded = true;
			Complete(work, AssetLoadStatus::Cancelled, "Asset cache was invalidated");
		}
	}

	void AssetLoader::RetainAsset(uint64_t assetId) {
		if (IsMainThread() && assetId != 0) ++g_RetainedAssets[assetId];
	}

	void AssetLoader::ReleaseAsset(uint64_t assetId) {
		if (std::this_thread::get_id() != g_MainThread) return;
		const auto it = g_RetainedAssets.find(assetId);
		if (it != g_RetainedAssets.end() && --it->second == 0) g_RetainedAssets.erase(it);
	}

	bool AssetLoader::IsAssetRetained(uint64_t assetId) {
		return std::this_thread::get_id() == g_MainThread && g_RetainedAssets.contains(assetId);
	}

	void AssetLoader::Pump(double budgetMs, size_t maxPublications) {
		LoadingScope timing("Assets.Pump", std::max(0.0, budgetMs));
		if (!IsMainThread()) return;
#if FUNKE_WITH_RENDER
		TextureUploadBatch uploadBatch;
#endif
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double, std::milli>(std::max(0.0, budgetMs));
		size_t publications = 0;
		for (auto it = g_Work.begin(); it != g_Work.end();) {
			const auto& work = *it;
			Revalidate(work);
			if (work->Future.valid() && work->Future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
				++it; continue;
			}
			if (!HasObservers(work)) { it = g_Work.erase(it); continue; }
			if (!work->Future.valid() && !work->Prepared) { ++it; continue; }
			if (publications >= maxPublications || (publications && std::chrono::steady_clock::now() >= deadline)) {
				++it; continue;
			}
			try {
				if (!work->Prepared) work->Prepared = std::make_unique<PreparedAsset>(work->Future.get());
				if (!Revalidate(work, work->Prepared.get())) { it = g_Work.erase(it); continue; }
				++publications;
				const auto result = PublishStep(*work, std::max(0.0, budgetMs));
				if (result == Publication::Pending) { ++it; continue; }
				const bool loaded = result == Publication::Success;
				Complete(work, loaded ? AssetLoadStatus::Success : AssetLoadStatus::Failed,
					loaded ? "" : (work->Prepared->Error.empty() ? "Asset publication failed" : work->Prepared->Error));
			}
			catch (const std::exception& error) { Complete(work, AssetLoadStatus::Failed, error.what()); }
			catch (...) { Complete(work, AssetLoadStatus::Failed, "Asset publication failed"); }
			LoadingScope release("Assets.Release", std::max(0.0, budgetMs));
			it = g_Work.erase(it);
		}

		size_t active = 0;
		for (const auto& work : g_Work) if (work->Future.valid()) ++active;
		for (auto it = g_Work.begin(); it != g_Work.end();) {
			const auto& work = *it;
			if (work->Future.valid() || work->Prepared) { ++it; continue; }
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
