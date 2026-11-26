#include "Systems/ImGuiEditorLayer.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/General/General.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/DirectionalLightComponent.hpp"
#include "Components/Graphics/PointLightComponent.hpp"
#include "Components/Graphics/SpotLightComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/UI/UI.hpp"
#include "Core/Application.hpp"
#include "Core/EngineModules.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Graphics/TextureManager.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/EditorShortcuts.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/HierarchyDragData.hpp"
#include "Gui/HierarchyDropPreview.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptAttachment.hpp"
#include "Scripting/ScriptDiscovery.hpp"
#include "Serialization/SceneSerializer.hpp"
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <unordered_set>

namespace Funke {
	namespace {
		bool IsLeftMouseDragPastClickThreshold()
		{
			const ImGuiIO& io = ImGui::GetIO();
			const ImVec2 delta(io.MousePos.x - io.MouseClickedPos[ImGuiMouseButton_Left].x,
				io.MousePos.y - io.MouseClickedPos[ImGuiMouseButton_Left].y);
			return (delta.x * delta.x + delta.y * delta.y) > (io.MouseDragThreshold * io.MouseDragThreshold);
		}
	}

	template <typename T>
	static bool TryComponentIcon(Scene& scene, EntityHandle handle, const char*& icon) {
		if (!scene.HasComponent<T>(handle)) return false;
		const ComponentInfo* info = SceneManager::GetComponentRegistry().GetInfo<T>();
		if (info && !info->icon.empty()) icon = info->icon.c_str();
		return true;
	}

	// Icon (ComponentInfo::icon) of an entity's dominant component, first match wins; shared by the hierarchy rows and the inspector header.
	const char* ImGuiEditorLayer::EntityTypeIconName(Scene& scene, EntityHandle handle) {
		const char* icon = "entity";
		TryComponentIcon<Camera2DComponent>(scene, handle, icon)
			|| TryComponentIcon<Camera3DComponent>(scene, handle, icon)
			|| TryComponentIcon<DirectionalLightComponent>(scene, handle, icon)
			|| TryComponentIcon<PointLightComponent>(scene, handle, icon)
			|| TryComponentIcon<SpotLightComponent>(scene, handle, icon)
			|| TryComponentIcon<ParticleSystem2DComponent>(scene, handle, icon)
			|| TryComponentIcon<ParticleSystem3DComponent>(scene, handle, icon)
			|| TryComponentIcon<AudioSourceComponent>(scene, handle, icon)
			|| TryComponentIcon<TextRendererComponent>(scene, handle, icon)
			|| TryComponentIcon<RectTransform2DComponent>(scene, handle, icon);
		return icon;
	}

	void ImGuiEditorLayer::RenderEntitiesPanel() {
		FUNKE_PROFILE_SCOPE("Hierarchy Panel");
		ImGui::Begin("Entities");

		// Scale the whole panel by the user's row-size preference. FontSizeBase already
		// folds in the global font zoom + DPI, so this composes; icons/arrows/row heights
		// all derive from the text line height and grow together. PopFont before End().
		// Capture the unscaled base first: PushFont overwrites style.FontSizeBase, so right-click
		// popups reset to this captured value (via ScopedPopupFontReset) to stay normal-sized.
		const float hierarchyRowScale = EditorPreferences::GetHierarchyRowScale();
		const bool scaleHierarchyFont = hierarchyRowScale > 1.001f || hierarchyRowScale < 0.999f;
		const float hierarchyBaseFontSize = ImGui::GetStyle().FontSizeBase;
		if (scaleHierarchyFont) {
			ImGui::PushFont(nullptr, hierarchyBaseFontSize * hierarchyRowScale);
		}

		if (IsInPrefabEditMode()) {
			// Mirror IsDirty onto m_PrefabEditDirty so callers (e.g. breadcrumb) read the canonical dirty state.
			m_PrefabEditDirty = m_PrefabEditScene->IsDirty();

			const std::string prefabName = std::filesystem::path(m_PrefabEditPath).stem().string();
			const bool showDirtyMark = m_PrefabEditDirty && !EditorPreferences::GetAutoSavePrefabs();
			const std::string title = showDirtyMark ? (prefabName + " *") : prefabName;

			const float backButtonStartX = ImGui::GetCursorPosX();
			if (EditorUI::IconButton("##PrefabBack", "arrow_left", "Back to scene. Closing Prefab Mode ends this prefab's undo/redo history.")) {
				if (m_PrefabEditDirty) {
					m_ShowPrefabEditDiscardPrompt = true;
				}
				else {
					ClosePrefabEditing(false);
				}
			}

			const float rowEndX = backButtonStartX + ImGui::GetContentRegionAvail().x;
			const float textWidth = ImGui::CalcTextSize(title.c_str()).x;
			const float centerX = backButtonStartX + (rowEndX - backButtonStartX - textWidth) * 0.5f;
			ImGui::SameLine();
			// Guard against very narrow panels where the back button would
			// overlap the centered text; fall back to a normal SameLine gap.
			const float minCenterX = ImGui::GetCursorPosX();
			ImGui::SetCursorPosX(centerX > minCenterX ? centerX : minCenterX);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(title.c_str());
			ImGui::Separator();
		}

		// Hierarchy search bar: a non-empty query filters the entity rows below
		// to name matches (applied by the per-scene filter pass before rendering).
		const float addButtonW = ImGui::GetFrameHeight();
		if (EditorUI::SearchField("##HierarchySearch", m_HierarchySearchBuffer, sizeof(m_HierarchySearchBuffer),
			"Search entities...", ImGui::GetContentRegionAvail().x - addButtonW - ImGui::GetStyle().ItemSpacing.x)) {
			// Clearing the query must restore the unfiltered (collapse-pruned)
			// list, which only the rebuild produces — so force one this frame.
			GetHierarchyState().Dirty = true;
			// A new query changes the visible rows, so a stored
			// range-select anchor (an index into it) is now meaningless — drop
			// it so the next shift-click re-derives the anchor from m_SelectedEntity.
			m_LastEntitySelectionIndex = -1;
		}
		ImGui::SameLine();
		if (EditorUI::IconButton("##HierarchyAdd", "plus", "Create entity")) {
			ImGui::OpenPopup("##HierarchyAddMenu");
		}
		if (ImGui::BeginPopup("##HierarchyAddMenu")) {
			if (scaleHierarchyFont) ImGui::PushFont(nullptr, hierarchyBaseFontSize);
			if (Scene* target = GetContextScene()) {
				RenderCreateEntityMenu(*target, Entity::Null);
			}
			if (scaleHierarchyFont) ImGui::PopFont();
			ImGui::EndPopup();
		}
		ImGui::Separator();

		// Pin the header above (prefab breadcrumb + search bar) and scroll only the
		// entity list: everything below lives in its own child so the search bar
		// stays visible no matter how far the hierarchy is scrolled.
		ImGui::BeginChild("##HierarchyScroll", ImVec2(0.0f, 0.0f),
			ImGuiChildFlags_None, ImGuiWindowFlags_None);

		Scene* activeScene = GetContextScene();

		// Right-click on empty space: create entities in the active scene
		if (activeScene && ImGui::BeginPopupContextWindow("EntityCreateContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
		{
			// Right-click menus stay at the normal size even when the rows are drawn scaled
			// (the scaled font otherwise leaks into the popup); pop before EndPopup so the
			// popup window's font stack stays balanced.
			if (scaleHierarchyFont) ImGui::PushFont(nullptr, hierarchyBaseFontSize);
			Scene& scene = *activeScene;
			RenderCreateEntityMenu(scene, Entity::Null);
			if (scaleHierarchyFont) ImGui::PopFont();
			ImGui::EndPopup();
		}

		std::vector<Scene*> scenesToShow;
		if (IsInPrefabEditMode()) {
			scenesToShow.push_back(m_PrefabEditScene.get());
		}
		else {
			auto loadedScenes = SceneManager::GetLoadedScenes();
			scenesToShow.reserve(loadedScenes.size());
			for (auto& weakScene : loadedScenes) {
				if (auto scenePtr = weakScene.lock()) {
					scenesToShow.push_back(scenePtr.get());
				}
			}
		}
		std::string sceneToRemove;
		struct PendingSceneReorder {
			std::string SourceName;
			size_t TargetIndex;
		};
		std::optional<PendingSceneReorder> pendingSceneReorder;
		HierarchyDropUI::Preview dropPreview;
		// Preview and delivery use the same eligibility checks. In particular, never
		// advertise a destination inside the dragged subtree or outside a prefab root.
		auto acceptEntityDrop = [&](Scene& targetScene, Entity target, HierarchyDropUI::Placement placement,
			const ImVec2& rowMin, const ImVec2& rowMax) -> const ImGuiPayload* {
			const ImGuiPayload* peek = ImGui::GetDragDropPayload();
			if (!peek || !peek->IsDataType("HIERARCHY_ENTITY") || peek->DataSize != sizeof(HierarchyDragData)) return nullptr;
			const auto& data = *static_cast<const HierarchyDragData*>(peek->Data);
			// Match the existing delivery path's same-scene resolution first.
			Scene* sourceScene = static_cast<uint64_t>(targetScene.GetSceneId()) == data.SourceSceneId ? &targetScene : nullptr;
			if (!sourceScene) for (Scene* candidate : scenesToShow) {
				if (candidate && static_cast<uint64_t>(candidate->GetSceneId()) == data.SourceSceneId) {
					sourceScene = candidate;
					break;
				}
			}
			dropPreview = {};
			dropPreview.Active = true;
			dropPreview.Mode = placement;
			dropPreview.RowMin = rowMin;
			dropPreview.RowMax = rowMax;
			const EntityHandle primary = static_cast<EntityHandle>(data.EntityHandle);
			if (!sourceScene || !sourceScene->IsValid(primary)) return nullptr;
			const auto dragged = ResolveDraggedHierarchyEntities(*sourceScene, primary);
			if (sourceScene != &targetScene && (IsInPrefabEditMode() || Application::GetIsPlaying())) return nullptr;
			const bool asChild = placement == HierarchyDropUI::Placement::Child;
			if (sourceScene == &targetScene) {
				for (EntityHandle handle : dragged) {
					Entity moving = sourceScene->GetEntity(handle);
					if (IsInPrefabEditMode() && (handle == m_PrefabEditRootEntity
						|| placement == HierarchyDropUI::Placement::Root)) return nullptr;
					if (target.IsValid() && (moving == target || moving.IsAncestorOf(target))) return nullptr;
					// Parenting an existing child again is a no-op, not an append operation.
					if (asChild && moving.GetParent() == target) return nullptr;
				}
			}
			if (dragged.empty()) return nullptr;
			const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY",
				ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
			if (!payload) return nullptr;
			dropPreview.Allowed = true;
			return payload;
		};

		for (size_t sceneIdx = 0; sceneIdx < scenesToShow.size(); ++sceneIdx) {
			Scene* scenePtrRaw = scenesToShow[sceneIdx];
			if (!scenePtrRaw) continue;
			Scene& scene = *scenePtrRaw;
			auto& hierarchy = SceneHierarchyState::Get(scene);

			const uint64_t sceneIdValue = static_cast<uint64_t>(scene.GetSceneId());
			// Scene* (not sceneId) for the ImGui scope: two scenes can collide on
			// sceneId — duplicated .scene files keep their serialized id, and an
			// additively loaded script-only scene starts at the UUID default.
			// Scene* is always distinct among currently-loaded scenes.
			ImGui::PushID(scenePtrRaw);

			// Prefab mode: skip scene header; drop target would create a second root in the single-rooted prefab.
			bool sceneOpen = true;
			if (!IsInPrefabEditMode()) {
				ImGuiTreeNodeFlags sceneFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow
					| ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
				const std::string fullSceneLabel = scene.IsDirty() ? scene.GetName() + " *" : scene.GetName();
				const std::string sceneLabel = ImGuiUtils::Ellipsize(fullSceneLabel, ImGui::GetContentRegionAvail().x);
				// Use stable str_id (not the label) so the fold state survives the
				// dirty-asterisk toggle that flips on save. Label is supplied via fmt.
				// Neutral frame: the accent Header color is reserved for entity selection.
				ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
				ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
				ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
				sceneOpen = ImGui::TreeNodeEx("##scene_node", sceneFlags, "%s", sceneLabel.c_str());
				ImGui::PopStyleColor(3);
				if (!ImGui::GetDragDropPayload() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
					const size_t entityCount = scene.GetEntityCount();
					ImGui::SetTooltip("%s\n%zu %s", fullSceneLabel.c_str(), entityCount, entityCount == 1 ? "entity" : "entities");
				}
				// Capture immediately after TreeNodeEx; subsequent calls would shift the "last item" rect.
				const ImVec2 sceneItemRectMin = ImGui::GetItemRectMin();
				const ImVec2 sceneItemRectMax = ImGui::GetItemRectMax();
				if (ImGui::IsItemActivated()) {
					m_LastInteractedSceneName = scene.GetName();
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
					SelectSceneNode();
				}

				if (scenesToShow.size() > 1 && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID | ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
					HierarchySceneDragData sceneDrag{ sceneIdValue };
					ImGui::SetDragDropPayload("HIERARCHY_SCENE", &sceneDrag, sizeof(sceneDrag));
					ImGui::EndDragDropSource();
				}

				if (ImGui::BeginDragDropTarget()) {
					const float mouseY = ImGui::GetMousePos().y;
					const float headerMidY = (sceneItemRectMin.y + sceneItemRectMax.y) * 0.5f;
					const bool sceneInTopZone = mouseY < headerMidY;
					if (const ImGuiPayload* peek = ImGui::GetDragDropPayload()) {
						if (peek->IsDataType("HIERARCHY_SCENE")) {
							const float lineY = sceneInTopZone ? sceneItemRectMin.y : sceneItemRectMax.y;
							const ImU32 color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
							ImGui::GetWindowDrawList()->AddLine(
								ImVec2(sceneItemRectMin.x, lineY),
								ImVec2(sceneItemRectMax.x, lineY),
								color, 2.0f);
						}
					}

					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_SCENE")) {
						auto* dragData = static_cast<const HierarchySceneDragData*>(payload->Data);
						if (dragData->SceneId != sceneIdValue) {
							size_t sourceIdx = SIZE_MAX;
							std::string sourceName;
							for (size_t i = 0; i < scenesToShow.size(); ++i) {
								Scene* candidate = scenesToShow[i];
								if (candidate && static_cast<uint64_t>(candidate->GetSceneId()) == dragData->SceneId) {
									sourceIdx = i;
									sourceName = candidate->GetName();
									break;
								}
							}
							if (sourceIdx != SIZE_MAX) {
								// Adjust for the removal shift: when source < target, removing source shifts target index down by 1.
								size_t finalIdx;
								if (sceneInTopZone) {
									finalIdx = (sourceIdx < sceneIdx) ? (sceneIdx - 1) : sceneIdx;
								}
								else {
									finalIdx = (sourceIdx < sceneIdx) ? sceneIdx : (sceneIdx + 1);
								}
								pendingSceneReorder = PendingSceneReorder{ sourceName, finalIdx };
							}
						}
					}

					if (const ImGuiPayload* payload = acceptEntityDrop(scene, Entity::Null, HierarchyDropUI::Placement::Root,
						sceneItemRectMin, sceneItemRectMax); payload && payload->IsDelivery()) {
						auto* dragData = static_cast<const HierarchyDragData*>(payload->Data);
						EntityHandle primaryDraggedHandle = static_cast<EntityHandle>(dragData->EntityHandle);
						const bool sameScene = dragData->SourceSceneId == sceneIdValue;
						if (sameScene) {
							std::vector<EntityHandle> draggedHandles = ResolveDraggedHierarchyEntities(scene, primaryDraggedHandle);
							StructureEditRecorder recorder(scene, draggedHandles, "Reparent");
							bool didMove = false;
							for (EntityHandle h : draggedHandles) {
								Entity draggedEntity = scene.GetEntity(h);
								if (!draggedEntity.HasParent()) continue;
								if (SetEntityParentPreservingWorld(scene, h, Entity::Null)) {
									didMove = true;
								}
							}
							if (didMove) {
								// Force a re-DFS — reparenting changes nesting but
								// not the registry count, so the size-mismatch
								// fallback won't trigger.
								hierarchy.Dirty = true;
								scene.MarkDirty();
								PushStructureUndo(scene, recorder.Finish(scene, draggedHandles));
							}
						}
						else {
							// Cross-scene drag: locate the source scene by
							// the SceneId carried in the payload, resolve
							// the multi-selection against it, then migrate.
							Scene* sourceScene = nullptr;
							for (Scene* candidate : scenesToShow) {
								if (candidate && static_cast<uint64_t>(candidate->GetSceneId()) == dragData->SourceSceneId) {
									sourceScene = candidate;
									break;
								}
							}
							if (sourceScene && sourceScene != &scene) {
								std::vector<EntityHandle> draggedHandles = ResolveDraggedHierarchyEntities(*sourceScene, primaryDraggedHandle);
								std::vector<EntityHandle> migrated = MigrateEntitiesToScene(*sourceScene, scene, draggedHandles, Entity::Null);
								if (!migrated.empty()) {
									// Selection is keyed by raw entt handle; clear source-scene selection so inspector shows the migrated entities.
									ClearEntitySelection();
									m_SelectedEntities = migrated;
									RebuildSelectionSet();
									m_SelectedEntity = migrated.back();
									m_LastInteractedSceneName = scene.GetName();
									hierarchy.Dirty = true;
								}
							}
						}
					}
					ImGui::EndDragDropTarget();
				}

				// Right-click context menu on scene tree node. "Remove"
				// only makes sense with more than one scene loaded.
				if (ImGui::BeginPopupContextItem()) {
					if (scaleHierarchyFont) ImGui::PushFont(nullptr, hierarchyBaseFontSize); // popup at normal size; pop before EndPopup
					const bool canRemove = scenesToShow.size() > 1;
					if (canRemove) {
						if (ImGui::MenuItem("Remove")) {
							sceneToRemove = scene.GetName();
						}
					} else {
						ImGui::MenuItem("Remove", nullptr, false, false);
					}
					EditorUI::Tooltip("Removing this scene ends its undo/redo history, even after saving. Other loaded scenes keep their histories.");
					// Manual reorder fallback for users without drag-and-drop
					// muscle memory (and a keyboard-only accessibility path).
					// Disabled at the boundaries.
					ImGui::Separator();
					const bool canMoveUp = sceneIdx > 0;
					const bool canMoveDown = sceneIdx + 1 < scenesToShow.size();
					if (ImGui::MenuItem("Move Up", nullptr, false, canMoveUp)) {
						pendingSceneReorder = PendingSceneReorder{ scene.GetName(), sceneIdx - 1 };
					}
					if (ImGui::MenuItem("Move Down", nullptr, false, canMoveDown)) {
						pendingSceneReorder = PendingSceneReorder{ scene.GetName(), sceneIdx + 1 };
					}
					if (scaleHierarchyFont) ImGui::PopFont();
					ImGui::EndPopup();
				}
			}

			if (sceneOpen) {
				hierarchy.Update(scene, m_HierarchySearchBuffer);
				const bool hierarchySearchActive = m_HierarchySearchBuffer[0] != '\0';
				if (hierarchySearchActive && hierarchy.VisibleEntityOrder.empty())
					EditorUI::EmptyState("No matching entities");

				const ImVec2 defaultItemSpacing = ImGui::GetStyle().ItemSpacing;
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(defaultItemSpacing.x, 1.0f));

				const auto& guideContinueMask = hierarchy.Guides.GetContinueMasks();
				const auto& guideIsLastChild = hierarchy.Guides.GetLastChildren();

				ImGuiListClipper clipper;
				clipper.Begin(static_cast<int>(hierarchy.VisibleEntityOrder.size()));
					// Gate selection on scene identity: the same entt handle value can exist in multiple loaded scenes.
					Scene* const selectionScene = GetContextScene();
				while (clipper.Step()) {
				for (int entityIdx = clipper.DisplayStart; entityIdx < clipper.DisplayEnd; ++entityIdx) {
					const EntityHandle entityHandle = hierarchy.VisibleEntityOrder[static_cast<std::size_t>(entityIdx)];
					if (!scene.IsValid(entityHandle)) continue;
					Entity entity = scene.GetEntity(entityHandle);
					const bool selected = IsEntitySelected(entityHandle) && &scene == selectionScene;

					const int targetDepth = entityIdx < static_cast<int>(hierarchy.VisibleEntityDepths.size())
						? hierarchy.VisibleEntityDepths[static_cast<std::size_t>(entityIdx)] : 0;

					const int visibleIndex = entityIdx;

					const float rowIndent = static_cast<float>(targetDepth) * 14.0f;
					const ImVec2 rowStart = ImGui::GetCursorScreenPos();
					const ImVec2 dropRowMin(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x * 0.5f, rowStart.y);
					const ImVec2 dropRowMax(rowStart.x + ImGui::GetContentRegionAvail().x,
						rowStart.y + ImGui::GetTextLineHeight());

					// Row highlight is painted before the guides, fold arrow and icon so it always sits behind them.
					{
						const ImVec2 rowPos = ImGui::GetCursorScreenPos();
						const ImVec2 rMin(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x * 0.5f, rowPos.y);
						const ImVec2 rMax(rowPos.x + ImGui::GetContentRegionAvail().x, rowPos.y + ImGui::GetTextLineHeight());
						const bool rowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)
							&& ImGui::IsMouseHoveringRect(rMin, rMax);
						if (selected || (rowHovered && !ImGui::GetDragDropPayload())) {
							const bool pressed = rowHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
							const ImU32 col = (selected || pressed)
								? ImGui::GetColorU32(EditorTheme::Colors::RowSelection(pressed))
								: ImGui::GetColorU32(ImGuiCol_HeaderHovered);
							ImDrawList* rowDraw = ImGui::GetWindowDrawList();
							rowDraw->AddRectFilled(rMin, rMax, col, ImGui::GetStyle().FrameRounding);
							if (selected) {
								const float inset = (rMax.y - rMin.y) * 0.2f;
								rowDraw->AddRectFilled(ImVec2(rMin.x, rMin.y + inset), ImVec2(rMin.x + 3.0f, rMax.y - inset),
									ImGui::GetColorU32(EditorTheme::Colors::Accent()), 1.5f);
							}
						}
					}

					if (targetDepth > 0 && entityIdx < static_cast<int>(guideContinueMask.size())) {
						const ImVec2 rowPos = ImGui::GetCursorScreenPos();
						const float lineH = ImGui::GetTextLineHeight();
						const float half = lineH * 0.5f;
						const float top = rowPos.y - ImGui::GetStyle().ItemSpacing.y;
						const float midY = std::floor(rowPos.y + half) + 0.5f;
						const ImU32 guideCol = ImGui::GetColorU32(ImGuiCol_Text, 0.35f);
						ImDrawList* guideDraw = ImGui::GetWindowDrawList();
						// Line sits under the parent's fold arrow at each level.
						auto levelX = [&](int level) { return std::floor(rowPos.x + (level - 1) * 14.0f + half) + 0.5f; };

						const uint64_t mask = guideContinueMask[static_cast<std::size_t>(entityIdx)];
						for (int k = 1; k < targetDepth && k < 64; ++k) {
							if (mask & (1ull << k)) {
								guideDraw->AddLine(ImVec2(levelX(k), top), ImVec2(levelX(k), rowPos.y + lineH), guideCol);
							}
						}
						const float x = levelX(targetDepth);
						const bool isLast = guideIsLastChild[static_cast<std::size_t>(entityIdx)];
						guideDraw->AddLine(ImVec2(x, top), ImVec2(x, isLast ? midY : rowPos.y + lineH), guideCol);
						const bool rowHasChildren = scene.HasComponent<HierarchyComponent>(entityHandle)
							&& !scene.GetComponent<HierarchyComponent>(entityHandle).Children.empty();
						const float elbowEnd = rowPos.x + rowIndent + (rowHasChildren ? 1.0f : lineH * 0.8f);
						guideDraw->AddLine(ImVec2(x, midY), ImVec2(elbowEnd, midY), guideCol);
					}

					if (rowIndent > 0.0f) ImGui::Indent(rowIndent);

					ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entityHandle)));

					const bool hasChildren = scene.HasComponent<HierarchyComponent>(entityHandle)
						&& !scene.GetComponent<HierarchyComponent>(entityHandle).Children.empty();
					bool isCollapsed = hierarchy.CollapsedEntities.contains(static_cast<uint32_t>(entityHandle));
					{
						const float arrowSize = ImGui::GetTextLineHeight();
						if (hasChildren && !hierarchySearchActive) {
							const ImVec2 arrowTopLeft = ImGui::GetCursorScreenPos();
							if (ImGui::InvisibleButton("##fold", ImVec2(arrowSize, arrowSize))) {
								if (isCollapsed) {
									hierarchy.CollapsedEntities.erase(static_cast<uint32_t>(entityHandle));
								}
								else {
									hierarchy.CollapsedEntities.insert(static_cast<uint32_t>(entityHandle));
								}
								isCollapsed = !isCollapsed;
								// Collapse state feeds into the visible-list
								// pruning done by the DFS rebuild, so any
								// toggle has to trigger a rebuild next frame.
								hierarchy.Dirty = true;
							}
							const ImU32 arrowColor = ImGui::GetColorU32(
								ImGui::IsItemHovered() ? EditorTheme::Colors::Highlight() : ImGui::GetStyleColorVec4(ImGuiCol_Text));
							constexpr float k_ArrowScale = 0.7f;
							const ImVec2 arrowPos(arrowTopLeft.x,
								arrowTopLeft.y + (arrowSize - ImGui::GetFontSize() * k_ArrowScale) * 0.5f);
							ImGui::RenderArrow(ImGui::GetWindowDrawList(), arrowPos,
								arrowColor,
								isCollapsed ? ImGuiDir_Right : ImGuiDir_Down,
								k_ArrowScale);
						}
						else {
							ImGui::Dummy(ImVec2(arrowSize, arrowSize));
						}
						ImGui::SameLine(0.0f, 2.0f);
					}

					bool entityIsDisabled = scene.HasComponent<DisabledTag>(entityHandle);
					if (entityIsDisabled)
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

					// Prefab origin drives both the type icon and the row tint below.
					const bool entityIsPrefab = !Application::GetIsPlaying() && scene.GetEntityOrigin(entityHandle) == EntityOrigin::Prefab;
					bool entityPrefabResolvable = false;
					if (entityIsPrefab) {
						const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entityHandle));
						entityPrefabResolvable = prefabGuid != 0 && !AssetRegistry::ResolvePath(prefabGuid).empty();
					}

					bool entityIsPrefabTinted = false;
					if (!entityIsDisabled && entityIsPrefab) {
						ImGui::PushStyleColor(ImGuiCol_Text,
							entityPrefabResolvable ? EditorTheme::Colors::PrefabInstance() : EditorTheme::Colors::PrefabOrphan());
						entityIsPrefabTinted = true;
					}

					bool entityIsCutMarked = m_CutEntities.contains(static_cast<uint32_t>(entityHandle));
					if (entityIsCutMarked)
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

					// Type icon at the left of every row: prefab / prefab_missing for prefab instances
					// (by whether the source asset resolves), otherwise the generic entity icon.
					{
						const char* entityIconName = entityIsPrefab
							? (entityPrefabResolvable ? "prefab" : "prefab_missing")
							: EntityTypeIconName(scene, entityHandle);
						const float entityIconSize = ImGui::GetTextLineHeight();
						const uint64_t entityIcon = EditorIcons::Get(entityIconName, static_cast<int>(entityIconSize));
						if (entityIcon) {
							// Monochrome entity and text glyphs follow the theme text color.
							const ImVec4 entityIconTint = (std::string_view(entityIconName) == "entity" || std::string_view(entityIconName) == "text")
								? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImVec4(1, 1, 1, 1);
							ImGui::ImageWithBg(static_cast<ImTextureID>(static_cast<intptr_t>(entityIcon)),
								ImVec2(entityIconSize, entityIconSize), ImVec2(0, 1), ImVec2(1, 0),
								ImVec4(0, 0, 0, 0), entityIconTint);
							ImGui::SameLine(0.0f, 4.0f);
						}
					}

					if (m_RenamingEntity == entityHandle) {
						m_EntityRenameFrameCounter++;

						ImGui::PushItemWidth(-1);
						if (m_EntityRenameFrameCounter == 1) {
							ImGui::SetKeyboardFocusHere();
						}

						// Inline edit: row-height field with an accent outline so it sits inside the highlighted row.
						const ImVec4 renameBg = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
						ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x * 0.5f, 0.0f));
						ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
						ImGui::PushStyleColor(ImGuiCol_FrameBg, renameBg);
						ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, renameBg);
						ImGui::PushStyleColor(ImGuiCol_FrameBgActive, renameBg);
						ImGui::PushStyleColor(ImGuiCol_Border, EditorTheme::Colors::Accent());
						bool committed = ImGui::InputText("##EntityRename", m_EntityRenameBuffer, sizeof(m_EntityRenameBuffer),
							ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
						ImGui::PopStyleColor(4);
						ImGui::PopStyleVar(2);

						const bool renameFinished = committed
							|| (!ImGui::IsKeyPressed(ImGuiKey_Escape) && m_EntityRenameFrameCounter > 2 && !ImGui::IsItemActive());
						std::optional<StructureEditRecorder> renameRecorder;
						if (renameFinished && entity.GetName() != std::string_view(m_EntityRenameBuffer)) {
							renameRecorder.emplace(scene, std::span(&entityHandle, 1), "Rename");
						}

						if (committed) {
							std::string newName(m_EntityRenameBuffer);
							if (newName.empty()) {
								if (entity.HasComponent<NameComponent>()) {
									entity.RemoveComponent<NameComponent>();
									scene.MarkDirty();
								}
							}
							else if (entity.HasComponent<NameComponent>()) {
								entity.GetComponent<NameComponent>().Name = newName;
								scene.MarkDirty();
							}
							else {
								scene.AddComponent<NameComponent>(entity.GetHandle(), newName);
								scene.MarkDirty();
							}
							m_RenamingEntity = entt::null;
							m_EntityRenameFrameCounter = 0;
							++m_SelectionVersion; // Bump so inspector re-reads FirstName; stale cache keeps showing pre-rename value.
						}
						else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
							m_RenamingEntity = entt::null;
							m_EntityRenameFrameCounter = 0;
						}
						else if (m_EntityRenameFrameCounter > 2 && !ImGui::IsItemActive()) {
							std::string newName(m_EntityRenameBuffer);
							if (newName.empty()) {
								if (entity.HasComponent<NameComponent>()) {
									entity.RemoveComponent<NameComponent>();
									scene.MarkDirty();
								}
							}
							else if (entity.HasComponent<NameComponent>()) {
								entity.GetComponent<NameComponent>().Name = newName;
								scene.MarkDirty();
							}
							else {
								scene.AddComponent<NameComponent>(entity.GetHandle(), newName);
								scene.MarkDirty();
							}
							m_RenamingEntity = entt::null;
							m_EntityRenameFrameCounter = 0;
							// Same cache-invalidation reason as the committed branch above.
							++m_SelectionVersion;
						}
						if (renameRecorder) PushStructureUndo(scene, renameRecorder->Finish(scene, std::span(&entityHandle, 1)));

						ImGui::PopItemWidth();
					}
					else {
						bool entityLabelTruncated = false;
						const std::string entityLabel = ImGuiUtils::Ellipsize(entity.GetName(), ImGui::GetContentRegionAvail().x, &entityLabelTruncated);
						// Selectable's own fill is suppressed; the full-row highlight was painted at the start of the row.
						ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(0, 0, 0, 0));
						ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
						ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32(0, 0, 0, 0));
						ImGui::Selectable(entityLabel.c_str(), selected);
						ImGui::PopStyleColor(3);
						if (entityLabelTruncated && !ImGui::GetDragDropPayload() && ImGui::IsItemHovered()) {
							ImGui::SetTooltip("%s", entity.GetName().c_str());
						}
						if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
							m_PressedEntity = entityHandle;
						}
						if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
							if (ImGui::IsItemHovered() && m_PressedEntity == entityHandle && !IsLeftMouseDragPastClickThreshold()) {
								const ImGuiIO& io = ImGui::GetIO();
								if (io.KeyShift) {
									SelectEntityRange(scene, visibleIndex);
								}
								else if (io.KeyCtrl) {
									ToggleEntitySelection(entityHandle, visibleIndex);
								}
								else {
									SetSingleEntitySelection(entityHandle, visibleIndex);
								}
								// Record the owning scene so a follow-up
								// "Create Entity" lands in the same scene the
								// user is selecting from.
								if (!IsInPrefabEditMode()) {
									m_LastInteractedSceneName = scene.GetName();
								}
							}
							if (m_PressedEntity == entityHandle) {
								m_PressedEntity = entt::null;
							}
						}

						if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
							m_RenamingEntity = entityHandle;
							m_EntityRenameFrameCounter = 0;
							std::snprintf(m_EntityRenameBuffer, sizeof(m_EntityRenameBuffer), "%s", entity.GetName().c_str());
						}
					}

					// SourceSceneId distinguishes same-scene reorder from cross-scene migration at the drop site.
					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID | ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
						HierarchyDragData dragData{
							entityIdx,
							static_cast<uint32_t>(entityHandle),
							static_cast<uint64_t>(scene.GetSceneId()),
						};
						ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &dragData, sizeof(dragData));
						ImGui::EndDragDropSource();
					}

					const ImVec2 itemRectMin = ImGui::GetItemRectMin();
					const ImVec2 itemRectMax = ImGui::GetItemRectMax();

					// Suppress sibling zones on prefab root: the gate refuses them anyway but without suppression the indicator still flashes.
					const bool suppressSiblingZones = IsInPrefabEditMode()
						&& entityHandle == m_PrefabEditRootEntity;

					// Cover the icon, fold arrow and indentation too, including the one-pixel row gap.
					const ImRect dropRect(ImVec2(dropRowMin.x, itemRectMin.y), ImVec2(dropRowMax.x, itemRectMax.y));
					if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("##HierarchyRowDrop"))) {
						const auto placement = HierarchyDropUI::RowPlacement(ImGui::GetMousePos().y,
							itemRectMin.y, itemRectMax.y, suppressSiblingZones);
						const bool inTopZone = placement == HierarchyDropUI::Placement::Before;
						const bool inBottomZone = placement == HierarchyDropUI::Placement::After;
						const ImGuiPayload* entityPayload = acceptEntityDrop(scene, entity, placement, dropRowMin, dropRowMax);
						if (entityPayload) {
							const int slot = HierarchyDropUI::InsertionSlot(hierarchy.VisibleEntityDepths, entityIdx, placement);
							dropPreview.ShowInsertion = slot >= 0 && !hierarchySearchActive;
							dropPreview.Left = rowStart.x + rowIndent + (placement == HierarchyDropUI::Placement::Child ? 14.0f : 0.0f);
							dropPreview.Right = dropRowMax.x;
							dropPreview.Y = rowStart.y + static_cast<float>(slot - entityIdx)
								* (ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y) - ImGui::GetStyle().ItemSpacing.y * 0.5f;
						}

						if (const ImGuiPayload* payload = entityPayload; payload && payload->IsDelivery()) {
							auto* dragData = static_cast<const HierarchyDragData*>(payload->Data);
							EntityHandle primaryDraggedHandle = static_cast<EntityHandle>(dragData->EntityHandle);
							const uint64_t targetSceneIdValue = static_cast<uint64_t>(scene.GetSceneId());
							const bool sameScene = dragData->SourceSceneId == targetSceneIdValue;
							// Reject cross-scene migration into prefab edit mode: would import foreign PrefabGUID/origin metadata into the asset.
							if (!sameScene && IsInPrefabEditMode()) {
								// drop ignored
							}
							else if (!sameScene) {
								Scene* sourceScene = nullptr;
								for (Scene* candidate : scenesToShow) {
									if (candidate && static_cast<uint64_t>(candidate->GetSceneId()) == dragData->SourceSceneId) {
										sourceScene = candidate;
										break;
									}
								}
								if (sourceScene && sourceScene != &scene) {
									std::vector<EntityHandle> sourceHandles = ResolveDraggedHierarchyEntities(*sourceScene, primaryDraggedHandle);
									std::vector<EntityHandle> migrated = MigrateEntitiesToScene(*sourceScene, scene, sourceHandles, Entity::Null);
									if (!migrated.empty()) {
										// Splice migrated handles into hierarchy.EntityOrder immediately; MoveSiblingNextTo only repositions entries already in the list.
										for (EntityHandle h : migrated) {
											hierarchy.EntityOrder.push_back(h);
										}
										if (inTopZone || inBottomZone) {
											EntityHandle anchor = entityHandle;
											if (inBottomZone) {
												for (EntityHandle h : migrated) {
													MoveSiblingNextTo(scene, h, anchor, /*insertAfter*/true);
													anchor = h;
												}
											}
											else {
												for (auto it = migrated.rbegin(); it != migrated.rend(); ++it) {
													MoveSiblingNextTo(scene, *it, anchor, /*insertAfter*/false);
													anchor = *it;
												}
											}
										}
										else {
											// Middle-zone reparent under `entity`.
											for (EntityHandle h : migrated) {
												SetEntityParentPreservingWorld(scene, h, entity);
											}
											hierarchy.CollapsedEntities.erase(static_cast<uint32_t>(entityHandle));
										}
										ClearEntitySelection();
										m_SelectedEntities = migrated;
										RebuildSelectionSet();
										m_SelectedEntity = migrated.back();
										m_LastInteractedSceneName = scene.GetName();
										hierarchy.Dirty = true;
									}
								}
							}
							else {
							std::vector<EntityHandle> draggedHandles = ResolveDraggedHierarchyEntities(scene, primaryDraggedHandle);
							// Root-only reorders live in hierarchy.EntityOrder; replaying them through a snapshot would push roots to the end.
							const bool targetIsRoot = !entity.HasParent();
							bool recordMove = !(inTopZone || inBottomZone) || !targetIsRoot;
							for (EntityHandle h : draggedHandles) recordMove |= scene.GetEntity(h).HasParent();
							std::optional<StructureEditRecorder> moveRecorder;
							if (recordMove) moveRecorder.emplace(scene, draggedHandles, (inTopZone || inBottomZone) ? "Reorder" : "Reparent");
							bool didMove = false;
							if (inTopZone || inBottomZone) {
								// Multi-drop: advance anchor so each entity lands next to the previous one in selection order.
								EntityHandle anchor = entityHandle;
								if (inBottomZone) {
									for (EntityHandle h : draggedHandles) {
										if (h == anchor) continue;
										if (MoveSiblingNextTo(scene, h, anchor, /*insertAfter*/true)) {
											didMove = true;
											anchor = h;
										}
									}
								}
								else {
									for (auto it = draggedHandles.rbegin(); it != draggedHandles.rend(); ++it) {
										EntityHandle h = *it;
										if (h == anchor) continue;
										if (MoveSiblingNextTo(scene, h, anchor, /*insertAfter*/false)) {
											didMove = true;
											anchor = h;
										}
									}
								}
							}
							else {
								for (EntityHandle h : draggedHandles) {
									if (h == entityHandle) continue;
									Entity draggedEntity = scene.GetEntity(h);
									if (draggedEntity.IsAncestorOf(entity)) continue;
									if (SetEntityParentPreservingWorld(scene, h, entity)) {
										didMove = true;
									}
								}
								if (didMove) {
									hierarchy.CollapsedEntities.erase(static_cast<uint32_t>(entityHandle));
									// Force re-DFS: reparenting doesn't change entity count so the size-mismatch heuristic won't trigger it automatically.
									hierarchy.Dirty = true;
								}
							}
							if (didMove) {
								scene.MarkDirty();
								if (moveRecorder) PushStructureUndo(scene, moveRecorder->Finish(scene, draggedHandles));
							}
							}
						}
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
							// 1-arg form: asset-browser sends size()+1 bytes with a trailing '\0'; 2-arg would embed it in the string, breaking extension comparisons.
							std::string droppedPath(static_cast<const char*>(payload->Data));
							std::string ext = std::filesystem::path(droppedPath).extension().string();
							std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
							if (ext == ".prefab") {
								// Reject nested-prefab drop in prefab edit mode: would create a second root or allow self-recursion.
								if (!IsInPrefabEditMode()) {
									EntityHandle loaded = SceneSerializer::LoadEntityFromFile(scene, droppedPath);
									if (loaded != entt::null) {
										EnsureEditorUniqueEntityNames(scene, { loaded });
										// Drop ONTO an entity row parents the prefab under that entity (mirrors the
										// HIERARCHY_ENTITY reparent handler above); without this the instance stayed a root.
										SetEntityParentPreservingWorld(scene, loaded, entity);
										hierarchy.CollapsedEntities.erase(static_cast<uint32_t>(entityHandle));
										SelectEntity(loaded);
										scene.MarkDirty();
										RecordCreatedEntities(scene, std::span(&loaded, 1), "Instantiate Prefab");
									}
									hierarchy.EntityOrder.clear(); hierarchy.Dirty = true;
								}
							}
							else {
								std::vector<EditorScriptDiscovery::ScriptEntry> droppedScripts;
								EditorScriptDiscovery::CollectScriptFile(std::filesystem::path(droppedPath), droppedScripts);
								StructureEditRecorder scriptRecorder(scene, std::span(&entityHandle, 1), "Add Script");
								bool scriptAttached = false;
								for (const auto& scriptEntry : droppedScripts) {
									if (scriptEntry.IsSceneSystem || scriptEntry.IsGlobalSystem) {
										continue;
									}
									if (scriptEntry.IsManagedComponent) {
										scriptAttached |= EditorScriptAttachment::AttachManagedComponentToEntity(entity, scene, scriptEntry);
									}
									else {
										scriptAttached |= EditorScriptAttachment::AttachScriptToEntity(entity, scene, scriptEntry);
									}
								}
								// Invalidate the inspector cache so the new
								// component shows up immediately if the dropped-on
								// entity happens to be selected.
								if (scriptAttached) {
									++m_SelectionVersion;
									PushStructureUndo(scene, scriptRecorder.Finish(scene, std::span(&entityHandle, 1)));
								}
							}
						}
						ImGui::EndDragDropTarget();
					}

					// Pop tints before context menu: popups inherit the live style stack, so tints would bleed into menu item text.
					if (entityIsCutMarked)
						ImGui::PopStyleColor();
					if (entityIsPrefabTinted)
						ImGui::PopStyleColor();
					if (entityIsDisabled)
						ImGui::PopStyleColor();

					if (ImGui::BeginPopupContextItem())
					{
						if (scaleHierarchyFont) ImGui::PushFont(nullptr, hierarchyBaseFontSize); // popup at normal size; pop before EndPopup
						RenderCreateEntityMenu(scene, entity);


						ImGui::Separator();

						if (!(IsEntitySelected(entityHandle) && &scene == selectionScene)) {
							SetSingleEntitySelection(entityHandle, visibleIndex);
							if (!IsInPrefabEditMode()) m_LastInteractedSceneName = scene.GetName();
						}

						if (ImGui::MenuItem("Delete", EditorShortcuts::k_DeleteLabel))
						{
							RequestDeleteSelectedEntity(scene);
						}

						if (ImGui::MenuItem("Cut", EditorShortcuts::k_CutLabel))
						{
							CutSelectedEntities(scene);
						}

						if (ImGui::MenuItem("Duplicate", EditorShortcuts::k_DuplicateLabel))
						{
							DuplicateSelectedEntity(scene);
						}

						if (ImGui::MenuItem("Rename", EditorShortcuts::k_RenameLabel))
						{
							BeginRenameSelectedEntity(scene);
						}

						// "Unpack Prefab" only shows up when at least one
						// selected entity is currently a prefab instance —
						// no point cluttering the menu otherwise.
						bool anySelectedIsPrefabInstance = false;
						for (EntityHandle h : GetSelectedEntities(scene)) {
							if (scene.HasComponent<PrefabInstanceComponent>(h)) {
								anySelectedIsPrefabInstance = true;
								break;
							}
						}
						if (anySelectedIsPrefabInstance) {
							ImGui::Separator();
							if (ImGui::MenuItem("Unpack Prefab", "Shift + U")) {
								UnpackSelectedPrefabs(scene);
							}
						}

						if (scaleHierarchyFont) ImGui::PopFont();
						ImGui::EndPopup();
					}

					ImGui::PopID();

					if (rowIndent > 0.0f) ImGui::Unindent(rowIndent);
				}
				} // while (clipper.Step())
				clipper.End();

				ImGui::PopStyleVar(); // ItemSpacing

				// TreePop matches the TreeNodeEx above, which prefab-edit
				// mode skips — balance the call accordingly.
				if (!IsInPrefabEditMode()) {
					ImGui::TreePop();
				}
			}

			ImGui::PopID();
		}

		// Deferred scene removal (after iteration)
		if (!sceneToRemove.empty()) {
			RequestEditorAction(PendingEditorAction::RemoveScene, std::move(sceneToRemove));
		}

		if (pendingSceneReorder) {
			SceneManager::MoveLoadedScene(pendingSceneReorder->SourceName, pendingSceneReorder->TargetIndex);
			GetHierarchyState().Dirty = true;
		}

		// Drag-drop target: only present during an active drag so it doesn't block right-click menus
		if (ImGui::GetDragDropPayload() != nullptr) {
			ImVec2 avail = ImGui::GetContentRegionAvail();
			if (avail.y > 0) {
				ImGui::InvisibleButton("##SceneDropTarget", ImVec2(-1, avail.y));
				const ImVec2 tailMin = ImGui::GetItemRectMin();
				const ImVec2 tailMax(ImGui::GetItemRectMax().x, tailMin.y + ImGui::GetTextLineHeight());
				if (ImGui::BeginDragDropTarget()) {
					// Drop to bottom panel: detaches entity to root and moves it to end of the scene root order. Disabled in prefab mode (would create second root).
					const ImGuiPayload* tailPayload = nullptr;
					if (Scene* dropScene = GetContextScene(); dropScene && !IsInPrefabEditMode()) {
						tailPayload = acceptEntityDrop(*dropScene, Entity::Null, HierarchyDropUI::Placement::Root, tailMin, tailMax);
					}
					if (!IsInPrefabEditMode())
					if (const ImGuiPayload* payload = tailPayload; payload && payload->IsDelivery()) {
						auto* dragData = static_cast<const HierarchyDragData*>(payload->Data);
						EntityHandle primaryDraggedHandle = static_cast<EntityHandle>(dragData->EntityHandle);
						Scene* dropScene = GetContextScene();
						if (dropScene) {
							const uint64_t dropSceneIdValue = static_cast<uint64_t>(dropScene->GetSceneId());
							const bool sameScene = dragData->SourceSceneId == dropSceneIdValue;
							if (!sameScene) {
								Scene* sourceScene = nullptr;
								auto loadedScenes = SceneManager::GetLoadedScenes();
								for (auto& weakScene : loadedScenes) {
									if (auto scenePtr = weakScene.lock();
										scenePtr && static_cast<uint64_t>(scenePtr->GetSceneId()) == dragData->SourceSceneId) {
										sourceScene = scenePtr.get();
										break;
									}
								}
								if (sourceScene && sourceScene != dropScene) {
									std::vector<EntityHandle> sourceHandles = ResolveDraggedHierarchyEntities(*sourceScene, primaryDraggedHandle);
									std::vector<EntityHandle> migrated = MigrateEntitiesToScene(*sourceScene, *dropScene, sourceHandles, Entity::Null);
									if (!migrated.empty()) {
										ClearEntitySelection();
										m_SelectedEntities = migrated;
										RebuildSelectionSet();
										m_SelectedEntity = migrated.back();
										m_LastInteractedSceneName = dropScene->GetName();
										GetHierarchyState().Dirty = true;
									}
								}
							}
							else {
							std::vector<EntityHandle> draggedHandles = ResolveDraggedHierarchyEntities(*dropScene, primaryDraggedHandle);
							StructureEditRecorder recorder(*dropScene, draggedHandles, "Reparent");
							bool didMove = false;
							for (EntityHandle h : draggedHandles) {
								Entity draggedEntity = dropScene->GetEntity(h);
								if (draggedEntity.HasParent()) {
									if (SetEntityParentPreservingWorld(*dropScene, h, Entity::Null)) {
										didMove = true;
									}
								}
								auto orderIt = std::find(GetHierarchyState().EntityOrder.begin(), GetHierarchyState().EntityOrder.end(), h);
								if (orderIt != GetHierarchyState().EntityOrder.end() && std::next(orderIt) != GetHierarchyState().EntityOrder.end()) {
									GetHierarchyState().EntityOrder.erase(orderIt);
									GetHierarchyState().EntityOrder.push_back(h);
									GetHierarchyState().Dirty = true; // M30: re-DFS next render.
								}
							}
							if (didMove) {
								GetHierarchyState().Dirty = true;
								dropScene->MarkDirty();
								PushStructureUndo(*dropScene, recorder.Finish(*dropScene, draggedHandles));
							}
							}
						}
					}
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
						std::string droppedPath(static_cast<const char*>(payload->Data));
						std::string ext = std::filesystem::path(droppedPath).extension().string();
						std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
						// Scene-level drops create root entities; reject in prefab mode (violates single-root invariant; .prefab could recurse if same asset).
						if (IsInPrefabEditMode()) {
							// drop ignored
						}
						else if (ext == ".scene") {
							m_PendingSceneFileDrop = droppedPath;
						}
						else if (ext == ".prefab") {
							Scene* dropScene = GetContextScene();
							if (dropScene) {
								EntityHandle loaded = SceneSerializer::LoadEntityFromFile(*dropScene, droppedPath);
								if (loaded != entt::null) {
									EnsureEditorUniqueEntityNames(*dropScene, { loaded });
									RecordCreatedEntities(*dropScene, std::span(&loaded, 1), "Instantiate Prefab");
								}
								GetHierarchyState().EntityOrder.clear(); GetHierarchyState().Dirty = true;
							}
						}
						else if ((ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
							&& !EngineModules::IsActive(EngineModule::TwoD)) {
							// A dropped image becomes a Sprite Renderer on a Transform2D entity.
							FNK_WARN_TAG("Editor", "Dropping an image creates a sprite, which needs the Funke.2D module (Project Settings > Build > Engine Modules).");
						}
						else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
							Scene* dropScene = GetContextScene();
							if (dropScene) {
								std::string entityName = std::filesystem::path(droppedPath).stem().string();
								Entity newEntity = dropScene->CreateEntity(entityName);
								auto& sr = newEntity.AddComponent<SpriteRendererComponent>();
								sr.TextureHandle = TextureManager::LoadTexture(droppedPath);

								Texture2D* tex = TextureManager::GetTexture(sr.TextureHandle);
								if (tex && tex->IsValid() && tex->GetHeight() > 0) {
									float aspect = (float)tex->GetWidth() / (float)tex->GetHeight();
									auto& transform = newEntity.GetComponent<Transform2DComponent>();
									transform.Scale = { aspect, 1.0f };
								}

								EnsureEditorUniqueEntityNames(*dropScene, { newEntity.GetHandle() });
								SelectEntity(newEntity.GetHandle());
								dropScene->MarkDirty();
								const EntityHandle createdHandle = newEntity.GetHandle();
								RecordCreatedEntities(*dropScene, std::span(&createdHandle, 1), "Create Sprite");
							}
						}
						else {
							// Script dropped on empty space → new root entity named after the script with the script attached. Scene/global systems aren't entity components (the Systems inspector handles those).
							Scene* dropScene = GetContextScene();
							if (dropScene) {
								std::vector<EditorScriptDiscovery::ScriptEntry> droppedScripts;
								EditorScriptDiscovery::CollectScriptFile(std::filesystem::path(droppedPath), droppedScripts);

								Entity newEntity = Entity::Null;
								bool created = false;
								for (const auto& scriptEntry : droppedScripts) {
									if (scriptEntry.IsSceneSystem || scriptEntry.IsGlobalSystem) {
										continue;
									}
									if (!created) {
										const auto name = scriptEntry.Type == ScriptType::Bolt
											? Path::ToUtf8(scriptEntry.Path.stem())
											: EditorScriptDiscovery::ClassDisplayName(scriptEntry.ClassName);
										newEntity = dropScene->CreateEntity(name);
										created = true;
									}
									if (scriptEntry.IsManagedComponent) {
										EditorScriptAttachment::AttachManagedComponentToEntity(newEntity, *dropScene, scriptEntry);
									}
									else {
										EditorScriptAttachment::AttachScriptToEntity(newEntity, *dropScene, scriptEntry);
									}
								}

								if (created) {
									EnsureEditorUniqueEntityNames(*dropScene, { newEntity.GetHandle() });
									SelectEntity(newEntity.GetHandle());
									dropScene->MarkDirty();
									GetHierarchyState().EntityOrder.clear();
									GetHierarchyState().Dirty = true;
									++m_SelectionVersion;
									const EntityHandle createdHandle = newEntity.GetHandle();
									RecordCreatedEntities(*dropScene, std::span(&createdHandle, 1), "Create Entity");
								}
							}
						}
					}
					ImGui::EndDragDropTarget();
				}
			}
		}

		if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)
			&& ImGui::IsMouseReleased(ImGuiMouseButton_Left)
			&& !IsLeftMouseDragPastClickThreshold()
			&& !ImGui::IsAnyItemHovered()) {
			ClearEntitySelection();
		}

		dropPreview.Draw();
		ImGui::EndChild(); // ##HierarchyScroll

		// RootAndChildWindows so the scroll child counts as panel focus for shortcuts.
		m_IsEntitiesPanelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (scaleHierarchyFont) {
			ImGui::PopFont();
		}
		ImGui::End();
	}
}
