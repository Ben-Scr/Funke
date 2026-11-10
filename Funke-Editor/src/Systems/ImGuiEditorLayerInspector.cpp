#include <pch.hpp>
#include "Systems/ImGuiEditorLayer.hpp"
#include "Components/General/General.hpp"
#include "Components/General/Transform3DComponent.hpp"
#include "Components/General/ComponentOrderComponent.hpp"
#include "Components/Tags.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/ComponentOrder.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Scripting/ScriptAttachment.hpp"
#include "Scripting/ScriptComponentInspector.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/Json.hpp"
#include "Inspector/PropertyDrawer.hpp"
#include "Inspector/ReferencePicker.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Editor/EditorComponentRegistration.hpp"
#include "Core/Application.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Gui/AddComponentPopup.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/EditorMenu.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/Icons.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Undo/InspectorUndoRecorder.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>

namespace Funke {
	namespace {
		const ComponentInfo* FindComponentInfoBySerializedName(const ComponentRegistry& registry, const std::string& serializedName)
		{
			const ComponentInfo* found = nullptr;
			registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
				if (!found && info.category == ComponentCategory::Component && info.serializedName == serializedName) {
					found = &info;
				}
			});
			return found;
		}

		// Transforms first, scripts last, the rest in registration order: the registry's map order is a hash order
		// that put e.g. Box Collider 3D above Transform 3D.
		template <typename F>
		void ForEachComponentInInspectorOrder(const ComponentRegistry& registry, F&& fn) {
			std::vector<const ComponentInfo*> infos;
			registry.ForEachComponentInfoInRegistrationOrder([&](const ComponentInfo& info) { infos.push_back(&info); });
			const auto rank = [](const ComponentInfo& info) {
				if (info.typeId == typeid(Transform2DComponent) || info.typeId == typeid(RectTransform2DComponent)
					|| info.typeId == typeid(Transform3DComponent)) return 0;
				return info.isDynamic || info.displayName == "Scripts" ? 2 : 1;
			};
			std::stable_sort(infos.begin(), infos.end(),
				[&](const ComponentInfo* a, const ComponentInfo* b) { return rank(*a) < rank(*b); });
			for (const ComponentInfo* info : infos) {
				fn(info->isDynamic ? std::type_index(typeid(void)) : info->typeId, *info);
			}
		}

		// One Inspector section: a built-in component, or one C# component / script of the ScriptComponent.
		struct InspectorSection {
			std::string Key;      // ComponentOrderComponent key
			std::string RefName;  // name in COMPONENT_REF payloads: display name, or the class
			const ComponentInfo* Info = nullptr;
			std::type_index TypeId = typeid(void);
			int ScriptKind = 0;   // 1 = ScriptComponent::ManagedComponents, 2 = ScriptComponent::Scripts
			std::size_t ScriptIndex = 0;
			bool WholeScriptBlock = false;  // multi-selection with different scripts: drawn as one block, not reorderable
		};

		struct InspectorSectionList {
			std::vector<InspectorSection> Items;
			std::size_t Pinned = 0;  // leading transforms; they stay on top
		};

		bool IsInspectorTransform(const ComponentInfo& info) {
			return info.typeId == typeid(Transform2DComponent) || info.typeId == typeid(RectTransform2DComponent)
				|| info.typeId == typeid(Transform3DComponent);
		}

		// has picks the components to list (the selection's common ones, or one entity's own); scripts non-null
		// lists each C# component and script as its own section. saved is the hand-made order to apply.
		template <typename HasFn>
		InspectorSectionList CollectInspectorSections(const ComponentRegistry& registry, const ScriptComponent* scripts,
			const std::vector<std::string>* saved, HasFn&& has) {
			InspectorSectionList list;
			ForEachComponentInInspectorOrder(registry, [&](const std::type_index& typeId, const ComponentInfo& info) {
				if (info.category != ComponentCategory::Component) return;
				if (info.displayName == "Name") return; // Shown in entity header
				if (!has(typeId, info)) return;
				if (info.displayName == "Scripts") {
					if (!scripts) {
						list.Items.push_back({ info.displayName, info.displayName, &info, typeId, 0, 0, true });
						return;
					}
					for (std::size_t i = 0; i < scripts->ManagedComponents.size(); ++i) {
						const std::string& className = scripts->ManagedComponents[i];
						list.Items.push_back({ ComponentOrder::ManagedComponentKey(className), className, &info, typeId, 1, i });
					}
					for (std::size_t i = 0; i < scripts->Scripts.size(); ++i) {
						const std::string& className = scripts->Scripts[i].GetClassName();
						list.Items.push_back({ ComponentOrder::ScriptKey(className), className, &info, typeId, 2, i });
					}
					return;
				}
				// Dynamic components with no drawInspector/properties render as empty sections; their fields appear in the paired ScriptComponent.Scripts entry.
				if (info.isDynamic && !info.drawInspector && info.properties.empty()) return;
				if (IsInspectorTransform(info)) ++list.Pinned;
				list.Items.push_back({ info.serializedName.empty() ? "~" + info.displayName : info.serializedName,
					info.displayName, &info, typeId });
			});
			if (saved && !saved->empty()) {
				std::vector<std::string> keys;
				keys.reserve(list.Items.size());
				for (const InspectorSection& item : list.Items) keys.push_back(item.Key);
				std::vector<InspectorSection> arranged;
				arranged.reserve(list.Items.size());
				for (const std::string& key : ComponentOrder::Arrange(keys, *saved, list.Pinned)) {
					const auto it = std::find_if(list.Items.begin(), list.Items.end(),
						[&](const InspectorSection& item) { return item.Key == key; });
					if (it != list.Items.end()) arranged.push_back(std::move(*it));
				}
				list.Items = std::move(arranged);
			}
			return list;
		}

		// Local "HH:MM:SS" stamp for a console entry, captured at log-receipt time.
		struct ComponentClipboardPayload {
			std::string SerializedName;
			Json::Value Data;
		};

		bool TryReadComponentClipboard(const std::string& clipboardJson, ComponentClipboardPayload& outPayload)
		{
			if (clipboardJson.empty()) {
				return false;
			}

			Json::Value root;
			std::string parseError;
			if (!Json::TryParse(clipboardJson, root, &parseError) || !root.IsObject()) {
				return false;
			}

			const Json::Value* componentValue = root.FindMember("component");
			const Json::Value* dataValue = root.FindMember("data");
			if (!componentValue || !componentValue->IsString() || !dataValue || dataValue->IsNull()) {
				return false;
			}

			outPayload.SerializedName = componentValue->AsStringOr();
			outPayload.Data = *dataValue;
			return !outPayload.SerializedName.empty();
		}

	}

	void ImGuiEditorLayer::RenderSceneSystemsInspector(Scene& scene) {
		const auto systemsBefore = SceneSystemsEditCommand::Capture(scene);
		bool structureChanged = false;
		ImGui::Text("Scene: %s", scene.GetName().c_str());
		ImGui::SeparatorText("Systems");

		auto acceptDroppedSceneSystems = [&]() {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
				// 1-arg form: 2-arg would embed trailing '\0' and break extension matching in CollectScriptFile.
				std::string droppedPath(static_cast<const char*>(payload->Data));
				std::vector<EditorScriptDiscovery::ScriptEntry> droppedScripts;
				EditorScriptDiscovery::CollectScriptFile(std::filesystem::path(droppedPath), droppedScripts);
				for (const auto& scriptEntry : droppedScripts) {
					if (scriptEntry.IsSceneSystem) {
						structureChanged |= scene.AddSceneSystem(scriptEntry.ClassName);
					}
				}
			}
		};

		const auto& systems = scene.GetSceneSystemClassNames();
		for (size_t i = 0; i < systems.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			const std::string className = systems[i];

			// AllowOverlap lets trailing buttons hit-test over the header widget.
			bool open = ImGui::CollapsingHeader((className + "##system_header").c_str(),
				ImGuiTreeNodeFlags_AllowOverlap);

			const ImGuiStyle& style = ImGui::GetStyle();
			const float btnW = ImGui::GetFrameHeight();
			const float spacing = style.ItemInnerSpacing.x;
			const float stripW = btnW * 4.0f + spacing * 3.0f;
			ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - stripW);

			bool enabled = scene.IsSceneSystemEnabled(className);
			if (ImGuiUtils::Switch("##enabled", &enabled)) {
				structureChanged |= scene.SetSceneSystemEnabled(className, enabled);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", enabled ? "Disable SceneSystem" : "Enable SceneSystem");
			}

			ImGui::SameLine(0, spacing);
			if (EditorUI::IconButton("##move_up", "arrow_up", "Move up", false, false, btnW) && i > 0) {
				structureChanged |= scene.MoveSceneSystem(i, i - 1);
			}

			ImGui::SameLine(0, spacing);
			if (EditorUI::IconButton("##move_down", "arrow_down", "Move down", false, false, btnW) && i + 1 < systems.size()) {
				structureChanged |= scene.MoveSceneSystem(i, i + 1);
			}

			ImGui::SameLine(0, spacing);
			const bool remove = EditorUI::IconButton("##remove", "delete", "Remove SceneSystem", false, false, btnW);
			if (remove) {
				structureChanged |= scene.RemoveSceneSystem(i);
				ImGui::PopID();
				break;
			}

			if (open) {
				ImGui::Indent(8.0f);
				DrawSceneSystemFields(scene, className);
				ImGui::Unindent(8.0f);
			}

			ImGui::PopID();
		}

		// Pre-collect available systems so the button can be disabled when nothing
		// can be added — avoids the empty-popup sliver that looks broken.
		std::vector<EditorScriptDiscovery::ScriptEntry> scriptEntries;
		EditorScriptDiscovery::CollectProjectScriptEntries(scriptEntries);
		size_t availableSystemCount = 0;
		for (const auto& entry : scriptEntries) {
			if (entry.IsSceneSystem && !scene.HasSceneSystem(entry.ClassName)) {
				++availableSystemCount;
			}
		}
		const bool hasAvailableSystem = availableSystemCount > 0;

		float buttonWidth = ImGui::GetContentRegionAvail().x;
		if (!hasAvailableSystem) ImGui::BeginDisabled();
		if (Icons::ButtonWithIcon(Icons::Type::Plus, "Add System", ImVec2(buttonWidth, 0), true)) {
			ImGui::OpenPopup("AddSystemPopup");
			m_SystemSearchBuffer[0] = '\0';
		}
		// Scoped drag-drop target: only the "+ Add System" button accepts
		// dropped scripts. Previously the whole inspector InnerRect was a
		// drop zone which was confusing.
		if (ImGui::BeginDragDropTarget()) {
			acceptDroppedSceneSystems();
			ImGui::EndDragDropTarget();
		}
		if (!hasAvailableSystem) {
			ImGui::EndDisabled();
			// AllowWhenDisabled so hover still fires after BeginDisabled.
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
				ImGui::SetTooltip("No game systems available.\nCreate one via Asset Browser > Create > Scripting > SceneSystem (C#).");
			}
		}

		if (ImGui::BeginPopup("AddSystemPopup")) {
			EditorUI::SearchField("##SystemSearch", m_SystemSearchBuffer, sizeof(m_SystemSearchBuffer),
				"Search systems...", ImGui::GetFontSize() * 18.0f);
			ImGui::Separator();

			std::string filter(m_SystemSearchBuffer);
			std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

			for (const auto& scriptEntry : scriptEntries) {
				if (!scriptEntry.IsSceneSystem || scene.HasSceneSystem(scriptEntry.ClassName)) {
					continue;
				}

				if (!filter.empty()) {
					std::string lowerClassName = EditorScriptDiscovery::ToLowerCopy(scriptEntry.ClassName);
					std::string lowerPath = EditorScriptDiscovery::ToLowerCopy(scriptEntry.Path.string());
					if (lowerClassName.find(filter) == std::string::npos
						&& lowerPath.find(filter) == std::string::npos) {
						continue;
					}
				}

				const std::string label = EditorScriptDiscovery::MenuLabel(scriptEntry);
				const std::string id = EditorScriptDiscovery::EntryId(scriptEntry);
				if (ImGuiUtils::MenuItemEllipsis(label, id.c_str(), nullptr, false, true, 260.0f)) {
					structureChanged |= scene.AddSceneSystem(scriptEntry.ClassName);
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}

		if (structureChanged) {
			PushStructureUndo(scene, std::make_unique<SceneSystemsEditCommand>(systemsBefore, SceneSystemsEditCommand::Capture(scene)));
		}
	}

	std::vector<std::string> ImGuiEditorLayer::CurrentEntityNames(Scene& scene, std::span<const EntityHandle> handles) {
		std::vector<std::string> names;
		names.reserve(handles.size());
		for (EntityHandle h : handles) {
			const NameComponent* name = scene.IsValid(h) ? scene.GetRegistry().try_get<NameComponent>(h) : nullptr;
			names.push_back(name ? name->Name : std::string{});
		}
		return names;
	}

	void ImGuiEditorLayer::FinishInspectorRename(Scene& scene) {
		if (!m_InspectorRename) return;
		if (m_InspectorRename->Recorder && CurrentEntityNames(scene, m_InspectorRename->Handles) != m_InspectorRename->NamesBefore) {
			PushStructureUndo(scene, m_InspectorRename->Recorder->Finish(scene, m_InspectorRename->Handles));
		}
		m_InspectorRename.reset();
	}

	void ImGuiEditorLayer::RenderInspectorPanel(Scene& scene) {
		auto& hierarchyState = SceneHierarchyState::Get(scene);
		ImGui::Begin("Inspector");

		// Cache avoids O(N×M) loops per frame over (selection × components) at large selections.
		// Gameplay can destroy a selected entity between OnUpdate's selection prune and this render.
		bool cacheHasStaleHandle = false;
		for (EntityHandle h : m_InspectorCache.Handles) {
			if (!scene.IsValid(h)) { cacheHasStaleHandle = true; break; }
		}
		if (cacheHasStaleHandle) {
			++m_SelectionVersion;
		}
		if (m_InspectorCache.Version != m_SelectionVersion) {
			RecomputeInspectorSelectionCache(scene);
			m_InspectorCache.Version = m_SelectionVersion;
		}
		const std::vector<EntityHandle>& selectedHandles = m_InspectorCache.Handles;
		// The name field is no longer drawn for that selection, so its deactivation never arrives.
		if (m_InspectorRename && m_InspectorRename->Handles != selectedHandles) {
			FinishInspectorRename(scene);
		}
		if (selectedHandles.empty()) {
			m_SelectedEntity = entt::null;
			if (m_IsSceneNodeSelected) {
				RenderSceneSystemsInspector(scene);
				// Render picker popup here too: this early return bypasses the later RenderPopup call.
				ReferencePicker::RenderPopup();
			}
			else {
				RenderAssetInspector();
				// DataAsset reference fields open the picker; same early-return bypass as above.
				ReferencePicker::RenderPopup();
			}
			m_IsInspectorPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			ImGui::End();
			return;
		}

		std::vector<Entity> selectedEntities;
		selectedEntities.reserve(selectedHandles.size());
		for (EntityHandle h : selectedHandles) {
			selectedEntities.push_back(scene.GetEntity(h));
		}
		std::span<const Entity> entitySpan(selectedEntities);
		Entity entity = selectedEntities[0]; // primary, used for the drag-source label and prefab buttons
		if (!scene.IsValid(m_SelectedEntity)) {
			m_SelectedEntity = entity.GetHandle();
		}

		// ── Entity Header: type icon, name + summary, enabled switch, options menu ──
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float frameH = ImGui::GetFrameHeight();
			const float iconBox = std::round(frameH * 1.9f);
			const ImVec2 boxMin = ImGui::GetCursorScreenPos();
			ImDrawList* headerDraw = ImGui::GetWindowDrawList();
			headerDraw->AddRectFilled(boxMin, ImVec2(boxMin.x + iconBox, boxMin.y + iconBox),
				ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding * 1.5f);
			const float glyph = std::round(iconBox * 0.55f);
			const char* headerIconName = EntityTypeIconName(scene, entity.GetHandle());
			if (const uint64_t icon = EditorIcons::Get(headerIconName, static_cast<int>(glyph))) {
				const ImVec2 g0(boxMin.x + (iconBox - glyph) * 0.5f, boxMin.y + (iconBox - glyph) * 0.5f);
				// Component icons are coloured; a tint would multiply them off-hue. Only the plain entity glyph follows the text colour.
				const ImU32 tint = std::string_view(headerIconName) == "entity"
					? ImGui::GetColorU32(ImGuiCol_Text) : IM_COL32_WHITE;
				headerDraw->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(icon)), g0, ImVec2(g0.x + glyph, g0.y + glyph),
					ImVec2(0, 1), ImVec2(1, 0), tint);
			}
			ImGui::Dummy(ImVec2(iconBox, iconBox));
			ImGui::SameLine();
			ImGui::BeginGroup();

			// Empty buffer + hint avoids the bug where Enter on a nameless entity (buffer pre-filled with "Entity") created a real NameComponent.
			const bool nameUniform = m_InspectorCache.NameUniform;
			char nameBuf[256]{};
			if (nameUniform && !m_InspectorCache.FirstName.empty()) {
				std::snprintf(nameBuf, sizeof(nameBuf), "%s", m_InspectorCache.FirstName.c_str());
			}
			const char* nameHint = nameUniform ? "Entity" : "-";
			const float switchW = std::round(std::round(frameH * 0.62f) * 1.8f);
			ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - switchW - frameH - style.ItemSpacing.x * 2.0f));
			const bool nameEdited = ImGui::InputTextWithHint("##EntityName", nameHint, nameBuf, sizeof(nameBuf));
			const bool nameActivated = ImGui::IsItemActivated();
			const bool nameDeactivated = ImGui::IsItemDeactivated();

			// Shows authored state, not effective in-hierarchy: InheritedDisabledTag doesn't flip the switch.
			ImGui::SameLine();
			bool isEnabled = m_InspectorCache.EnabledFirst;
			const bool enabledChanged = ImGuiUtils::ToggleSwitch("##EntityEnabled", &isEnabled, !m_InspectorCache.EnabledUniform);
			EditorUI::Tooltip("Enabled");

			ImGui::SameLine();
			if (EditorUI::IconButton("##EntityOptions", "settings", "Entity options")) {
				ImGui::OpenPopup("EntityHeaderMenu");
			}

			const bool isStaticNow = m_InspectorCache.StaticFirst;
			std::string summary = selectedEntities.size() > 1
				? std::to_string(selectedEntities.size()) + " entities selected"
				: std::string("Entity");
			if (m_InspectorCache.StaticUniform && isStaticNow) summary += "  -  Static";
			if (m_InspectorCache.EnabledUniform && !m_InspectorCache.EnabledFirst) summary += "  -  Disabled";
			ImGui::TextDisabled("%s", summary.c_str());
			ImGui::EndGroup();

			bool staticChanged = false;
			bool isStatic = isStaticNow;
			if (ImGui::BeginPopup("EntityHeaderMenu")) {
				if (ImGui::MenuItem("Static", nullptr, m_InspectorCache.StaticUniform && isStaticNow)) {
					isStatic = !(m_InspectorCache.StaticUniform && isStaticNow);
					staticChanged = true;
				}
				EditorUI::Tooltip("Static entities are excluded from per-frame transform updates.");
				ImGui::Separator();
				ImGui::TextDisabled("Component View");
				const InspectorComponentView currentView = EditorPreferences::GetInspectorComponentView();
				if (ImGui::MenuItem("Normal", nullptr, currentView == InspectorComponentView::Normal)) {
					EditorPreferences::SetInspectorComponentView(InspectorComponentView::Normal);
				}
				EditorUI::Tooltip("Show editable component fields only.");
				if (ImGui::MenuItem("Detailed", nullptr, currentView == InspectorComponentView::Detailed)) {
					EditorPreferences::SetInspectorComponentView(InspectorComponentView::Detailed);
				}
				EditorUI::Tooltip("Also show read-only / computed script values\n(get-only properties, const and readonly fields).");
				ImGui::EndPopup();
			}

			std::vector<EntityHandle> headerHandles;
			for (Entity& e : selectedEntities) headerHandles.push_back(e.GetHandle());
			if (nameActivated || (nameEdited && ImGui::IsItemActive() && !m_InspectorRename)) {
				FinishInspectorRename(scene);
				m_InspectorRename.emplace();
				m_InspectorRename->Handles = headerHandles;
				m_InspectorRename->NamesBefore = CurrentEntityNames(scene, headerHandles);
				m_InspectorRename->Recorder.emplace(scene, headerHandles, "Rename");
			}
			// On the deactivation frame ImGui writes the typed text back into the buffer, which by then may be
			// seeded from another selection (the click that ended the edit selected a different entity).
			const bool renameTargetsSelection = m_InspectorRename && m_InspectorRename->Handles == headerHandles;
			// A mixed-name selection shows an empty field; leaving it empty keeps every name.
			const bool nameChanged = nameEdited && renameTargetsSelection
				&& !(nameBuf[0] == '\0' && !m_InspectorCache.NameUniform);
			if (nameChanged) {
				std::string newName(nameBuf);
				for (Entity& e : selectedEntities) {
					if (newName.empty()) {
						if (e.HasComponent<NameComponent>()) {
							e.RemoveComponent<NameComponent>();
						}
					}
					else if (e.HasComponent<NameComponent>()) {
						e.GetComponent<NameComponent>().Name = newName;
					}
					else {
						scene.AddComponent<NameComponent>(e.GetHandle(), newName);
					}
				}
				scene.MarkDirty();
				// NameComponent can be added/removed and FirstName changes; the
				// next inspector frame must rebuild its cached header strings.
				++m_SelectionVersion;
			}
			if (nameDeactivated) {
				FinishInspectorRename(scene);
			}

			std::optional<StructureEditRecorder> headerRecorder;
			if (enabledChanged || staticChanged) {
				headerRecorder.emplace(scene, headerHandles,
					enabledChanged ? (isEnabled ? "Enable" : "Disable") : "Toggle Static");
			}
			if (enabledChanged) {
				for (Entity& e : selectedEntities) {
					e.SetEnabled(isEnabled);
				}
				scene.MarkDirty();
				// DisabledTag/InheritedDisabledTag presence changes — cached EnabledFirst/EnabledUniform are stale.
				++m_SelectionVersion;
			}
			if (staticChanged) {
				for (Entity& e : selectedEntities) {
					if (isStatic && !e.HasComponent<StaticTag>()) e.AddComponent<StaticTag>();
					else if (!isStatic && e.HasComponent<StaticTag>()) e.RemoveComponent<StaticTag>();
				}
				scene.MarkDirty();
				// StaticTag presence changes — cached StaticFirst/StaticUniform are stale.
				++m_SelectionVersion;
			}
			if (headerRecorder) PushStructureUndo(scene, headerRecorder->Finish(scene, headerHandles));
		}

		/* INFO(Ben-Scr): MetaData Doesn't need to be displayed
		if (const EntityMetaData* metaData = entity.GetMetaData()) {
			const char* originText = "Runtime";
			if (metaData->Origin == EntityOrigin::Scene) originText = "Scene";
			if (metaData->Origin == EntityOrigin::Prefab) originText = "Prefab";
			ImGui::TextDisabled("Origin: %s", originText);

			const uint64_t runtimeId = metaData->RuntimeID;
			ImGui::TextDisabled("RuntimeID: %llu", runtimeId);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to copy");
			if (ImGui::IsItemClicked()) ImGui::SetClipboardText(std::to_string(runtimeId).c_str());

			if (metaData->Origin == EntityOrigin::Scene && static_cast<uint64_t>(metaData->SceneGUID) != 0) {
				const uint64_t sceneGuid = static_cast<uint64_t>(metaData->SceneGUID);
				ImGui::TextDisabled("SceneGUID: %llu", sceneGuid);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to copy");
				if (ImGui::IsItemClicked()) ImGui::SetClipboardText(std::to_string(sceneGuid).c_str());
			}
			if (metaData->Origin == EntityOrigin::Prefab && static_cast<uint64_t>(metaData->PrefabGUID) != 0) {
				const uint64_t prefabGuid = static_cast<uint64_t>(metaData->PrefabGUID);
				ImGui::TextDisabled("PrefabGUID: %llu", prefabGuid);
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to copy");
				if (ImGui::IsItemClicked()) ImGui::SetClipboardText(std::to_string(prefabGuid).c_str());

				if (ImGui::Button("Apply to Prefab")) {
					SceneSerializer::ApplyPrefabInstanceOverrides(scene, m_SelectedEntity);
				}
				ImGui::SameLine();
				if (ImGui::Button("Revert All")) {
					EntityHandle replacement = SceneSerializer::RevertPrefabInstanceOverride(scene, m_SelectedEntity, {});
					if (replacement != entt::null) {
						SelectEntity(replacement);
						hierarchyState.EntityOrder.clear(); hierarchyState.Dirty = true;
					}
				}
			}
		}
		*/

		m_IsInspectorPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		ImGui::Separator();

		const auto& registry = SceneManager::GetComponentRegistry();

		std::type_index pendingRemoval = typeid(void);

		Json::Value prefabOverrides = Json::Value::MakeObject();
		const bool isSinglePrefabInstance = selectedEntities.size() == 1
			&& scene.GetEntityOrigin(entity.GetHandle()) == EntityOrigin::Prefab
			&& static_cast<uint64_t>(scene.GetPrefabGUID(entity.GetHandle())) != 0;
		bool prefabSourceResolvable = false;
		// Always resolved to the actual prefab root: applying a child as if it were root would overwrite the prefab file with that child's subtree.
		EntityHandle prefabInstanceRoot = entt::null;
		// True iff ANY entity in the subtree has overrides; per-entity prefabOverrides only captures this entity's diff.
		bool hasSubtreeOverrides = false;
		if (isSinglePrefabInstance) {
			prefabSourceResolvable = SceneSerializer::ComputeInstanceOverrides(
				scene, entity.GetHandle(), prefabOverrides);
			if (prefabSourceResolvable) {
				prefabInstanceRoot = SceneSerializer::GetPrefabInstanceRoot(scene, entity.GetHandle());
				hasSubtreeOverrides = !prefabOverrides.GetObject().empty()
					|| SceneSerializer::HasPrefabInstanceOverrides(scene, entity.GetHandle());
			}
		}

		if (isSinglePrefabInstance && prefabSourceResolvable && prefabInstanceRoot != entt::null) {
			// Disable Apply/Revert when there are no overrides: Apply would still trigger disk write + live-instance refresh for no semantic change.
			ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors::PrefabInstance());
			ImGui::TextUnformatted(entity.GetHandle() == prefabInstanceRoot
				? "Prefab Instance"
				: "Prefab Instance (child)");
			ImGui::PopStyleColor();
			{
				const char* selectLabel = "Select Prefab";
				const float selectW = ImGui::CalcTextSize(selectLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
				const float afterLabelX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetStyle().ItemSpacing.x;
				ImGui::SameLine(std::max(afterLabelX, ImGui::GetWindowContentRegionMax().x - selectW));
				if (ImGui::SmallButton(selectLabel)) {
					AssetBrowser::RequestRevealAsset(
						AssetRegistry::ResolvePath(static_cast<uint64_t>(scene.GetPrefabGUID(prefabInstanceRoot))));
				}
				EditorUI::Tooltip("Show the source prefab in the Asset Browser");
			}
			ImGui::BeginDisabled(!hasSubtreeOverrides || Application::GetIsPlaying()); // play mode: Apply would bake runtime state, Revert would destroy the running instance
			if (ImGui::SmallButton("Apply All")) {
				ApplyPrefabInstanceToSource(scene, prefabInstanceRoot);
				ImGui::EndDisabled();
				ImGui::Separator();
				ImGui::End();
				return;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Revert All")) {
				// RevertPrefabInstanceOverride destroys the root and returns a replacement; bail out of the inspector body this frame to avoid iterating destroyed handles.
				const EntityHandle revertedRoot = prefabInstanceRoot;
				StructureEditRecorder undo(scene, std::span(&revertedRoot, 1), "Revert Prefab");
				EntityHandle replacement = SceneSerializer::RevertPrefabInstanceOverride(scene, revertedRoot, {});
				if (replacement != entt::null) {
					PushStructureUndo(scene, undo.Finish(scene, std::span(&replacement, 1)));
					if (m_PrefabEditRootEntity == revertedRoot) {
						m_PrefabEditRootEntity = replacement;
					}
					SelectEntity(replacement); // also bumps m_SelectionVersion
					hierarchyState.EntityOrder.clear(); hierarchyState.Dirty = true;
					ImGui::EndDisabled();
					ImGui::Separator();
					ImGui::End();
					return;
				}
			}
			ImGui::EndDisabled();
			ImGui::Separator();
		}
		else if (isSinglePrefabInstance) {
			ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors::PrefabOrphan());
			ImGui::TextUnformatted("Prefab Instance (orphaned — source missing)");
			ImGui::PopStyleColor();
			ImGui::Separator();
		}

		// The entity header and prefab bar stay pinned; only the components scroll.
		ImGui::BeginChild("##InspectorBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_None);

		// Bucket override paths by component name once per frame for O(1) per-component lookup (was O(components × paths) per render).
		std::unordered_map<std::string, std::vector<std::string>> overridesByComponent;
		if (prefabSourceResolvable) {
			for (const auto& [path, _] : prefabOverrides.GetObject()) {
				const std::size_t dot = path.find('.');
				const std::string componentKey = (dot == std::string::npos)
					? path
					: path.substr(0, dot);
				overridesByComponent[componentKey].push_back(path);
			}
		}

		// Lambda: do any override paths fall under this component's serializedName?
		auto componentHasOverrides = [&](const std::string& serializedName) -> bool {
			if (Application::GetIsPlaying() && !scene.IsDetached()) return false;
			if (!prefabSourceResolvable || serializedName.empty()) return false;
			auto it = overridesByComponent.find(serializedName);
			return it != overridesByComponent.end() && !it->second.empty();
		};

		const std::vector<std::string>& hiddenPartialComponents = m_InspectorCache.PartialComponents;

		// Latch: a Revert destroys+recreates `entity`; remaining ForEachComponentInfo iterations this frame must be no-ops to avoid use-after-destroy.
		bool selectionInvalidatedByRevert = false;
		const auto drawComponentSection = [&](const std::type_index& typeId, const ComponentInfo& info) {
			if (selectionInvalidatedByRevert) return;
			if (info.category != ComponentCategory::Component) return;
			if (info.displayName == "Name") return; // Shown in entity header

			if (m_InspectorCache.CommonComponentTypes.find(typeId)
				== m_InspectorCache.CommonComponentTypes.end()) { // O(1) cache gate vs per-frame O(N) info.has() sweep
				return;
			}

			// Scripts render their own per-script sections — skip the outer wrapper
			if (info.displayName == "Scripts") {
				DispatchComponentInspector(info, entitySpan);
				return;
			}

			// Dynamic components with no drawInspector/properties render as empty sections; their fields appear in the paired ScriptComponent.Scripts entry.
			if (info.isDynamic && !info.drawInspector && info.properties.empty()) {
				return;
			}

			bool removeRequested = false;
			bool copyRequested = false;
			bool pasteRequested = false;
			bool resetRequested = false;
			ComponentClipboardPayload clipboardPayload;
			const bool hasComponentClipboard = TryReadComponentClipboard(m_ComponentClipboardJson, clipboardPayload);
			const ComponentInfo* pastedComponentInfo = hasComponentClipboard
				? FindComponentInfoBySerializedName(registry, clipboardPayload.SerializedName)
				: nullptr;
			const bool canPasteComponent = hasComponentClipboard
				&& pastedComponentInfo
				&& pastedComponentInfo->add
				&& pastedComponentInfo->has
				&& clipboardPayload.Data.IsObject();
			const bool canSerializeComponent = !info.serializedName.empty();

			const std::string componentSerializedName = info.serializedName;
			const bool thisComponentOverridden = componentHasOverrides(componentSerializedName);

			bool revertComponentRequested = false;
			bool applyComponentRequested = false;
			std::string revertFieldRequested; // empty = none

			bool open = ImGuiUtils::BeginComponentSection(info.displayName.c_str(), removeRequested, [&]() {
				if (ImGui::MenuItem("Copy Component", nullptr, false, canSerializeComponent)) {
					copyRequested = true;
				}
				if (ImGui::MenuItem("Paste Component", nullptr, false, canPasteComponent)) {
					pasteRequested = true;
				}
				if (ImGui::MenuItem("Reset Component", nullptr, false, canSerializeComponent)) {
					resetRequested = true;
				}
				// Prefab override actions — only meaningful when this entity is a
				// prefab instance whose source resolves AND this component has
				// at least one diff against the source.
				if (isSinglePrefabInstance && prefabSourceResolvable && thisComponentOverridden) {
					ImGui::Separator();
					if (ImGui::BeginMenu("Revert Field", !Application::GetIsPlaying())) {
						const std::string prefix = componentSerializedName + ".";
						for (const auto& [path, _] : prefabOverrides.GetObject()) {
							if (path != componentSerializedName && path.compare(0, prefix.size(), prefix) != 0) continue;
							const char* leaf = path.c_str() + (path.size() > prefix.size() ? prefix.size() : 0);
							if (ImGui::MenuItem(leaf)) {
								revertFieldRequested = path;
							}
						}
						ImGui::EndMenu();
					}
					if (ImGui::MenuItem("Revert Component", nullptr, false, !Application::GetIsPlaying())) {
						revertComponentRequested = true;
					}
					if (ImGui::MenuItem("Apply Component", nullptr, false, !Application::GetIsPlaying())) {
						applyComponentRequested = true;
					}
				}
			}, info.icon, thisComponentOverridden);

			if (removeRequested) {
				pendingRemoval = typeId;
			}
			if (copyRequested && canSerializeComponent) {
				Json::Value componentValue = SceneSerializer::SerializeComponent(scene, entity.GetHandle(), info.serializedName);
				if (!componentValue.IsNull()) {
					Json::Value root = Json::Value::MakeObject();
					root.AddMember("type", Json::Value("ComponentClipboard"));
					root.AddMember("component", Json::Value(info.serializedName));
					root.AddMember("displayName", Json::Value(info.displayName));
					root.AddMember("data", std::move(componentValue));
					m_ComponentClipboardJson = Json::Stringify(root, false);
				}
			}
			if (pasteRequested && canPasteComponent) {
				const std::vector<EntityHandle> roots = selectedHandles;
				StructureEditRecorder undo(scene, roots, "Paste Component");
				bool anyApplied = false;
				bool anyAdded = false;
				for (const Entity& e : selectedEntities) {
					if (!pastedComponentInfo->has(e)) {
						// Refused (conflicts with a component on the entity): don't let the deserialize below add it anyway.
						if (!registry.AddWithDependencies(e, pastedComponentInfo->typeId)) continue;
						anyAdded = true;
					}
					if (SceneSerializer::DeserializeComponent(scene, e.GetHandle(),
						clipboardPayload.SerializedName, clipboardPayload.Data)) {
						anyApplied = true;
					}
				}
				if (anyApplied) scene.MarkDirty();
				if (anyApplied || anyAdded) PushStructureUndo(scene, undo.Finish(scene, roots));
				// AddWithDependencies may have inserted a new component type
				// on one or more selected entities — the cached intersection
				// has to refresh so the new section appears.
				if (anyAdded) ++m_SelectionVersion;
			}
			if (resetRequested && canSerializeComponent) {
				const std::vector<EntityHandle> roots = selectedHandles;
				StructureEditRecorder undo(scene, roots, "Reset Component");
				bool anyReset = false;
				for (const Entity& e : selectedEntities) {
					if (SceneSerializer::ResetComponent(scene, e.GetHandle(), info.serializedName)) {
						anyReset = true;
					}
				}
				if (anyReset) scene.MarkDirty();
				if (anyReset) PushStructureUndo(scene, undo.Finish(scene, roots));
			}

			if (!revertFieldRequested.empty()) {
				StructureEditRecorder undo(scene, std::span(&prefabInstanceRoot, 1), "Revert Prefab Field");
				EntityHandle replacement = SceneSerializer::RevertPrefabInstanceOverride(
					scene, entity.GetHandle(), revertFieldRequested);
				if (replacement != entt::null) {
					const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(scene, replacement);
					PushStructureUndo(scene, undo.Finish(scene, std::span(&root, 1)));
					SelectEntity(replacement);
					hierarchyState.EntityOrder.clear(); hierarchyState.Dirty = true;
					// Entity was recreated — `entity`/`entitySpan` are now stale.
					// Stop iterating components this frame (see latch above).
					selectionInvalidatedByRevert = true;
					if (open) ImGuiUtils::EndComponentSection();
					return;
				}
			}
			if (revertComponentRequested) {
				StructureEditRecorder undo(scene, std::span(&prefabInstanceRoot, 1), "Revert Prefab Component");
				const std::string prefix = componentSerializedName + ".";
				std::vector<std::string> paths;
				for (const auto& [path, _] : prefabOverrides.GetObject()) {
					if (path == componentSerializedName || path.compare(0, prefix.size(), prefix) == 0) {
						paths.push_back(path);
					}
				}
				EntityHandle current = entity.GetHandle();
				for (const std::string& p : paths) {
					EntityHandle replacement = SceneSerializer::RevertPrefabInstanceOverride(scene, current, p);
					if (replacement != entt::null) current = replacement;
				}
				SelectEntity(current);
				const EntityHandle root = SceneSerializer::GetPrefabInstanceRoot(scene, current);
				PushStructureUndo(scene, undo.Finish(scene, std::span(&root, 1)));
				hierarchyState.EntityOrder.clear(); hierarchyState.Dirty = true;
				// Entity may have been recreated by the reverts above — `entity`/
				// `entitySpan` are now stale. Stop iterating components this frame.
				selectionInvalidatedByRevert = true;
				if (open) ImGuiUtils::EndComponentSection();
				return;
			}
			if (applyComponentRequested) {
				ApplyPrefabInstanceToSource(scene, entity.GetHandle());
				selectionInvalidatedByRevert = true;
				if (open) ImGuiUtils::EndComponentSection();
				return;
			}

			if (open) {
				DispatchComponentInspector(info, entitySpan);
				ImGuiUtils::EndComponentSection();
			}
		};

		// Each C# component and script gets its own section among the built-ins, unless the selection's scripts differ.
		ScriptSectionPass scriptPass;
		const bool scriptSectionsActive = BeginScriptSections(entitySpan, scriptPass) && scriptPass.MultiEdit;
		const ComponentOrderComponent* savedOrder = entity.HasComponent<ComponentOrderComponent>()
			? &entity.GetComponent<ComponentOrderComponent>() : nullptr;
		const InspectorSectionList sections = CollectInspectorSections(registry,
			scriptSectionsActive ? &entity.GetComponent<ScriptComponent>() : nullptr,
			savedOrder ? &savedOrder->Order : nullptr,
			[&](const std::type_index& typeId, const ComponentInfo&) {
				return m_InspectorCache.CommonComponentTypes.contains(typeId);
			});

		// COMPONENT_REF payloads carry the persistent UUID (not RuntimeID): reference fields write it to disk, RuntimeIDs are reallocated on reload.
		uint64_t refEntityId = scene.GetEntityPersistentID(entity.GetHandle());
		if (refEntityId == 0) refEntityId = entity.GetRuntimeID(); // fallback for entities with no UUIDComponent
		const std::string refPrefix = std::to_string(refEntityId) + ":";
		for (std::size_t i = 0; i < sections.Items.size() && !selectionInvalidatedByRevert; ++i) {
			const InspectorSection& section = sections.Items[i];
			if (!section.WholeScriptBlock) {
				ImGuiUtils::SetNextComponentSectionDrag({
					refPrefix + section.RefName,
					"Ref: " + entity.GetName() + "." + section.RefName,
					true,
					i > sections.Pinned,
					i >= sections.Pinned && i + 1 < sections.Items.size() });
			}
			if (section.ScriptKind == 1) DrawManagedComponentSection(scriptPass, section.ScriptIndex);
			else if (section.ScriptKind == 2) DrawScriptSection(scriptPass, section.ScriptIndex);
			else drawComponentSection(section.TypeId, *section.Info);
		}
		if (selectionInvalidatedByRevert) {
			ImGui::EndChild();
			ImGui::End();
			return;
		}
		if (scriptSectionsActive) EndScriptSections(scriptPass);

		// Copy: the inspector cache behind selectedHandles is rebuilt when components change.
		const std::vector<EntityHandle> undoHandles = selectedHandles;

		if (std::optional<ImGuiUtils::ComponentSectionMove> move = ImGuiUtils::TakeComponentSectionMove()) {
			const auto indexOf = [&](const std::string& payload) -> std::ptrdiff_t {
				for (std::size_t i = 0; i < sections.Items.size(); ++i) {
					if (refPrefix + sections.Items[i].RefName == payload) return static_cast<std::ptrdiff_t>(i);
				}
				return -1;
			};
			const std::ptrdiff_t target = indexOf(move->Target);
			const std::ptrdiff_t moved = move->Dragged.empty() ? target : indexOf(move->Dragged);
			// Move Up / Move Down go past the neighbouring section shown here (entities of a multi-selection may have more).
			const std::ptrdiff_t anchor = move->Dragged.empty() ? target + move->Delta : target;
			const bool after = move->Dragged.empty() ? move->Delta > 0 : move->After;
			const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(sections.Items.size());
			if (target >= 0 && moved >= 0 && anchor >= 0 && anchor < count && moved != anchor) {
				const std::string& movedKey = sections.Items[moved].Key;
				const std::string& anchorKey = sections.Items[anchor].Key;
				StructureEditRecorder recorder(scene, undoHandles, "Reorder Components");
				bool changed = false;
				for (const Entity& e : selectedEntities) {
					Entity subject = e;
					const ComponentOrderComponent* own = subject.HasComponent<ComponentOrderComponent>()
						? &subject.GetComponent<ComponentOrderComponent>() : nullptr;
					const InspectorSectionList ownSections = CollectInspectorSections(registry,
						subject.HasComponent<ScriptComponent>() ? &subject.GetComponent<ScriptComponent>() : nullptr,
						own ? &own->Order : nullptr,
						[&](const std::type_index&, const ComponentInfo& info) { return info.has && info.has(subject); });
					std::vector<std::string> keys;
					for (const InspectorSection& item : ownSections.Items) keys.push_back(item.Key);
					std::vector<std::string> reordered = ComponentOrder::Move(keys, movedKey, anchorKey, after, ownSections.Pinned);
					if (reordered == keys) continue;
					if (!subject.HasComponent<ComponentOrderComponent>()) subject.AddComponent<ComponentOrderComponent>();
					subject.GetComponent<ComponentOrderComponent>().Order = std::move(reordered);
					changed = true;
				}
				if (changed) {
					scene.MarkDirty();
					PushStructureUndo(scene, recorder.Finish(scene, undoHandles));
					++m_SelectionVersion;
				}
			}
		}

		if (pendingRemoval != typeid(void)) {
			StructureEditRecorder recorder(scene, undoHandles, "Remove Component");
			// Remove from EVERY selected entity that has it.
			registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
				if (typeId != pendingRemoval || !info.remove) return;
				for (const Entity& e : selectedEntities) {
					if (info.has(e)) info.remove(e);
				}
			});
			scene.MarkDirty();
			PushStructureUndo(scene, recorder.Finish(scene, undoHandles));
			// Component-presence map changed — the cached
			// CommonComponentTypes / PartialComponents must rebuild.
			++m_SelectionVersion;
		}

		if (!hiddenPartialComponents.empty()) {
			std::string text = "Hidden: ";
			for (std::size_t i = 0; i < hiddenPartialComponents.size(); ++i) {
				if (i > 0) text += ", ";
				text += hiddenPartialComponents[i];
			}
			text += " - not on all selected";
			ImGui::TextDisabled("%s", text.c_str());
		}

		ReferencePicker::RenderPopup();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		float buttonWidth = ImGui::GetContentRegionAvail().x;
		if (Icons::ButtonWithIcon(Icons::Type::Plus, "Add Component", ImVec2(buttonWidth, 0), true)) {
			ImGui::OpenPopup("AddComponentPopup");
			m_ComponentSearchBuffer[0] = '\0';
		}

		// MUST sit immediately after the button: placing it after RenderAddComponentPopup binds to a different LastItemData and silently no-ops.
		bool scriptDroppedSomething = false;
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
				StructureEditRecorder scriptRecorder(scene, undoHandles, "Add Script");
				// 1-arg form: see entity-row drop comment above.
				std::string droppedPath(static_cast<const char*>(payload->Data));
				std::vector<EditorScriptDiscovery::ScriptEntry> droppedScripts;
				EditorScriptDiscovery::CollectScriptFile(std::filesystem::path(droppedPath), droppedScripts);
				for (const auto& scriptEntry : droppedScripts) {
					if (scriptEntry.IsSceneSystem || scriptEntry.IsGlobalSystem) {
						continue;
					}
					for (const Entity& e : selectedEntities) {
						if (scriptEntry.IsManagedComponent) {
							scriptDroppedSomething |= EditorScriptAttachment::AttachManagedComponentToEntity(const_cast<Entity&>(e), scene, scriptEntry);
						}
						else {
							scriptDroppedSomething |= EditorScriptAttachment::AttachScriptToEntity(const_cast<Entity&>(e), scene, scriptEntry);
						}
					}
				}
				if (scriptDroppedSomething) PushStructureUndo(scene, scriptRecorder.Finish(scene, undoHandles));
			}
			ImGui::EndDragDropTarget();
		}

		bool addComponentChanged = false;
		std::optional<StructureEditRecorder> addRecorder;
		RenderAddComponentPopup("AddComponentPopup", scene, entitySpan,
			m_ComponentSearchBuffer, sizeof(m_ComponentSearchBuffer),
			&addComponentChanged,
			[&]() { addRecorder.emplace(scene, undoHandles, "Add Component"); });
		if (addComponentChanged && addRecorder) {
			PushStructureUndo(scene, addRecorder->Finish(scene, undoHandles));
		}
		if (addComponentChanged || scriptDroppedSomething) {
			++m_SelectionVersion;
		}
		ImGui::EndChild();

		// IsAnyItemActive fires on focus/click; ActiveIdHasBeenEditedThisFrame only fires on real value edits (drag step, keystroke).
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
			const ImGuiContext& g = *ImGui::GetCurrentContext();
			if (g.ActiveId != 0 && g.ActiveIdHasBeenEditedThisFrame) {
				scene.MarkDirty();
			}
		}
		m_IsInspectorPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		ImGui::End();
	}
}
