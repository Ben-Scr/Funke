#include <pch.hpp>
#include "Gui/PrefabInspector.hpp"
#include "Gui/PrefabVariantState.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/General/PrefabInstanceComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Log.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"
#include "Serialization/PrefabTemplateCache.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/SceneSerializerShared.hpp"
#include <unordered_set>
namespace Funke {
	using PrefabAuthoringDetail::VariantEditState;
	namespace {
		uint64_t VariantBaseGuid(const Json::Value& source) {
			const auto* reference = source.FindMember("PrefabRef");
			if (!reference) return 0;
			if (!reference->IsString()) return reference->AsUInt64Or(0);
			const std::string value = reference->AsStringOr();
			try { size_t used = 0; const auto guid = std::stoull(value, &used); if (used == value.size()) return guid; } catch (...) {}
			return AssetRegistry::GetOrCreateAssetUUID(value);
		}

		struct PrefabSceneTransaction {
			std::shared_ptr<Scene> LiveScene;
			std::unique_ptr<Scene> ValidationScene;
			Json::Value SerializedState;
			size_t ExpectedInstanceCount = 0;
			bool RecoveryBegun = false;
		};

		bool CollectPrefabInstanceRoots(Scene& scene, uint64_t prefabGuid,
			std::vector<EntityHandle>& roots) {
			auto view = scene.GetRegistry().view<EntityMetaDataComponent>();
			for (entt::entity entity : view) {
				const EntityMetaData& metadata = view.get<EntityMetaDataComponent>(entity).MetaData;
				if (metadata.Origin != EntityOrigin::Prefab
					|| static_cast<uint64_t>(metadata.PrefabGUID) != prefabGuid) {
					continue;
				}

				const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(scene, entity);
				if (root == entt::null) {
					return false;
				}
				if (std::find(roots.begin(), roots.end(), root) == roots.end()) {
					roots.push_back(root);
				}
			}
			return true;
		}

		using PrefabBaselines = std::unordered_map<uint64_t, Json::Value>;
		bool CollectAffectedRoots(Scene& scene, const PrefabBaselines& baselines, std::vector<EntityHandle>& roots) {
			for (const auto& [guid, source] : baselines) if (!CollectPrefabInstanceRoots(scene, guid, roots)) return false;
			auto depth = [&](EntityHandle entity) {
				size_t result = 0;
				while (const auto* hierarchy = scene.GetRegistry().try_get<HierarchyComponent>(entity)) {
					if (!scene.IsValid(hierarchy->Parent) || ++result > 1024) break;
					entity = hierarchy->Parent;
				}
				return result;
			};
			std::stable_sort(roots.begin(), roots.end(), [&](EntityHandle left, EntityHandle right) { return depth(left) > depth(right); });
			return true;
		}
		bool ValidateLoadedPersistentIdentities(std::string_view logTag) {
			std::unordered_map<uint64_t, std::string> owners;
			for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
				const std::shared_ptr<Scene> scene = weakScene.lock();
				if (!scene || !scene->IsLoaded()) continue;
				for (const EntityHandle entity : scene->GetRegistry().view<UUIDComponent>()) {
					const uint64_t id = scene->GetEntityPersistentID(entity);
					const auto [owner, inserted] = owners.emplace(id, scene->GetName());
					if (id == 0 || !inserted) {
						FNK_CORE_ERROR_TAG(logTag,
							"Prefab save aborted: persistent entity ID {} is invalid or shared by scenes '{}' and '{}'.",
							id, inserted ? scene->GetName() : owner->second, scene->GetName());
						return false;
					}
				}
			}
			return true;
		}

		bool RestorePrefabSource(const std::string& prefabPath, const Json::Value& previousSource,
			SceneSerializationFormat previousFormat, uint64_t prefabGuid, std::string_view logTag) {
			const bool restored = SceneSerializerStorage::WriteRootToFile(
				prefabPath, previousSource, previousFormat);
			if (restored) {
				KnownFileStamps::Record(prefabPath);
			}
			SceneSerializer::ForgetCachedPrefabRoot(prefabPath);
			PrefabTemplateCache::Get().Invalidate(prefabGuid);
			if (!restored) {
				FNK_CORE_ERROR_TAG(logTag, "Failed to restore prefab source {} after propagation failure.", prefabPath);
			}
			return restored;
		}

		bool RestorePrefabScenes(const std::vector<PrefabSceneTransaction>& transactions,
			std::string_view logTag) {
			bool allRestored = true;
			std::vector<std::shared_ptr<Scene>> suspendedScenes;
			suspendedScenes.reserve(transactions.size());
			for (const PrefabSceneTransaction& transaction : transactions) {
				if (!transaction.RecoveryBegun) continue;
				if (!SceneManager::RollbackLoadedSceneRecovery(transaction.LiveScene)) {
					FNK_CORE_ERROR_TAG(logTag, "Failed to restore scene '{}' after prefab propagation failure.",
						transaction.LiveScene->GetName());
					allRestored = false;
					continue;
				}
				suspendedScenes.push_back(transaction.LiveScene);
			}
			if (!allRestored) return false;

			std::vector<std::shared_ptr<Scene>> activatedScenes;
			activatedScenes.reserve(suspendedScenes.size());
			for (const std::shared_ptr<Scene>& scene : suspendedScenes) {
				if (!SceneManager::ActivateSuspendedScene(scene)) {
					FNK_CORE_ERROR_TAG(logTag, "Failed to reactivate restored scene '{}'.", scene->GetName());
					allRestored = false;
					break;
				}
				activatedScenes.push_back(scene);
			}
			if (!allRestored) {
				for (auto it = activatedScenes.rbegin(); it != activatedScenes.rend(); ++it) {
					if (!SceneManager::ResuspendSceneForRecovery(*it)) {
						FNK_CORE_ERROR_TAG(logTag, "Failed to resuspend restored scene '{}'.", (*it)->GetName());
					}
				}
				return false;
			}

			for (const std::shared_ptr<Scene>& scene : activatedScenes) {
				if (!SceneManager::CommitSuspendedSceneActivation(scene)) {
					FNK_CORE_ERROR_TAG(logTag, "Failed to finalize restored scene '{}'.", scene->GetName());
					allRestored = false;
				}
			}
			return allRestored;
		}

		bool ValidatePrefabRefreshes(const std::vector<PrefabSceneTransaction>& transactions,
			const PrefabBaselines& baselines, std::string_view logTag) {
			try {
				for (const PrefabSceneTransaction& transaction : transactions) {
					std::vector<EntityHandle> roots;
					if (!transaction.ValidationScene
						|| !CollectAffectedRoots(*transaction.ValidationScene, baselines, roots)
						|| roots.size() != transaction.ExpectedInstanceCount) {
						return false;
					}
					for (const EntityHandle root : roots) {
						const EntityHandle replacement = SceneSerializer::RefreshPrefabInstance(
							*transaction.ValidationScene, root, baselines.at(static_cast<uint64_t>(transaction.ValidationScene->GetPrefabGUID(root))));
						if (replacement == entt::null || replacement == root) return false;
					}
					roots.clear();
					if (!CollectAffectedRoots(*transaction.ValidationScene, baselines, roots)
						|| roots.size() != transaction.ExpectedInstanceCount) {
						return false;
					}
				}
				return true;
			}
			catch (const std::exception& exception) {
				FNK_CORE_ERROR_TAG(logTag, "Prefab propagation preflight failed: {}", exception.what());
			}
			catch (...) {
				FNK_CORE_ERROR_TAG(logTag, "Prefab propagation preflight failed.");
			}
			return false;
		}
	}

	// Suspends every loaded scene holding instances of the prefab, publishes the new source, then
	// refreshes the instances against previousSource; any failure rolls scenes (and source) back.
	// An empty publishSource means the file already changed on disk, so it is never rolled back.
	static bool PropagatePrefabSource(uint64_t prefabGuid, const std::string& prefabPath,
		const Json::Value& previousSource, SceneSerializationFormat previousFormat,
		const std::function<bool()>& publishSource, std::string_view logTag, size_t* outRefreshedCount,
		const Json::Value* resolvedPreviousSource = nullptr) {
		Json::Value expandedPrevious;
		if (!resolvedPreviousSource && !publishSource) {
			if (!SceneSerializer::ResolvePrefabSource(prefabGuid, expandedPrevious, prefabGuid, previousSource)) return false;
			resolvedPreviousSource = &expandedPrevious;
		}
		const Json::Value& previousBaseline = resolvedPreviousSource ? *resolvedPreviousSource : previousSource;
		if (!ValidateLoadedPersistentIdentities(logTag)) return false;
		const bool ownsSource = static_cast<bool>(publishSource);

		PrefabBaselines baselines{{prefabGuid, previousBaseline}};
		std::unordered_set<uint64_t> inspected{prefabGuid};
		for (const auto& weak : SceneManager::GetLoadedScenes()) if (auto loaded = weak.lock()) {
			for (auto [handle, metadata] : loaded->GetRegistry().view<EntityMetaDataComponent>().each()) {
				const uint64_t guid = static_cast<uint64_t>(metadata.MetaData.PrefabGUID);
				if (!guid || !inspected.insert(guid).second) continue;
				std::vector<uint64_t> dependencies;
				if (!SceneSerializer::GetPrefabDependencies(guid, dependencies)
					|| std::find(dependencies.begin(), dependencies.end(), prefabGuid) == dependencies.end()) continue;
				Json::Value expanded;
				if (!(ownsSource ? SceneSerializer::ResolvePrefabSource(guid, expanded)
					: SceneSerializer::ResolvePrefabSource(guid, expanded, prefabGuid, previousSource))) return false;
				baselines.emplace(guid, std::move(expanded));
			}
		}
		std::vector<PrefabSceneTransaction> sceneTransactions;
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			std::shared_ptr<Scene> scene = weakScene.lock();
			if (!scene || !scene->IsLoaded()) continue;
			std::vector<EntityHandle> roots;
			if (!CollectAffectedRoots(*scene, baselines, roots)) {
				FNK_CORE_ERROR_TAG(logTag, "Scene '{}' contains a malformed instance of {}.",
					scene->GetName(), prefabPath);
				return false;
			}
			if (roots.empty()) continue;
			if (scene->IsLifecycleSuspended()) {
				FNK_CORE_ERROR_TAG(logTag, "Scene '{}' is already lifecycle-suspended; prefab propagation was aborted.",
					scene->GetName());
				return false;
			}
			try {
				PrefabSceneTransaction transaction;
				transaction.LiveScene = scene;
				transaction.SerializedState = SceneSerializer::SerializeScene(*transaction.LiveScene);
				transaction.ExpectedInstanceCount = roots.size();
				transaction.ValidationScene = Scene::CreateDetachedScene(scene->GetName());
				if (!SceneSerializer::DeserializeScene(*transaction.ValidationScene,
					transaction.SerializedState, "prefab propagation preflight")) {
					FNK_CORE_ERROR_TAG(logTag, "Failed to stage scene '{}' for prefab propagation.", scene->GetName());
					return false;
				}
				sceneTransactions.push_back(std::move(transaction));
			}
			catch (const std::exception& exception) {
				FNK_CORE_ERROR_TAG(logTag, "Failed to snapshot scene '{}': {}", scene->GetName(), exception.what());
				return false;
			}
			catch (...) {
				FNK_CORE_ERROR_TAG(logTag, "Failed to snapshot scene '{}'.", scene->GetName());
				return false;
			}
		}

		bool sourcePublished = false;
		auto restoreSource = [&]() {
			return !ownsSource || !sourcePublished
				|| RestorePrefabSource(prefabPath, previousSource, previousFormat, prefabGuid, logTag);
		};
		try {
		for (PrefabSceneTransaction& transaction : sceneTransactions) {
			if (SceneManager::BeginLoadedSceneRecovery(transaction.LiveScene)) {
				transaction.RecoveryBegun = true;
				continue;
			}
			transaction.RecoveryBegun = transaction.LiveScene->IsLifecycleSuspended();
			const bool scenesRestored = RestorePrefabScenes(sceneTransactions, logTag);
			FNK_CORE_ERROR_TAG(logTag, "Failed to suspend scene '{}' for prefab propagation; scene recovery: {}.",
				transaction.LiveScene->GetName(), scenesRestored);
			return false;
		}

		if (ownsSource && !publishSource()) {
			const bool scenesRestored = RestorePrefabScenes(sceneTransactions, logTag);
			FNK_CORE_ERROR_TAG(logTag, "Prefab source save failed; scene recovery: {}.", scenesRestored);
			return false;
		}
		sourcePublished = true;
		PrefabTemplateCache::Get().Invalidate(prefabGuid);

		if (!ValidatePrefabRefreshes(sceneTransactions, baselines, logTag)) {
			const bool sourceRestored = restoreSource();
			const bool scenesRestored = RestorePrefabScenes(sceneTransactions, logTag);
			FNK_CORE_ERROR_TAG(logTag, "Prefab propagation preflight failed for {}; source rollback: {}; scene rollback: {}.",
				prefabPath, sourceRestored, scenesRestored);
			return false;
		}

		size_t failedInstanceCount = 0;
		size_t refreshedInstanceCount = 0;
		for (const PrefabSceneTransaction& transaction : sceneTransactions) {
			std::vector<EntityHandle> roots;
			if (!CollectAffectedRoots(*transaction.LiveScene, baselines, roots)
				|| roots.size() != transaction.ExpectedInstanceCount) {
				failedInstanceCount += std::max<size_t>(transaction.ExpectedInstanceCount, 1);
				continue;
			}
			bool sceneRefreshFailed = false;
			for (EntityHandle root : roots) {
				const EntityHandle replacement = SceneSerializer::RefreshPrefabInstance(
					*transaction.LiveScene, root, baselines.at(static_cast<uint64_t>(transaction.LiveScene->GetPrefabGUID(root))));
				if (replacement == entt::null || replacement == root) {
					++failedInstanceCount;
					sceneRefreshFailed = true;
				}
				else {
					++refreshedInstanceCount;
				}
			}
			if (!sceneRefreshFailed) {
				transaction.LiveScene->MarkDirty();
			}
		}

		if (failedInstanceCount != 0) {
			const bool sourceRestored = restoreSource();
			const bool scenesRestored = RestorePrefabScenes(sceneTransactions, logTag);
			FNK_CORE_ERROR_TAG(logTag, "Failed to refresh {} live prefab instance(s) from {}; source rollback: {}; scene rollback: {}.",
				failedInstanceCount, prefabPath, sourceRestored, scenesRestored);
			return false;
		}

		std::vector<std::shared_ptr<Scene>> activatedScenes;
		activatedScenes.reserve(sceneTransactions.size());
		for (const PrefabSceneTransaction& transaction : sceneTransactions) {
			if (!SceneManager::ActivateSuspendedScene(transaction.LiveScene)) {
				const bool sourceRestored = restoreSource();
				const bool scenesRestored = RestorePrefabScenes(sceneTransactions, logTag);
				FNK_CORE_ERROR_TAG(logTag, "Failed to activate refreshed scene '{}'; source rollback: {}; scene rollback: {}.",
					transaction.LiveScene->GetName(), sourceRestored, scenesRestored);
				return false;
			}
			activatedScenes.push_back(transaction.LiveScene);
		}

		bool committed = true;
		for (const std::shared_ptr<Scene>& scene : activatedScenes) {
			if (!SceneManager::CommitSuspendedSceneActivation(scene)) {
				FNK_CORE_ERROR_TAG(logTag, "Failed to finalize prefab propagation for scene '{}'.", scene->GetName());
				committed = false;
			}
		}
		if (outRefreshedCount) *outRefreshedCount = refreshedInstanceCount;
		return committed;
		}
		catch (const std::exception& exception) {
			const bool sourceRestored = restoreSource();
			const bool scenesRestored = RestorePrefabScenes(sceneTransactions, logTag);
			FNK_CORE_ERROR_TAG(logTag, "Prefab propagation failed: {}; source rollback: {}; scene rollback: {}.",
				exception.what(), sourceRestored, scenesRestored);
		}
		catch (...) {
			const bool sourceRestored = restoreSource();
			const bool scenesRestored = RestorePrefabScenes(sceneTransactions, logTag);
			FNK_CORE_ERROR_TAG(logTag, "Prefab propagation failed; source rollback: {}; scene rollback: {}.",
				sourceRestored, scenesRestored);
		}
		return false;
	}

	bool SavePrefabSourceTransactionally(Scene& sourceScene, EntityHandle sourceRoot,
		const std::string& prefabPath, std::string_view logTag) {
		if (sourceRoot == entt::null || !sourceScene.IsValid(sourceRoot) || prefabPath.empty()) {
			return false;
		}

		Json::Value previousSource;
		std::string readError;
		if (!SceneSerializerStorage::ReadRootFromFile(prefabPath, previousSource, &readError)
			|| !previousSource.IsObject()) {
			FNK_CORE_WARN_TAG(logTag, "Pre-save read of '{}' failed ({}); leaving prefab dirty for retry.",
				prefabPath, readError);
			return false;
		}

		const SceneSerializationFormat previousFormat = SceneSerializer::IsBinarySerializedFile(prefabPath)
			? SceneSerializationFormat::Binary
			: SceneSerializationFormat::Json;
		const uint64_t prefabGuid = AssetRegistry::GetOrCreateAssetUUID(prefabPath);
		if (prefabGuid == 0) {
			FNK_CORE_ERROR_TAG(logTag, "Failed to resolve the prefab GUID for {}.", prefabPath);
			return false;
		}
		PrefabTemplateCache::Get().Invalidate(prefabGuid);

		if (auto* variant = sourceScene.GetRegistry().ctx().find<VariantEditState>()) {
			Json::Value currentBase;
			if (!SceneSerializer::ResolvePrefabSource(variant->BaseGuid, currentBase) || !(currentBase == variant->BaseSource)) {
				FNK_CORE_ERROR_TAG(logTag, "The prefab variant base changed or is unavailable. Reopen the variant before saving.");
				return false;
			}
			const auto serialized = SceneSerializer::SerializePrefabVariant(sourceScene, sourceRoot, variant->BaseGuid, prefabGuid, variant->EditableBase);
			if (!serialized.IsObject()) {
				FNK_CORE_ERROR_TAG(logTag, "Variant save refused: adding, deleting or reparenting inherited entities is not supported by the variant format.");
				return false;
			}
			auto publish = [&] {
				if (!SceneSerializerStorage::WriteRootToFile(prefabPath, serialized, previousFormat)) return false;
				KnownFileStamps::Record(prefabPath); SceneSerializer::ForgetCachedPrefabRoot(prefabPath);
				PrefabTemplateCache::Get().Invalidate(prefabGuid); return true;
			};
			const bool saved = Application::GetIsPlaying() ? publish() : PropagatePrefabSource(prefabGuid, prefabPath,
				previousSource, previousFormat, publish, logTag, nullptr, &variant->PreviousSource);
			if (saved) SceneSerializer::ResolvePrefabSource(prefabGuid, variant->PreviousSource);
			return saved;
		}
		if (Application::GetIsPlaying()) {
			if (!SceneSerializer::SaveEntityToFile(sourceScene, sourceRoot, prefabPath)) return false;
			PrefabTemplateCache::Get().Invalidate(prefabGuid);
			return true;
		}
		return PropagatePrefabSource(prefabGuid, prefabPath, previousSource, previousFormat,
			[&]() { return SceneSerializer::SaveEntityToFile(sourceScene, sourceRoot, prefabPath); },
			logTag, nullptr);
	}

	bool RefreshPrefabInstancesFromDisk(const std::string& prefabPath, const Json::Value& previousSource,
		std::string_view logTag, size_t* outRefreshedCount) {
		if (outRefreshedCount) *outRefreshedCount = 0;
		if (prefabPath.empty() || !previousSource.IsObject() || Application::GetIsPlaying()) return false;
		// Often a write still in progress; the caller retries on the next change.
		Json::Value currentSource;
		std::string readError;
		if (!SceneSerializerStorage::ReadRootFromFile(prefabPath, currentSource, &readError) || !currentSource.IsObject()) {
			FNK_CORE_WARN_TAG(logTag, "Prefab {} changed on disk but cannot be read ({}); its instances keep the previous version for now.",
				prefabPath, readError);
			return false;
		}
		const uint64_t prefabGuid = AssetRegistry::GetOrCreateAssetUUID(prefabPath);
		if (prefabGuid == 0) {
			FNK_CORE_ERROR_TAG(logTag, "Failed to resolve the prefab GUID for {}.", prefabPath);
			return false;
		}
		PrefabTemplateCache::Get().Invalidate(prefabGuid);
		return PropagatePrefabSource(prefabGuid, prefabPath, previousSource, SceneSerializationFormat::Json,
			{}, logTag, outRefreshedCount);
	}

	EntityHandle LoadPrefabSourceIntoDetachedScene(const std::string& prefabPath, const std::string& sceneName,
		std::string_view logTag, std::unique_ptr<Scene>& outScene) {
		if (!File::Exists(prefabPath)) {
			FNK_CORE_WARN_TAG(logTag, "Prefab file not found: {}", prefabPath);
			return entt::null;
		}

		Json::Value root;
		std::string readError;
		if (!SceneSerializerStorage::ReadRootFromFile(prefabPath, root, &readError) || !root.IsObject()) {
			FNK_CORE_ERROR_TAG(logTag, "Failed to parse {}: {}", prefabPath, readError);
			return entt::null;
		}
		const uint64_t baseGuid = VariantBaseGuid(root);
		if (!baseGuid && !SceneSerializer::HasPrefabEntityPayload(root)) return entt::null;
		auto stagedScene = Scene::CreateDetachedScene(sceneName);
		EntityHandle stagedRoot = entt::null;
		if (baseGuid) {
			const uint64_t guid = AssetRegistry::GetOrCreateAssetUUID(prefabPath);
			VariantEditState state;
			state.BaseGuid = baseGuid;
			if (!SceneSerializer::ResolvePrefabSource(baseGuid, state.BaseSource)
				|| !SceneSerializer::ResolvePrefabSource(guid, state.PreviousSource)) return entt::null;
			auto baseScene = Scene::CreateDetachedScene("##VariantBase");
			const auto baseRoot = SceneSerializer::InstantiatePrefab(*baseScene, baseGuid);
			if (baseRoot == entt::null) return entt::null;
			state.EditableBase = SceneSerializer::SerializePrefabSource(*baseScene, baseRoot);
			stagedRoot = SceneSerializer::InstantiatePrefab(*stagedScene, guid);
			stagedScene->GetRegistry().ctx().emplace<VariantEditState>(std::move(state));
		}
		else stagedRoot = SceneSerializer::DeserializeEntityFromValue(*stagedScene, root);
		if (stagedRoot == entt::null || !stagedScene->IsValid(stagedRoot)) {
			FNK_CORE_ERROR_TAG(logTag, "Failed to deserialize prefab root from {}", prefabPath);
			return entt::null;
		}
		stagedScene->ClearDirty();
		outScene = std::move(stagedScene);
		return stagedRoot;
	}

	bool ResetPrefabVariantComponent(Scene& scene, EntityHandle root, const std::string& componentName) {
		const auto* state = scene.GetRegistry().ctx().find<VariantEditState>();
		const auto* base = state ? state->BaseSource.FindMember("Entity") : nullptr;
		const auto* value = base ? base->FindMember(componentName) : nullptr;
		if (!value || !scene.IsValid(root)) return false;
		const auto fixups = scene.GetPendingEntityRefFixupCount();
		Scene::EntityRefRemapScope remap(scene);
		const auto guid = scene.GetPrefabGUID(root);
		for (auto [handle, instance] : scene.GetRegistry().view<PrefabInstanceComponent>().each()) {
			if (instance.PrefabGUID == guid && instance.SourceEntityId)
				scene.RegisterEntityRefRemap(instance.SourceEntityId, scene.GetEntityPersistentID(handle));
		}
		if (!SceneSerializer::DeserializeComponent(scene, root, componentName, *value)) {
			scene.DiscardPendingEntityRefFixupsFrom(fixups); return false;
		}
		scene.RunPendingEntityRefFixupsFrom(fixups);
		scene.MarkDirty();
		return true;
	}
}
