#include <pch.hpp>
#include "Gui/PrefabInspector.hpp"
#include "Scripting/ScriptAttachment.hpp"
#include "Gui/PrefabVariantState.hpp"

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

namespace Funke {
	using PrefabAuthoringDetail::VariantEditState;

	PrefabInspector::PrefabInspector() = default;
	PrefabInspector::~PrefabInspector() = default;

	bool PrefabInspector::Open(const std::string& prefabPath) {
		const FileStamp stamp = FileStamp::Read(prefabPath);
		if (prefabPath == m_FailedPath && stamp == m_FailedStamp) return false;

		std::unique_ptr<Scene> stagedScene;
		const EntityHandle stagedRoot = LoadPrefabSourceIntoDetachedScene(prefabPath, "##PrefabInspector",
			"PrefabInspector", stagedScene);
		if (stagedRoot == entt::null) {
			// Callers only switch away from a clean prefab, so dropping it beats showing it under the wrong selection.
			Close();
			m_FailedPath = prefabPath;
			m_FailedStamp = stamp;
			return false;
		}
		m_FailedPath.clear();
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
			if (!m_FailedPath.empty()) EditorUI::EmptyState("This prefab could not be loaded.", "See the Console for details.");
			else EditorUI::EmptyState("No prefab loaded.");
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
		const auto* variant = m_PrefabScene->GetRegistry().ctx().find<VariantEditState>();
		const Json::Value* baseEntity = variant ? variant->EditableBase.FindMember("Entity") : nullptr;
		const Json::Value currentSource = variant ? SceneSerializer::SerializePrefabSource(*m_PrefabScene, m_RootEntity) : Json::Value();
		const auto* currentEntity = currentSource.FindMember("Entity");
		if (variant) {
			const std::string baseName = std::filesystem::path(AssetRegistry::ResolvePath(variant->BaseGuid)).filename().string();
			ImGui::Text("Base: %s", baseName.c_str());
		}

		std::type_index pendingRemoval = typeid(void);

		registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
			if (info.category != ComponentCategory::Component) return;
			if (info.displayName == "Name") return; // Edited via the entity name field, if shown.
			if (!info.has || !info.has(rootEntity)) return;

			if (info.displayName == "Scripts") {
				if (baseEntity && currentEntity) {
					bool overridden = false;
					for (const char* key : {"Scripts", "ManagedComponents", "ScriptFields"}) {
						const auto* inherited = baseEntity->FindMember(key);
						const auto* current = currentEntity->FindMember(key);
						overridden |= (!inherited != !current) || (inherited && current && !(*inherited == *current));
					}
					registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& dynamic) {
						if (!dynamic.isDynamic || dynamic.serializedName.empty()) return;
						const auto* inherited = baseEntity->FindMember(dynamic.serializedName);
						const auto* current = currentEntity->FindMember(dynamic.serializedName);
						overridden |= (!inherited != !current) || (inherited && current && !(*inherited == *current));
					});
					EditorUI::Badge(overridden ? "Scripts: Local overrides" : "Scripts: Inherited",
						overridden ? EditorUI::Tone::Info : EditorUI::Tone::Neutral);
				}
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
				[]() {}, info.icon);
			if (removeRequested) {
				pendingRemoval = typeId;
			}

			if (open && baseEntity) {
				const std::string key = info.serializedName.empty() ? info.displayName : info.serializedName;
				const auto* inherited = baseEntity->FindMember(key);
				const auto* current = currentEntity ? currentEntity->FindMember(key) : nullptr;
				const bool overridden = !inherited || !current || !(*current == *inherited);
				EditorUI::Badge(overridden ? "Local override" : "Inherited", overridden ? EditorUI::Tone::Info : EditorUI::Tone::Neutral);
				if (overridden) {
					ImGui::SameLine(); ImGui::PushID(key.c_str());
					if (Icons::ButtonWithIcon(Icons::Type::Reset, "", ImVec2(0, 0), true)) {
						if (inherited) ResetPrefabVariantComponent(*m_PrefabScene, m_RootEntity, key);
						else pendingRemoval = typeId;
						m_PrefabScene->MarkDirty();
					}
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset this component to the base prefab");
					ImGui::PopID();
				}
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

		if (baseEntity) registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
			if (info.category != ComponentCategory::Component || info.displayName == "Scripts" || info.isDynamic || !info.has || info.has(rootEntity)) return;
			const std::string key = info.serializedName.empty() ? info.displayName : info.serializedName;
			const auto* inherited = baseEntity->FindMember(key);
			if (!inherited) return;
			ImGui::PushID(key.c_str());
			ImGui::Text("%s: removed locally", info.displayName.c_str()); ImGui::SameLine();
			if (Icons::ButtonWithIcon(Icons::Type::Reset, "", ImVec2(0, 0), true)
				&& ResetPrefabVariantComponent(*m_PrefabScene, m_RootEntity, key)) m_PrefabScene->MarkDirty();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Restore inherited component");
			ImGui::PopID();
		});

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
					if (scriptEntry.IsManagedComponent) {
						EditorScriptAttachment::AttachManagedComponentToEntity(rootEntity, *m_PrefabScene, scriptEntry);
					} else {
						EditorScriptAttachment::AttachScriptToEntity(rootEntity, *m_PrefabScene, scriptEntry);
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
