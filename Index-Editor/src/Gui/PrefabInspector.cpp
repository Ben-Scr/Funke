#include <pch.hpp>
#include "Gui/PrefabInspector.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Log.hpp"
#include "Editor/EditorComponentRegistration.hpp"
#include "Inspector/ReferencePicker.hpp"
#include "Gui/AddComponentPopup.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/Icons.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/PrefabTemplateCache.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/SceneSerializerShared.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

namespace Index {
	namespace {
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

		bool ValidateLoadedPersistentIdentities(std::string_view logTag) {
			std::unordered_map<uint64_t, std::string> owners;
			for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
				const std::shared_ptr<Scene> scene = weakScene.lock();
				if (!scene || !scene->IsLoaded()) continue;
				for (const EntityHandle entity : scene->GetRegistry().view<UUIDComponent>()) {
					const uint64_t id = scene->GetEntityPersistentID(entity);
					const auto [owner, inserted] = owners.emplace(id, scene->GetName());
					if (id == 0 || !inserted) {
						IDX_CORE_ERROR_TAG(logTag,
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
				IDX_CORE_ERROR_TAG(logTag, "Failed to restore prefab source {} after propagation failure.", prefabPath);
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
					IDX_CORE_ERROR_TAG(logTag, "Failed to restore scene '{}' after prefab propagation failure.",
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
					IDX_CORE_ERROR_TAG(logTag, "Failed to reactivate restored scene '{}'.", scene->GetName());
					allRestored = false;
					break;
				}
				activatedScenes.push_back(scene);
			}
			if (!allRestored) {
				for (auto it = activatedScenes.rbegin(); it != activatedScenes.rend(); ++it) {
					if (!SceneManager::ResuspendSceneForRecovery(*it)) {
						IDX_CORE_ERROR_TAG(logTag, "Failed to resuspend restored scene '{}'.", (*it)->GetName());
					}
				}
				return false;
			}

			for (const std::shared_ptr<Scene>& scene : activatedScenes) {
				if (!SceneManager::CommitSuspendedSceneActivation(scene)) {
					IDX_CORE_ERROR_TAG(logTag, "Failed to finalize restored scene '{}'.", scene->GetName());
					allRestored = false;
				}
			}
			return allRestored;
		}

		bool ValidatePrefabRefreshes(const std::vector<PrefabSceneTransaction>& transactions,
			uint64_t prefabGuid, const Json::Value& previousSource, std::string_view logTag) {
			try {
				for (const PrefabSceneTransaction& transaction : transactions) {
					std::vector<EntityHandle> roots;
					if (!transaction.ValidationScene
						|| !CollectPrefabInstanceRoots(*transaction.ValidationScene, prefabGuid, roots)
						|| roots.size() != transaction.ExpectedInstanceCount) {
						return false;
					}
					for (const EntityHandle root : roots) {
						const EntityHandle replacement = SceneSerializer::RefreshPrefabInstance(
							*transaction.ValidationScene, root, previousSource);
						if (replacement == entt::null || replacement == root) return false;
					}
					roots.clear();
					if (!CollectPrefabInstanceRoots(*transaction.ValidationScene, prefabGuid, roots)
						|| roots.size() != transaction.ExpectedInstanceCount) {
						return false;
					}
				}
				return true;
			}
			catch (const std::exception& exception) {
				IDX_CORE_ERROR_TAG(logTag, "Prefab propagation preflight failed: {}", exception.what());
			}
			catch (...) {
				IDX_CORE_ERROR_TAG(logTag, "Prefab propagation preflight failed.");
			}
			return false;
		}
	}

	// Suspends every loaded scene holding instances of the prefab, publishes the new source, then
	// refreshes the instances against previousSource; any failure rolls scenes (and source) back.
	// An empty publishSource means the file already changed on disk, so it is never rolled back.
	static bool PropagatePrefabSource(uint64_t prefabGuid, const std::string& prefabPath,
		const Json::Value& previousSource, SceneSerializationFormat previousFormat,
		const std::function<bool()>& publishSource, std::string_view logTag, size_t* outRefreshedCount) {
		if (!ValidateLoadedPersistentIdentities(logTag)) return false;
		const bool ownsSource = static_cast<bool>(publishSource);

		std::vector<PrefabSceneTransaction> sceneTransactions;
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			std::shared_ptr<Scene> scene = weakScene.lock();
			if (!scene || !scene->IsLoaded()) continue;
			std::vector<EntityHandle> roots;
			if (!CollectPrefabInstanceRoots(*scene, prefabGuid, roots)) {
				IDX_CORE_ERROR_TAG(logTag, "Scene '{}' contains a malformed instance of {}.",
					scene->GetName(), prefabPath);
				return false;
			}
			if (roots.empty()) continue;
			if (scene->IsLifecycleSuspended()) {
				IDX_CORE_ERROR_TAG(logTag, "Scene '{}' is already lifecycle-suspended; prefab propagation was aborted.",
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
					IDX_CORE_ERROR_TAG(logTag, "Failed to stage scene '{}' for prefab propagation.", scene->GetName());
					return false;
				}
				sceneTransactions.push_back(std::move(transaction));
			}
			catch (const std::exception& exception) {
				IDX_CORE_ERROR_TAG(logTag, "Failed to snapshot scene '{}': {}", scene->GetName(), exception.what());
				return false;
			}
			catch (...) {
				IDX_CORE_ERROR_TAG(logTag, "Failed to snapshot scene '{}'.", scene->GetName());
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
			IDX_CORE_ERROR_TAG(logTag, "Failed to suspend scene '{}' for prefab propagation; scene recovery: {}.",
				transaction.LiveScene->GetName(), scenesRestored);
			return false;
		}

		if (ownsSource && !publishSource()) {
			const bool scenesRestored = RestorePrefabScenes(sceneTransactions, logTag);
			IDX_CORE_ERROR_TAG(logTag, "Prefab source save failed; scene recovery: {}.", scenesRestored);
			return false;
		}
		sourcePublished = true;
		PrefabTemplateCache::Get().Invalidate(prefabGuid);

		if (!ValidatePrefabRefreshes(sceneTransactions, prefabGuid, previousSource, logTag)) {
			const bool sourceRestored = restoreSource();
			const bool scenesRestored = RestorePrefabScenes(sceneTransactions, logTag);
			IDX_CORE_ERROR_TAG(logTag, "Prefab propagation preflight failed for {}; source rollback: {}; scene rollback: {}.",
				prefabPath, sourceRestored, scenesRestored);
			return false;
		}

		size_t failedInstanceCount = 0;
		size_t refreshedInstanceCount = 0;
		for (const PrefabSceneTransaction& transaction : sceneTransactions) {
			std::vector<EntityHandle> roots;
			if (!CollectPrefabInstanceRoots(*transaction.LiveScene, prefabGuid, roots)
				|| roots.size() != transaction.ExpectedInstanceCount) {
				failedInstanceCount += std::max<size_t>(transaction.ExpectedInstanceCount, 1);
				continue;
			}
			bool sceneRefreshFailed = false;
			for (EntityHandle root : roots) {
				const EntityHandle replacement = SceneSerializer::RefreshPrefabInstance(
					*transaction.LiveScene, root, previousSource);
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
			IDX_CORE_ERROR_TAG(logTag, "Failed to refresh {} live prefab instance(s) from {}; source rollback: {}; scene rollback: {}.",
				failedInstanceCount, prefabPath, sourceRestored, scenesRestored);
			return false;
		}

		std::vector<std::shared_ptr<Scene>> activatedScenes;
		activatedScenes.reserve(sceneTransactions.size());
		for (const PrefabSceneTransaction& transaction : sceneTransactions) {
			if (!SceneManager::ActivateSuspendedScene(transaction.LiveScene)) {
				const bool sourceRestored = restoreSource();
				const bool scenesRestored = RestorePrefabScenes(sceneTransactions, logTag);
				IDX_CORE_ERROR_TAG(logTag, "Failed to activate refreshed scene '{}'; source rollback: {}; scene rollback: {}.",
					transaction.LiveScene->GetName(), sourceRestored, scenesRestored);
				return false;
			}
			activatedScenes.push_back(transaction.LiveScene);
		}

		bool committed = true;
		for (const std::shared_ptr<Scene>& scene : activatedScenes) {
			if (!SceneManager::CommitSuspendedSceneActivation(scene)) {
				IDX_CORE_ERROR_TAG(logTag, "Failed to finalize prefab propagation for scene '{}'.", scene->GetName());
				committed = false;
			}
		}
		if (outRefreshedCount) *outRefreshedCount = refreshedInstanceCount;
		return committed;
		}
		catch (const std::exception& exception) {
			const bool sourceRestored = restoreSource();
			const bool scenesRestored = RestorePrefabScenes(sceneTransactions, logTag);
			IDX_CORE_ERROR_TAG(logTag, "Prefab propagation failed: {}; source rollback: {}; scene rollback: {}.",
				exception.what(), sourceRestored, scenesRestored);
		}
		catch (...) {
			const bool sourceRestored = restoreSource();
			const bool scenesRestored = RestorePrefabScenes(sceneTransactions, logTag);
			IDX_CORE_ERROR_TAG(logTag, "Prefab propagation failed; source rollback: {}; scene rollback: {}.",
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
			IDX_CORE_WARN_TAG(logTag, "Pre-save read of '{}' failed ({}); leaving prefab dirty for retry.",
				prefabPath, readError);
			return false;
		}

		const SceneSerializationFormat previousFormat = SceneSerializer::IsBinarySerializedFile(prefabPath)
			? SceneSerializationFormat::Binary
			: SceneSerializationFormat::Json;
		const uint64_t prefabGuid = AssetRegistry::GetOrCreateAssetUUID(prefabPath);
		if (prefabGuid == 0) {
			IDX_CORE_ERROR_TAG(logTag, "Failed to resolve the prefab GUID for {}.", prefabPath);
			return false;
		}
		PrefabTemplateCache::Get().Invalidate(prefabGuid);

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
			IDX_CORE_WARN_TAG(logTag, "Prefab {} changed on disk but cannot be read ({}); its instances keep the previous version for now.",
				prefabPath, readError);
			return false;
		}
		const uint64_t prefabGuid = AssetRegistry::GetOrCreateAssetUUID(prefabPath);
		if (prefabGuid == 0) {
			IDX_CORE_ERROR_TAG(logTag, "Failed to resolve the prefab GUID for {}.", prefabPath);
			return false;
		}
		PrefabTemplateCache::Get().Invalidate(prefabGuid);
		return PropagatePrefabSource(prefabGuid, prefabPath, previousSource, SceneSerializationFormat::Json,
			{}, logTag, outRefreshedCount);
	}

	PrefabInspector::PrefabInspector() = default;
	PrefabInspector::~PrefabInspector() = default;

	bool PrefabInspector::Open(const std::string& prefabPath) {
		if (!File::Exists(prefabPath)) {
			IDX_CORE_WARN_TAG("PrefabInspector", "Prefab file not found: {}", prefabPath);
			return false;
		}

		Json::Value root;
		std::string readError;
		if (!SceneSerializerStorage::ReadRootFromFile(prefabPath, root, &readError) || !root.IsObject()) {
			IDX_CORE_ERROR_TAG("PrefabInspector", "Failed to parse {}: {}", prefabPath, readError);
			return false;
		}

		// The .prefab envelope keeps both "Entity" (preferred) and a legacy
		// "prefab" key with the same payload. Prefer the new key, fall back.
		const Json::Value* entityValue = root.FindMember("Entity");
		if (!entityValue) entityValue = root.FindMember("prefab");
		if (!entityValue || !entityValue->IsObject()) {
			IDX_CORE_WARN_TAG("PrefabInspector", "No Entity/prefab block in {}", prefabPath);
			return false;
		}

		auto stagedScene = Scene::CreateDetachedScene("##PrefabInspector");
		const EntityHandle stagedRoot = SceneSerializer::DeserializeEntityFromValue(*stagedScene, root);
		if (stagedRoot == entt::null || !stagedScene->IsValid(stagedRoot)) {
			IDX_CORE_ERROR_TAG("PrefabInspector", "Failed to deserialize prefab root from {}", prefabPath);
			return false;
		}
		stagedScene->ClearDirty();
		m_PrefabScene = std::move(stagedScene);
		m_RootEntity = stagedRoot;
		m_PrefabPath = prefabPath;
		return true;
	}

	void PrefabInspector::Close() {
		// Destroy hooks gated by Scene::IsDetached() skip, so physics/audio/script subsystems are not affected.
		m_RootEntity = entt::null;
		m_PrefabScene.reset();
		m_PrefabPath.clear();
	}

	bool PrefabInspector::HasUnsavedChanges() const {
		return m_PrefabScene && m_PrefabScene->IsDirty();
	}

	bool PrefabInspector::Render() {
		if (!m_PrefabScene || m_RootEntity == entt::null || !m_PrefabScene->IsValid(m_RootEntity)) {
			EditorUI::EmptyState("No prefab loaded.");
			return false;
		}

		const auto& registry = SceneManager::GetComponentRegistry();
		Entity rootEntity = m_PrefabScene->GetEntity(m_RootEntity);
		const std::span<const Entity> entitySpan(&rootEntity, 1);

		const std::string filename = std::filesystem::path(m_PrefabPath).filename().string();
		// Frame-height baseline so the name centres against the badge.
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(filename.c_str());
		ImGui::SameLine();
		const bool dirty = m_PrefabScene->IsDirty();
		EditorUI::Badge(dirty ? "saving" : "auto-saved", dirty ? EditorUI::Tone::Info : EditorUI::Tone::Neutral);
		ImGui::Separator();

		// Component dispatch — same pattern as the entity inspector but without
		// clipboard / copy / paste / reset (those can come in v2).
		std::type_index pendingRemoval = typeid(void);

		registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
			if (info.category != ComponentCategory::Component) return;
			if (info.displayName == "Name") return; // Edited via the entity name field, if shown.
			if (!info.has || !info.has(rootEntity)) return;

			if (info.displayName == "Scripts") {
				DispatchComponentInspector(info, entitySpan);
				return;
			}

			// Mirrors the entity inspector guard (ImGuiEditorLayer): dynamic
			// `:IComponent` components render through the paired Scripts entry,
			// so the registry wrapper here would just be an empty section.
			if (info.isDynamic && !info.drawInspector && info.properties.empty()) {
				return;
			}

			bool removeRequested = false;
			bool open = ImGuiUtils::BeginComponentSection(info.displayName.c_str(), removeRequested,
				[]() {});
			if (removeRequested) {
				pendingRemoval = typeId;
			}

			if (open) {
				DispatchComponentInspector(info, entitySpan);
				ImGuiUtils::EndComponentSection();
			}
		});

		if (pendingRemoval != typeid(void)) {
			registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
				if (typeId == pendingRemoval && info.remove) {
					info.remove(rootEntity);
				}
			});
			m_PrefabScene->MarkDirty();
		}

		// Render the unified reference-picker popup once per inspector frame.
		ReferencePicker::RenderPopup();

		ImGui::Separator();
		const float buttonWidth = ImGui::GetContentRegionAvail().x;
		if (Icons::ButtonWithIcon(Icons::Type::Plus, "Add Component", ImVec2(buttonWidth, 0), true)) {
			ImGui::OpenPopup("AddPrefabComponentPopup");
			m_AddComponentSearchBuffer[0] = '\0';
		}

		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
				// Use 1-arg string constructor: the (data, DataSize) form embeds the trailing '\0' and breaks IsCSharpScriptExtension(".cs\0").
				std::string droppedPath(static_cast<const char*>(payload->Data));
				std::vector<EditorScriptDiscovery::ScriptEntry> droppedScripts;
				EditorScriptDiscovery::CollectScriptFile(std::filesystem::path(droppedPath), droppedScripts);
				for (const auto& scriptEntry : droppedScripts) {
					if (scriptEntry.IsSceneSystem || scriptEntry.IsGlobalSystem) {
						continue;
					}
					if (scriptEntry.ClassName.empty() || scriptEntry.Type == ScriptType::Unknown) {
						continue;
					}
					if (!rootEntity.HasComponent<ScriptComponent>()) {
						rootEntity.AddComponent<ScriptComponent>();
					}
					auto& scriptComponent = rootEntity.GetComponent<ScriptComponent>();
					if (scriptEntry.IsManagedComponent) {
						if (!scriptComponent.HasManagedComponent(scriptEntry.ClassName)) {
							scriptComponent.AddManagedComponent(scriptEntry.ClassName);
							m_PrefabScene->MarkDirty();
						}
					}
					else if (!scriptComponent.HasScript(scriptEntry.ClassName, scriptEntry.Type)) {
						scriptComponent.AddScript(scriptEntry.ClassName, scriptEntry.Type);
						m_PrefabScene->MarkDirty();
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		RenderAddComponentPopup("AddPrefabComponentPopup", *m_PrefabScene, entitySpan,
			m_AddComponentSearchBuffer, sizeof(m_AddComponentSearchBuffer));

		// Only dirty on actual value edits (ActiveIdHasBeenEditedThisFrame); plain focus/click does not set this flag, so tabbing through fields without changes is clean.
		const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		const ImGuiContext& g = *ImGui::GetCurrentContext();
		if (windowFocused && g.ActiveId != 0 && g.ActiveIdHasBeenEditedThisFrame) {
			m_PrefabScene->MarkDirty();
		}

		const bool itemActive = ImGui::IsAnyItemActive() && windowFocused;
		const bool dirtyForSave = m_PrefabScene->IsDirty();
		return dirtyForSave && !itemActive;
	}

	bool PrefabInspector::Save() {
		if (!m_PrefabScene || m_RootEntity == entt::null) return false;
		if (!SavePrefabSourceTransactionally(
			*m_PrefabScene, m_RootEntity, m_PrefabPath, "PrefabInspector")) return false;

		m_PrefabScene->ClearDirty();
		return true;
	}

}
