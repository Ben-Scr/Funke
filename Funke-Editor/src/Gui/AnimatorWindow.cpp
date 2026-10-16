#include "Gui/AnimatorWindow.hpp"

#include "Animation/AnimationClip.hpp"
#include "Animation/AnimatorRuntime.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Components/General/AnimatorComponent.hpp"
#include "Core/Application.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Inspector/PropertyDrawer.hpp"
#include "Inspector/PropertyRegistration.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Path.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace Funke {
	namespace {
		constexpr size_t k_MaxUndo = 100;

		ImU32 WithAlpha(ImVec4 color, float alpha) {
			color.w *= alpha;
			return ImGui::GetColorU32(color);
		}

		std::string AssetStem(uint64_t guid) {
			const std::string path = AssetRegistry::ResolvePath(guid);
			return path.empty() ? std::string{} : std::filesystem::path(path).stem().string();
		}

		ImVec2 Add(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
		ImVec2 Sub(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x - b.x, a.y - b.y); }
		ImVec2 Mul(const ImVec2& a, float s) { return ImVec2(a.x * s, a.y * s); }

		// Where the segment from the rect's center towards target leaves the rect.
		ImVec2 RectEdgePoint(const ImVec2& min, const ImVec2& max, const ImVec2& target) {
			const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
			const ImVec2 d = Sub(target, center);
			const float hw = (max.x - min.x) * 0.5f;
			const float hh = (max.y - min.y) * 0.5f;
			if (std::abs(d.x) < 0.0001f && std::abs(d.y) < 0.0001f) return center;
			const float tx = std::abs(d.x) > 0.0001f ? hw / std::abs(d.x) : 1e9f;
			const float ty = std::abs(d.y) > 0.0001f ? hh / std::abs(d.y) : 1e9f;
			const float t = std::min({ tx, ty, 1.0f });
			return Add(center, Mul(d, t));
		}

		float DistanceToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
			const ImVec2 ab = Sub(b, a);
			const float len2 = ab.x * ab.x + ab.y * ab.y;
			float t = len2 > 0.0f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2 : 0.0f;
			t = std::clamp(t, 0.0f, 1.0f);
			const ImVec2 q = Add(a, Mul(ab, t));
			return std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y));
		}

		const char* ConditionModeLabel(AnimatorConditionMode mode) {
			switch (mode) {
			case AnimatorConditionMode::Greater: return "Greater";
			case AnimatorConditionMode::Less: return "Less";
			case AnimatorConditionMode::Equals: return "Equals";
			case AnimatorConditionMode::NotEquals: return "Not Equal";
			case AnimatorConditionMode::If: return "True";
			case AnimatorConditionMode::IfNot: return "False";
			}
			return "";
		}

		const char* ParameterTypeLabel(AnimatorParameterType type) {
			switch (type) {
			case AnimatorParameterType::Float: return "Float";
			case AnimatorParameterType::Int: return "Int";
			case AnimatorParameterType::Bool: return "Bool";
			case AnimatorParameterType::Trigger: return "Trigger";
			}
			return "";
		}
	}

	// ── Target and working copy ──────────────────────────────────────

	void AnimatorWindow::OpenController(uint64_t guid) {
		m_OpenedController = guid;
	}

	void AnimatorWindow::ResolveTarget(Scene* scene, EntityHandle selected) {
		m_Scene = scene;
		m_Animator = entt::null;
		uint64_t selectedController = 0;
		if (scene && scene->GetRegistry().valid(selected)) {
			Entity entity = scene->GetEntity(selected);
			while (entity.IsValid()) {
				if (const AnimatorComponent* animator = scene->GetRegistry().try_get<AnimatorComponent>(entity.GetHandle())) {
					m_Animator = entity.GetHandle();
					selectedController = animator->Controller;
					break;
				}
				entity = entity.GetParent();
			}
		}
		// An Animator selected with a controller of its own wins over a controller opened from the Content Browser.
		if (selectedController != 0 && selectedController != m_OpenedController) {
			m_ControllerGuid = selectedController;
		}
		else if (m_OpenedController != 0) {
			m_ControllerGuid = m_OpenedController;
		}
		else {
			m_ControllerGuid = selectedController;
		}
	}

	void AnimatorWindow::SyncWorkingCopy() {
		if (m_ControllerGuid == 0) {
			m_Loaded = false;
			m_LoadedGuid = 0;
			return;
		}
		std::shared_ptr<const AnimatorController> controller = AnimatorControllerLibrary::Get(m_ControllerGuid);
		if (!controller) {
			m_Loaded = false;
			return;
		}
		if (m_LoadedGuid != m_ControllerGuid) {
			m_Controller = *controller;
			m_LoadedPtr = controller;
			m_LoadedGuid = m_ControllerGuid;
			m_ControllerName = AssetStem(m_ControllerGuid);
			m_FrameOnNextDraw = true;
			m_UndoStack.clear();
			m_RedoStack.clear();
			m_SelectedNode = k_NoNode;
			m_SelectedTransition = -1;
			m_MakingTransitionFrom = k_NoNode;
		}
		else if (controller != m_LoadedPtr && m_DraggingNode == k_NoNode) {
			m_Controller = *controller;
			m_LoadedPtr = controller;
			if (m_SelectedTransition >= static_cast<int>(m_Controller.Transitions.size())) m_SelectedTransition = -1;
			if (m_SelectedNode >= static_cast<int>(m_Controller.States.size())) m_SelectedNode = k_NoNode;
		}
		m_Loaded = true;
	}

	void AnimatorWindow::BeginEdit() {
		m_UndoStack.push_back(m_Controller);
		if (m_UndoStack.size() > k_MaxUndo) m_UndoStack.erase(m_UndoStack.begin());
		m_RedoStack.clear();
	}

	void AnimatorWindow::CommitEdit() {
		if (!m_Loaded) return;
		AnimatorControllerLibrary::Save(m_ControllerGuid, m_Controller);
		m_LoadedPtr = AnimatorControllerLibrary::Get(m_ControllerGuid);
	}

	void AnimatorWindow::LiveEdit() {
		if (!m_Loaded) return;
		AnimatorControllerLibrary::Set(m_ControllerGuid, m_Controller);
		m_LoadedPtr = AnimatorControllerLibrary::Get(m_ControllerGuid);
	}

	void AnimatorWindow::Undo() {
		if (m_UndoStack.empty()) return;
		m_RedoStack.push_back(m_Controller);
		m_Controller = m_UndoStack.back();
		m_UndoStack.pop_back();
		m_SelectedNode = k_NoNode;
		m_SelectedTransition = -1;
		CommitEdit();
	}

	void AnimatorWindow::Redo() {
		if (m_RedoStack.empty()) return;
		m_UndoStack.push_back(m_Controller);
		m_Controller = m_RedoStack.back();
		m_RedoStack.pop_back();
		m_SelectedNode = k_NoNode;
		m_SelectedTransition = -1;
		CommitEdit();
	}

	void AnimatorWindow::RequestNewController() {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project || !m_Scene || !m_Scene->GetRegistry().valid(m_Animator)) return;
		Entity entity = m_Scene->GetEntity(m_Animator);
		const AnimatorComponent& animator = entity.GetComponent<AnimatorComponent>();

		namespace fs = std::filesystem;
		// Next to the clip the Animator plays today.
		const std::string clipPath = animator.Clip != 0 ? AssetRegistry::ResolvePath(animator.Clip) : std::string{};
		const fs::path folder = clipPath.empty() ? Path::FromUtf8(project->AssetsDirectory) : Path::FromUtf8(clipPath).parent_path();
		const std::string stem = entity.GetName().empty() ? std::string("New Controller") : entity.GetName();
		m_NewControllerTarget = m_Animator;
		m_NewControllerDialog.Open(AssetSaveDialog::UniquePath(folder, stem, k_AnimatorControllerExtension), "Animator Controller", k_AnimatorControllerExtension);
	}

	void AnimatorWindow::CreateControllerAt(const std::filesystem::path& path) {
		if (!m_Scene || m_Animator != m_NewControllerTarget || !m_Scene->GetRegistry().valid(m_Animator)) return;
		AnimatorComponent* animatorPtr = m_Scene->GetRegistry().try_get<AnimatorComponent>(m_Animator);
		if (!animatorPtr) return;
		AnimatorComponent& animator = *animatorPtr;

		AnimatorController controller;
		if (animator.Clip != 0) {
			AnimatorState state;
			state.Name = AssetStem(animator.Clip).empty() ? std::string("Default") : AssetStem(animator.Clip);
			state.Clip = animator.Clip;
			controller.States.push_back(std::move(state));
		}
		const std::string pathUtf8 = Path::ToUtf8(path);
		if (!AnimatorControllerLibrary::SaveToFile(pathUtf8, controller)) return;
		const uint64_t guid = AssetRegistry::ImportAssetPathIncremental(pathUtf8);
		if (guid == 0) return;
		// A confirmed overwrite keeps the old GUID; drop its cached contents.
		AnimatorControllerLibrary::Invalidate(guid);
		animator.Controller = guid;
		animator.Clip = 0;
		animator.Runtime.Reset();
		m_Scene->MarkDirty();
		m_OpenedController = 0;
		m_ControllerGuid = guid;
	}

	AnimatorRuntime* AnimatorWindow::LiveRuntime() const {
		if (!Application::GetIsPlaying() || !m_Scene || !m_Scene->GetRegistry().valid(m_Animator)) return nullptr;
		const AnimatorComponent* animator = m_Scene->GetRegistry().try_get<AnimatorComponent>(m_Animator);
		if (!animator || animator->Controller != m_ControllerGuid) return nullptr;
		return animator->Runtime.Get();
	}

	// ── Graph geometry ───────────────────────────────────────────────

	ImVec2 AnimatorWindow::NodeSize() const {
		return ImVec2(ImGui::GetFontSize() * 10.5f * m_Zoom, ImGui::GetFrameHeight() * 1.6f * m_Zoom);
	}

	glm::vec2 AnimatorWindow::NodePosition(int node) const {
		if (node == k_EntryNode) return m_Controller.EntryPosition;
		if (node == k_AnyNode) return m_Controller.AnyStatePosition;
		if (node >= 0 && node < static_cast<int>(m_Controller.States.size())) return m_Controller.States[static_cast<size_t>(node)].Position;
		return glm::vec2(0.0f);
	}

	void AnimatorWindow::SetNodePosition(int node, const glm::vec2& position) {
		if (node == k_EntryNode) m_Controller.EntryPosition = position;
		else if (node == k_AnyNode) m_Controller.AnyStatePosition = position;
		else if (node >= 0 && node < static_cast<int>(m_Controller.States.size())) m_Controller.States[static_cast<size_t>(node)].Position = position;
	}

	ImVec2 AnimatorWindow::NodeScreenPos(int node) const {
		const glm::vec2 p = NodePosition(node);
		const ImVec2 size = NodeSize();
		return ImVec2(m_CanvasOrigin.x + m_Pan.x + p.x * m_Zoom - size.x * 0.5f, m_CanvasOrigin.y + m_Pan.y + p.y * m_Zoom - size.y * 0.5f);
	}

	int AnimatorWindow::HitNode(const ImVec2& point) const {
		const ImVec2 size = NodeSize();
		auto inside = [&](int node) {
			const ImVec2 min = NodeScreenPos(node);
			return point.x >= min.x && point.x <= min.x + size.x && point.y >= min.y && point.y <= min.y + size.y;
		};
		for (int i = static_cast<int>(m_Controller.States.size()) - 1; i >= 0; --i) if (inside(i)) return i;
		if (inside(k_AnyNode)) return k_AnyNode;
		if (inside(k_EntryNode)) return k_EntryNode;
		return k_NoNode;
	}

	bool AnimatorWindow::TransitionSegment(int transition, ImVec2& outFrom, ImVec2& outTo) const {
		const AnimatorTransition& t = m_Controller.Transitions[static_cast<size_t>(transition)];
		const ImVec2 half = Mul(NodeSize(), 0.5f);
		const ImVec2 fromCenter = Add(NodeScreenPos(t.From), half);
		const ImVec2 toCenter = Add(NodeScreenPos(t.To), half);
		ImVec2 dir = Sub(toCenter, fromCenter);
		const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
		if (len < 1.0f) return false;
		dir = Mul(dir, 1.0f / len);
		const bool reverse = std::any_of(m_Controller.Transitions.begin(), m_Controller.Transitions.end(),
			[&](const AnimatorTransition& o) { return o.From == t.To && o.To == t.From; });
		const ImVec2 shift = Mul(ImVec2(-dir.y, dir.x), reverse ? ImGui::GetFontSize() * 0.35f * m_Zoom : 0.0f);
		outFrom = Add(RectEdgePoint(Sub(fromCenter, half), Add(fromCenter, half), toCenter), shift);
		outTo = Add(RectEdgePoint(Sub(toCenter, half), Add(toCenter, half), fromCenter), shift);
		return true;
	}

	int AnimatorWindow::HitTransition(const ImVec2& point) const {
		const float tolerance = std::max(6.0f, ImGui::GetFontSize() * 0.45f);
		int best = -1;
		float bestDistance = tolerance;
		for (int i = 0; i < static_cast<int>(m_Controller.Transitions.size()); ++i) {
			ImVec2 a, b;
			if (!TransitionSegment(i, a, b)) continue;
			const float distance = DistanceToSegment(point, a, b);
			if (distance <= bestDistance) {
				bestDistance = distance;
				best = i;
			}
		}
		return best;
	}

	void AnimatorWindow::FrameAll(const ImVec2& canvasSize) {
		glm::vec2 lo = glm::min(m_Controller.EntryPosition, m_Controller.AnyStatePosition);
		glm::vec2 hi = glm::max(m_Controller.EntryPosition, m_Controller.AnyStatePosition);
		for (const AnimatorState& s : m_Controller.States) {
			lo = glm::min(lo, s.Position);
			hi = glm::max(hi, s.Position);
		}
		const glm::vec2 center = (lo + hi) * 0.5f;
		m_Pan = ImVec2(canvasSize.x * 0.5f - center.x * m_Zoom, canvasSize.y * 0.5f - center.y * m_Zoom);
	}

	void AnimatorWindow::CreateState(const glm::vec2& position, uint64_t clip, const std::string& name) {
		BeginEdit();
		AnimatorState state;
		std::string base = name.empty() ? std::string("New State") : name;
		state.Name = base;
		for (int i = 1; m_Controller.FindState(state.Name) >= 0; ++i) state.Name = base + " " + std::to_string(i);
		state.Clip = clip;
		state.Position = position;
		m_Controller.States.push_back(std::move(state));
		if (m_Controller.States.size() == 1) m_Controller.DefaultState = 0;
		m_SelectedNode = static_cast<int>(m_Controller.States.size()) - 1;
		m_SelectedTransition = -1;
		CommitEdit();
	}

	void AnimatorWindow::DeleteSelection() {
		if (m_SelectedTransition >= 0 && m_SelectedTransition < static_cast<int>(m_Controller.Transitions.size())) {
			BeginEdit();
			m_Controller.Transitions.erase(m_Controller.Transitions.begin() + m_SelectedTransition);
			m_SelectedTransition = -1;
			CommitEdit();
		}
		else if (m_SelectedNode >= 0 && m_SelectedNode < static_cast<int>(m_Controller.States.size())) {
			BeginEdit();
			m_Controller.RemoveState(m_SelectedNode);
			m_SelectedNode = k_NoNode;
			CommitEdit();
		}
	}

	// ── Render ───────────────────────────────────────────────────────

	void AnimatorWindow::Render(bool* pOpen, Scene* scene, EntityHandle selected) {
		m_FocusedLastFrame = false;
		if (!pOpen || !*pOpen) return;
		EditorUI::NextWindowDefaultSize(0.6f, 1.9f);
		if (!ImGui::Begin("Animator", pOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			ImGui::End();
			return;
		}
		m_FocusedLastFrame = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		ResolveTarget(scene, selected);
		if (const std::optional<std::filesystem::path> path = m_NewControllerDialog.Poll()) CreateControllerAt(*path);
		SyncWorkingCopy();

		if (!m_Loaded) {
			if (m_ControllerGuid != 0) {
				EditorUI::EmptyState("Controller could not be read", "Check the .animctrl file for JSON errors.");
			}
			else if (scene && scene->GetRegistry().valid(m_Animator)) {
				EditorUI::EmptyState("No controller", "This Animator plays a single clip. A controller adds states and transitions.");
				const float width = ImGui::CalcTextSize("Create Controller...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
				ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);
				if (EditorUI::Button("Create Controller...", EditorUI::ButtonVariant::Primary)) RequestNewController();
			}
			else {
				EditorUI::EmptyState("No Animator Controller", "Select an entity with an Animator, or open an .animctrl from the Project panel.");
			}
			ImGui::End();
			return;
		}

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		m_ParameterWidth = std::clamp(m_ParameterWidth, 160.0f, std::max(160.0f, avail.x * 0.35f));
		m_DetailsWidth = std::clamp(m_DetailsWidth, 200.0f, std::max(200.0f, avail.x * 0.4f));
		const float graphWidth = std::max(avail.x - m_ParameterWidth - m_DetailsWidth - spacing * 2.0f, 100.0f);

		ImGui::BeginChild("##Parameters", ImVec2(m_ParameterWidth, avail.y), ImGuiChildFlags_Borders);
		RenderParameters(m_ParameterWidth, avail.y);
		ImGui::EndChild();
		ImGui::SameLine();
		const ImVec2 graphOrigin = ImGui::GetCursorScreenPos();
		RenderGraph(graphOrigin, ImVec2(graphWidth, avail.y));
		ImGui::SameLine();
		ImGui::BeginChild("##Details", ImVec2(m_DetailsWidth, avail.y), ImGuiChildFlags_Borders);
		RenderDetails(m_DetailsWidth, avail.y);
		ImGui::EndChild();

		HandleShortcuts();
		ImGui::End();
	}

	void AnimatorWindow::RenderParameters(float width, float height) {
		(void)height;
		AnimatorRuntime* live = LiveRuntime();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Parameters");
		ImGui::SameLine(width - ImGui::GetFrameHeight() - ImGui::GetStyle().WindowPadding.x * 2.0f);
		if (EditorUI::IconButton("##AddParameter", "plus", "Add parameter")) ImGui::OpenPopup("##AddParameterMenu");
		if (ImGui::BeginPopup("##AddParameterMenu")) {
			for (AnimatorParameterType type : { AnimatorParameterType::Float, AnimatorParameterType::Int, AnimatorParameterType::Bool, AnimatorParameterType::Trigger }) {
				if (ImGui::MenuItem(ParameterTypeLabel(type))) {
					BeginEdit();
					AnimatorParameter parameter;
					parameter.Type = type;
					std::string name = std::string("New ") + ParameterTypeLabel(type);
					parameter.Name = name;
					for (int i = 1; m_Controller.FindParameter(parameter.Name) >= 0; ++i) parameter.Name = name + " " + std::to_string(i);
					m_Controller.Parameters.push_back(std::move(parameter));
					CommitEdit();
				}
			}
			ImGui::EndPopup();
		}
		ImGui::Separator();
		if (m_Controller.Parameters.empty()) {
			ImGui::TextDisabled("No parameters yet.");
			ImGui::TextWrapped("Scripts set them with SetFloat, SetBool, SetTrigger ... to drive transitions.");
		}

		int removeIndex = -1;
		const float valueWidth = ImGui::GetFontSize() * 4.0f;
		for (size_t i = 0; i < m_Controller.Parameters.size(); ++i) {
			AnimatorParameter& p = m_Controller.Parameters[i];
			ImGui::PushID(static_cast<int>(i));
			char name[96];
			std::snprintf(name, sizeof(name), "%s", p.Name.c_str());
			ImGui::SetNextItemWidth(std::max(width - valueWidth - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().WindowPadding.x * 2.0f, 40.0f));
			ImGui::InputText("##name", name, sizeof(name));
			if (ImGui::IsItemDeactivatedAfterEdit() && p.Name != name && name[0] != '\0' && m_Controller.FindParameter(name) < 0) {
				BeginEdit();
				const std::string oldName = m_Controller.Parameters[i].Name;
				m_Controller.Parameters[i].Name = name;
				// Conditions follow the rename.
				for (AnimatorTransition& t : m_Controller.Transitions) {
					for (AnimatorCondition& c : t.Conditions) if (c.Parameter == oldName) c.Parameter = name;
				}
				CommitEdit();
			}
			EditorUI::Tooltip(ParameterTypeLabel(p.Type));
			if (ImGui::BeginPopupContextItem("##ParameterMenu")) {
				if (ImGui::MenuItem("Delete")) removeIndex = static_cast<int>(i);
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(valueWidth);
			if (live) {
				// Live values while playing; changes go to the running Animator only.
				AnimatorStateMachine& machine = live->GetStateMachine();
				switch (p.Type) {
				case AnimatorParameterType::Float: { float v = machine.GetFloat(p.Name); if (ImGui::DragFloat("##live", &v, 0.01f)) machine.SetFloat(p.Name, v); break; }
				case AnimatorParameterType::Int: { int v = machine.GetInt(p.Name); if (ImGui::DragInt("##live", &v)) machine.SetInt(p.Name, v); break; }
				case AnimatorParameterType::Bool: { bool v = machine.GetBool(p.Name); if (ImGuiUtils::ToggleSwitch("##live", &v)) machine.SetBool(p.Name, v); break; }
				case AnimatorParameterType::Trigger: {
					const bool set = machine.GetBool(p.Name);
					if (EditorUI::ToggleButton("Set", set)) {
						if (set) machine.ResetTrigger(p.Name);
						else machine.SetTrigger(p.Name);
					}
					break;
				}
				}
			}
			else {
				switch (p.Type) {
				case AnimatorParameterType::Float: {
					float v = p.DefaultFloat;
					if (ImGui::DragFloat("##default", &v, 0.01f)) { if (!m_DragEditStarted) { BeginEdit(); m_DragEditStarted = true; } m_Controller.Parameters[i].DefaultFloat = v; LiveEdit(); }
					if (ImGui::IsItemDeactivatedAfterEdit()) { CommitEdit(); m_DragEditStarted = false; }
					break;
				}
				case AnimatorParameterType::Int: {
					int v = p.DefaultInt;
					if (ImGui::DragInt("##default", &v)) { if (!m_DragEditStarted) { BeginEdit(); m_DragEditStarted = true; } m_Controller.Parameters[i].DefaultInt = v; LiveEdit(); }
					if (ImGui::IsItemDeactivatedAfterEdit()) { CommitEdit(); m_DragEditStarted = false; }
					break;
				}
				case AnimatorParameterType::Bool: {
					bool v = p.DefaultBool;
					if (ImGuiUtils::ToggleSwitch("##default", &v)) { BeginEdit(); m_Controller.Parameters[i].DefaultBool = v; CommitEdit(); }
					break;
				}
				case AnimatorParameterType::Trigger:
					ImGui::TextDisabled("Trigger");
					break;
				}
				EditorUI::Tooltip("Value when the Animator starts");
			}
			ImGui::PopID();
		}
		if (removeIndex >= 0) {
			BeginEdit();
			const std::string removed = m_Controller.Parameters[static_cast<size_t>(removeIndex)].Name;
			m_Controller.Parameters.erase(m_Controller.Parameters.begin() + removeIndex);
			for (AnimatorTransition& t : m_Controller.Transitions) {
				std::erase_if(t.Conditions, [&](const AnimatorCondition& c) { return c.Parameter == removed; });
			}
			CommitEdit();
		}
	}

	void AnimatorWindow::RenderGraph(const ImVec2& origin, const ImVec2& size) {
		m_CanvasOrigin = origin;
		if (m_FrameOnNextDraw) {
			FrameAll(size);
			m_FrameOnNextDraw = false;
		}
		ImGui::InvisibleButton("##Graph", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		// Clips dropped from the Project panel become states.
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_BROWSER_ITEM")) {
				const std::string path(static_cast<const char*>(payload->Data));
				const uint64_t guid = AssetRegistry::GetOrCreateAssetUUID(path);
				if (guid != 0 && AssetRegistry::GetKind(guid) == AssetKind::AnimationClip) {
					const ImVec2 dropMouse = ImGui::GetIO().MousePos;
					const ImVec2 local = Mul(Sub(Sub(dropMouse, origin), m_Pan), 1.0f / m_Zoom);
					CreateState({ local.x, local.y }, guid, std::filesystem::path(path).stem().string());
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImVec2 max = Add(origin, size);
		draw->AddRectFilled(origin, max, ImGui::GetColorU32(ImGuiCol_ChildBg));
		draw->PushClipRect(origin, max, true);

		// Grid.
		const float grid = 32.0f * m_Zoom;
		const ImU32 gridColor = ImGui::GetColorU32(ImGuiCol_Separator, 0.35f);
		for (float x = std::fmod(m_Pan.x, grid); x < size.x; x += grid) draw->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, max.y), gridColor);
		for (float y = std::fmod(m_Pan.y, grid); y < size.y; y += grid) draw->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(max.x, origin.y + y), gridColor);

		const ImVec2 nodeSize = NodeSize();
		const ImVec2 half = Mul(nodeSize, 0.5f);
		AnimatorRuntime* live = LiveRuntime();
		const int liveState = live ? live->GetStateMachine().GetCurrentState() : -1;
		const int liveNext = live ? live->GetStateMachine().GetNextState() : -1;
		const ImVec2 mouse = ImGui::GetIO().MousePos;

		// Line with an arrowhead in its middle.
		auto drawSegment = [&](const ImVec2& a, const ImVec2& b, ImU32 color, float thickness) {
			ImVec2 dir = Sub(b, a);
			const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
			if (len < 1.0f) return;
			dir = Mul(dir, 1.0f / len);
			const ImVec2 normal(-dir.y, dir.x);
			draw->AddLine(a, b, color, thickness);
			const ImVec2 mid = Mul(Add(a, b), 0.5f);
			const float s = ImGui::GetFontSize() * 0.4f * std::max(m_Zoom, 0.6f);
			draw->AddTriangleFilled(Add(mid, Mul(dir, s)), Add(Sub(mid, Mul(dir, s * 0.6f)), Mul(normal, s * 0.8f)),
				Sub(Sub(mid, Mul(dir, s * 0.6f)), Mul(normal, s * 0.8f)), color);
		};

		// Entry -> default state.
		if (!m_Controller.States.empty()) {
			const ImVec2 entry = Add(NodeScreenPos(k_EntryNode), half);
			const ImVec2 target = Add(NodeScreenPos(m_Controller.DefaultState), half);
			drawSegment(RectEdgePoint(Sub(entry, half), Add(entry, half), target), RectEdgePoint(Sub(target, half), Add(target, half), entry),
				WithAlpha(EditorTheme::Colors::Warning(), 0.8f), 2.0f);
		}

		// Transitions; a pair in both directions is drawn side by side.
		for (size_t i = 0; i < m_Controller.Transitions.size(); ++i) {
			const AnimatorTransition& t = m_Controller.Transitions[i];
			ImVec2 a, b;
			if (!TransitionSegment(static_cast<int>(i), a, b)) continue;
			const bool selected = static_cast<int>(i) == m_SelectedTransition;
			const bool running = live && live->GetStateMachine().IsInTransition() && t.To == liveNext && (t.From == liveState || t.From == k_AnyNode);
			const ImU32 color = selected || running ? ImGui::GetColorU32(EditorTheme::Colors::Accent()) : ImGui::GetColorU32(ImGuiCol_Text, 0.75f);
			drawSegment(a, b, color, selected ? 2.5f : 1.6f);
		}

		if (m_MakingTransitionFrom != k_NoNode) {
			const ImVec2 from = Add(NodeScreenPos(m_MakingTransitionFrom), half);
			drawSegment(RectEdgePoint(Sub(from, half), Add(from, half), mouse), mouse, ImGui::GetColorU32(EditorTheme::Colors::Accent()), 2.0f);
		}

		// Nodes.
		auto drawNode = [&](int node, const std::string& label, ImU32 fill, bool isDefault) {
			const ImVec2 min = NodeScreenPos(node);
			const ImVec2 nodeMax = Add(min, nodeSize);
			const float rounding = 6.0f * m_Zoom;
			draw->AddRectFilled(min, nodeMax, fill, rounding);
			const bool selected = node == m_SelectedNode;
			const bool isLive = node >= 0 && (node == liveState || node == liveNext);
			const ImU32 border = selected || isLive ? ImGui::GetColorU32(EditorTheme::Colors::Accent()) : ImGui::GetColorU32(ImGuiCol_Border);
			draw->AddRect(min, nodeMax, border, rounding, 0, selected || isLive ? 2.0f : 1.0f);
			if (isDefault) draw->AddRectFilled(min, ImVec2(min.x + 4.0f * m_Zoom, nodeMax.y), ImGui::GetColorU32(EditorTheme::Colors::Warning()), rounding, ImDrawFlags_RoundCornersLeft);
			const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
			draw->PushClipRect(min, nodeMax, true);
			draw->AddText(ImVec2(min.x + (nodeSize.x - textSize.x) * 0.5f, min.y + (nodeSize.y - textSize.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
			draw->PopClipRect();
			if (live && node >= 0 && (node == liveState || node == liveNext)) {
				// Playback progress along the bottom edge.
				const auto infos = live->GetClipInfos();
				float progress = 0.0f;
				if (node == liveState) {
					progress = live->GetStateMachine().GetCurrentNormalizedTime(infos);
					progress = progress - std::floor(progress);
				}
				else {
					progress = live->GetStateMachine().GetTransitionProgress();
				}
				const float barY = nodeMax.y - 4.0f * m_Zoom;
				draw->AddRectFilled(ImVec2(min.x + 4.0f, barY), ImVec2(min.x + 4.0f + (nodeSize.x - 8.0f) * progress, nodeMax.y - 1.5f),
					ImGui::GetColorU32(EditorTheme::Colors::Accent()));
			}
		};
		const ImVec4 surface = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
		drawNode(k_EntryNode, "Entry", WithAlpha(EditorTheme::Colors::Success(), 0.35f), false);
		drawNode(k_AnyNode, "Any State", WithAlpha(EditorTheme::Colors::Info(), 0.35f), false);
		for (size_t i = 0; i < m_Controller.States.size(); ++i) {
			const AnimatorState& state = m_Controller.States[i];
			drawNode(static_cast<int>(i), state.Name, ImGui::GetColorU32(surface), static_cast<int>(i) == m_Controller.DefaultState);
		}

		if (m_Controller.States.empty()) {
			const char* hint = "Right-click to create a state, or drop an .anim clip here";
			const ImVec2 textSize = ImGui::CalcTextSize(hint);
			draw->AddText(ImVec2(origin.x + (size.x - textSize.x) * 0.5f, origin.y + size.y * 0.75f), ImGui::GetColorU32(ImGuiCol_TextDisabled), hint);
		}
		draw->AddText(ImVec2(origin.x + 8.0f, origin.y + 6.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
			(m_ControllerName + (live ? "  -  live" : "")).c_str());
		draw->PopClipRect();

		ImGuiIO& io = ImGui::GetIO();
		if (hovered && io.MouseWheel != 0.0f) {
			const ImVec2 local = Mul(Sub(Sub(mouse, origin), m_Pan), 1.0f / m_Zoom);
			m_Zoom = std::clamp(m_Zoom * std::pow(1.1f, io.MouseWheel), 0.4f, 2.0f);
			m_Pan = Sub(Sub(mouse, origin), Mul(ImVec2(local.x, local.y), m_Zoom));
		}

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			const int node = HitNode(mouse);
			if (m_MakingTransitionFrom != k_NoNode) {
				if (node >= 0 && node != m_MakingTransitionFrom) {
					BeginEdit();
					AnimatorTransition t;
					t.From = m_MakingTransitionFrom;
					t.To = node;
					// Unity's defaults: state to state plays out before blending; Any State waits for conditions.
					t.HasExitTime = m_MakingTransitionFrom != k_AnyNode;
					t.ExitTime = 0.75f;
					t.Duration = 0.25f;
					m_Controller.Transitions.push_back(t);
					m_SelectedTransition = static_cast<int>(m_Controller.Transitions.size()) - 1;
					m_SelectedNode = k_NoNode;
					CommitEdit();
				}
				m_MakingTransitionFrom = k_NoNode;
			}
			else if (node != k_NoNode) {
				m_SelectedNode = node;
				m_SelectedTransition = -1;
				m_DraggingNode = node;
				m_DragEditStarted = false;
			}
			else {
				m_SelectedTransition = HitTransition(mouse);
				m_SelectedNode = k_NoNode;
				if (m_SelectedTransition < 0) m_Panning = true;
			}
		}
		if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			const int node = HitNode(mouse);
			if (node >= 0 && m_Controller.States[static_cast<size_t>(node)].Clip != 0) m_OpenClipRequest = m_Controller.States[static_cast<size_t>(node)].Clip;
		}
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) m_Panning = true;
		if (m_Panning) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) m_Pan = Add(m_Pan, io.MouseDelta);
			else m_Panning = false;
		}
		if (m_DraggingNode != k_NoNode) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
					if (!m_DragEditStarted) { BeginEdit(); m_DragEditStarted = true; }
					SetNodePosition(m_DraggingNode, NodePosition(m_DraggingNode) + glm::vec2(io.MouseDelta.x, io.MouseDelta.y) / m_Zoom);
				}
			}
			else {
				if (m_DragEditStarted) CommitEdit();
				m_DraggingNode = k_NoNode;
				m_DragEditStarted = false;
			}
		}
		(void)active;

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			m_MakingTransitionFrom = k_NoNode;
			m_ContextPosition = Mul(Sub(Sub(mouse, origin), m_Pan), 1.0f / m_Zoom);
			m_ContextNode = HitNode(mouse);
			m_ContextTransition = m_ContextNode == k_NoNode ? HitTransition(mouse) : -1;
			if (m_ContextNode != k_NoNode) { m_SelectedNode = m_ContextNode; m_SelectedTransition = -1; }
			else if (m_ContextTransition >= 0) { m_SelectedTransition = m_ContextTransition; m_SelectedNode = k_NoNode; }
			ImGui::OpenPopup("##GraphMenu");
		}
		if (ImGui::BeginPopup("##GraphMenu")) {
			if (m_ContextNode >= 0 || m_ContextNode == k_AnyNode) {
				if (ImGui::MenuItem("Make Transition")) m_MakingTransitionFrom = m_ContextNode;
			}
			if (m_ContextNode >= 0) {
				if (ImGui::MenuItem("Set as Default State", nullptr, false, m_ContextNode != m_Controller.DefaultState)) {
					BeginEdit();
					m_Controller.DefaultState = m_ContextNode;
					CommitEdit();
				}
				const uint64_t clip = m_Controller.States[static_cast<size_t>(m_ContextNode)].Clip;
				if (ImGui::MenuItem("Edit Clip", nullptr, false, clip != 0)) m_OpenClipRequest = clip;
				ImGui::Separator();
				if (ImGui::MenuItem("Delete State", "Del")) DeleteSelection();
			}
			else if (m_ContextTransition >= 0) {
				if (ImGui::MenuItem("Delete Transition", "Del")) DeleteSelection();
			}
			else if (m_ContextNode == k_NoNode) {
				if (ImGui::MenuItem("Create State")) CreateState({ m_ContextPosition.x, m_ContextPosition.y }, 0, "New State");
				if (ImGui::MenuItem("Frame All", "F")) FrameAll(size);
			}
			ImGui::EndPopup();
		}
	}

	void AnimatorWindow::RenderDetails(float width, float height) {
		(void)width;
		(void)height;
		if (m_SelectedTransition >= 0 && m_SelectedTransition < static_cast<int>(m_Controller.Transitions.size())) {
			RenderTransitionDetails(m_SelectedTransition);
		}
		else if (m_SelectedNode >= 0 && m_SelectedNode < static_cast<int>(m_Controller.States.size())) {
			RenderStateDetails(m_SelectedNode);
		}
		else if (m_SelectedNode == k_AnyNode) {
			ImGui::TextUnformatted("Any State");
			ImGui::TextWrapped("Transitions from here can fire from every state, before the current state's own.");
		}
		else if (m_SelectedNode == k_EntryNode) {
			ImGui::TextUnformatted("Entry");
			ImGui::TextWrapped("The Animator starts in the default state (orange arrow). Right-click a state to change it.");
		}
		else {
			EditorUI::EmptyState("Nothing selected", "Select a state or a transition.");
		}
	}

	void AnimatorWindow::RenderStateDetails(int stateIndex) {
		AnimatorState& state = m_Controller.States[static_cast<size_t>(stateIndex)];
		ImGui::PushID(stateIndex);
		ImGui::TextDisabled(stateIndex == m_Controller.DefaultState ? "State (default)" : "State");

		char name[96];
		std::snprintf(name, sizeof(name), "%s", state.Name.c_str());
		ImGuiUtils::BeginInspectorFieldRow("Name");
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputText("##Name", name, sizeof(name));
		if (ImGui::IsItemDeactivatedAfterEdit() && state.Name != name && name[0] != '\0' && m_Controller.FindState(name) < 0) {
			BeginEdit();
			m_Controller.States[static_cast<size_t>(stateIndex)].Name = name;
			CommitEdit();
		}

		const int index = stateIndex;
		AnimatorWindow* self = this;
		PropertyDescriptor clip;
		clip.Name = "Clip";
		clip.DisplayName = "Clip";
		clip.Type = PropertyType::AssetRef;
		clip.Metadata.AssetKindFilter = AssetKind::AnimationClip;
		clip.Get = [self, index](const Entity&) -> PropertyValue {
			PropertyValue v;
			v.Type = PropertyType::AssetRef;
			v.UIntValue = index < static_cast<int>(self->m_Controller.States.size()) ? self->m_Controller.States[static_cast<size_t>(index)].Clip : 0;
			return v;
		};
		clip.Set = [self, index](Entity&, const PropertyValue& v) {
			if (index >= static_cast<int>(self->m_Controller.States.size())) return;
			self->BeginEdit();
			AnimationClipLibrary::Invalidate(v.UIntValue);
			self->m_Controller.States[static_cast<size_t>(index)].Clip = v.UIntValue;
			self->CommitEdit();
		};
		const Entity placeholder = Entity::Null;
		PropertyDrawer::Draw(std::span<const Entity>(&placeholder, 1), clip, "animctrl:" + std::to_string(m_ControllerGuid) + ".state" + std::to_string(stateIndex) + ".Clip");

		ImGuiUtils::BeginInspectorFieldRow("Speed");
		ImGui::SetNextItemWidth(-1.0f);
		float speed = state.Speed;
		if (ImGui::DragFloat("##Speed", &speed, 0.01f, -10.0f, 10.0f, "%.2f")) {
			if (!m_DragEditStarted) { BeginEdit(); m_DragEditStarted = true; }
			m_Controller.States[static_cast<size_t>(stateIndex)].Speed = speed;
			LiveEdit();
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) { CommitEdit(); m_DragEditStarted = false; }

		ImGuiUtils::BeginInspectorFieldRow("Loop");
		ImGui::SetNextItemWidth(-1.0f);
		static const char* k_LoopLabels[] = { "From Clip", "Loop", "Once" };
		int loop = static_cast<int>(state.Loop);
		if (ImGui::Combo("##Loop", &loop, k_LoopLabels, 3)) {
			BeginEdit();
			m_Controller.States[static_cast<size_t>(stateIndex)].Loop = static_cast<AnimatorStateLoop>(loop);
			CommitEdit();
		}

		if (stateIndex != m_Controller.DefaultState && EditorUI::Button("Set as Default State")) {
			BeginEdit();
			m_Controller.DefaultState = stateIndex;
			CommitEdit();
		}
		if (state.Clip != 0) {
			ImGui::SameLine();
			if (EditorUI::Button("Edit Clip")) m_OpenClipRequest = state.Clip;
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Transitions");
		ImGui::Separator();
		bool any = false;
		for (size_t i = 0; i < m_Controller.Transitions.size(); ++i) {
			const AnimatorTransition& t = m_Controller.Transitions[i];
			if (t.From != stateIndex) continue;
			any = true;
			const std::string label = "-> " + m_Controller.States[static_cast<size_t>(t.To)].Name + "##t" + std::to_string(i);
			if (ImGui::Selectable(label.c_str())) {
				m_SelectedTransition = static_cast<int>(i);
				m_SelectedNode = k_NoNode;
			}
		}
		if (!any) ImGui::TextDisabled("None. Right-click the state > Make Transition.");
		ImGui::PopID();
	}

	void AnimatorWindow::RenderTransitionDetails(int transitionIndex) {
		AnimatorTransition& t = m_Controller.Transitions[static_cast<size_t>(transitionIndex)];
		ImGui::PushID(transitionIndex + 10000);
		const std::string from = t.From == k_AnyNode ? std::string("Any State") : m_Controller.States[static_cast<size_t>(t.From)].Name;
		const std::string to = m_Controller.States[static_cast<size_t>(t.To)].Name;
		ImGui::TextDisabled("Transition");
		ImGui::TextUnformatted((from + "  ->  " + to).c_str());
		ImGui::Separator();

		bool hasExit = t.HasExitTime;
		if (ImGuiUtils::Switch("Has Exit Time", &hasExit)) {
			BeginEdit();
			m_Controller.Transitions[static_cast<size_t>(transitionIndex)].HasExitTime = hasExit;
			CommitEdit();
		}
		EditorUI::Tooltip("Waits until the source state reaches Exit Time (in clip lengths) before checking the conditions");
		ImGui::BeginDisabled(!t.HasExitTime);
		ImGuiUtils::BeginInspectorFieldRow("Exit Time");
		ImGui::SetNextItemWidth(-1.0f);
		float exitTime = t.ExitTime;
		if (ImGui::DragFloat("##ExitTime", &exitTime, 0.01f, 0.0f, 100.0f, "%.2f")) {
			if (!m_DragEditStarted) { BeginEdit(); m_DragEditStarted = true; }
			m_Controller.Transitions[static_cast<size_t>(transitionIndex)].ExitTime = std::max(exitTime, 0.0f);
			LiveEdit();
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) { CommitEdit(); m_DragEditStarted = false; }
		ImGui::EndDisabled();

		ImGuiUtils::BeginInspectorFieldRow("Duration");
		ImGui::SetNextItemWidth(-1.0f);
		float duration = t.Duration;
		if (ImGui::DragFloat("##Duration", &duration, 0.01f, 0.0f, 10.0f, "%.2f s")) {
			if (!m_DragEditStarted) { BeginEdit(); m_DragEditStarted = true; }
			m_Controller.Transitions[static_cast<size_t>(transitionIndex)].Duration = std::max(duration, 0.0f);
			LiveEdit();
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) { CommitEdit(); m_DragEditStarted = false; }
		EditorUI::Tooltip("Crossfade length in seconds");

		if (t.From == k_AnyNode) {
			bool toSelf = t.CanTransitionToSelf;
			if (ImGuiUtils::Switch("Can Transition To Self", &toSelf)) {
				BeginEdit();
				m_Controller.Transitions[static_cast<size_t>(transitionIndex)].CanTransitionToSelf = toSelf;
				CommitEdit();
			}
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Conditions");
		ImGui::Separator();
		if (t.Conditions.empty()) {
			ImGui::TextWrapped(t.HasExitTime ? "None: the transition fires at Exit Time." : "None: the transition fires immediately.");
		}
		int removeCondition = -1;
		for (size_t c = 0; c < t.Conditions.size(); ++c) {
			AnimatorCondition& condition = m_Controller.Transitions[static_cast<size_t>(transitionIndex)].Conditions[c];
			ImGui::PushID(static_cast<int>(c));
			const int parameterIndex = m_Controller.FindParameter(condition.Parameter);
			const AnimatorParameterType type = parameterIndex >= 0 ? m_Controller.Parameters[static_cast<size_t>(parameterIndex)].Type : AnimatorParameterType::Float;
			const float rowWidth = ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x;

			ImGui::SetNextItemWidth(rowWidth * 0.45f);
			const std::string preview = parameterIndex >= 0 ? condition.Parameter : (condition.Parameter.empty() ? std::string("Parameter") : condition.Parameter + " (missing)");
			if (ImGui::BeginCombo("##Parameter", preview.c_str())) {
				for (const AnimatorParameter& p : m_Controller.Parameters) {
					if (ImGui::Selectable(p.Name.c_str(), p.Name == condition.Parameter)) {
						BeginEdit();
						AnimatorCondition& edited = m_Controller.Transitions[static_cast<size_t>(transitionIndex)].Conditions[c];
						edited.Parameter = p.Name;
						const bool boolLike = p.Type == AnimatorParameterType::Bool || p.Type == AnimatorParameterType::Trigger;
						if (boolLike && edited.Mode != AnimatorConditionMode::If && edited.Mode != AnimatorConditionMode::IfNot) edited.Mode = AnimatorConditionMode::If;
						if (!boolLike && (edited.Mode == AnimatorConditionMode::If || edited.Mode == AnimatorConditionMode::IfNot)) edited.Mode = AnimatorConditionMode::Greater;
						CommitEdit();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();

			std::vector<AnimatorConditionMode> modes;
			if (type == AnimatorParameterType::Trigger) modes = { AnimatorConditionMode::If };
			else if (type == AnimatorParameterType::Bool) modes = { AnimatorConditionMode::If, AnimatorConditionMode::IfNot };
			else if (type == AnimatorParameterType::Int) modes = { AnimatorConditionMode::Greater, AnimatorConditionMode::Less, AnimatorConditionMode::Equals, AnimatorConditionMode::NotEquals };
			else modes = { AnimatorConditionMode::Greater, AnimatorConditionMode::Less };
			const bool needsValue = type == AnimatorParameterType::Float || type == AnimatorParameterType::Int;
			ImGui::SetNextItemWidth(needsValue ? rowWidth * 0.27f : rowWidth * 0.55f - ImGui::GetStyle().ItemSpacing.x);
			if (ImGui::BeginCombo("##Mode", type == AnimatorParameterType::Trigger ? "Set" : ConditionModeLabel(condition.Mode))) {
				for (AnimatorConditionMode mode : modes) {
					if (ImGui::Selectable(type == AnimatorParameterType::Trigger ? "Set" : ConditionModeLabel(mode), mode == condition.Mode)) {
						BeginEdit();
						m_Controller.Transitions[static_cast<size_t>(transitionIndex)].Conditions[c].Mode = mode;
						CommitEdit();
					}
				}
				ImGui::EndCombo();
			}
			if (needsValue) {
				ImGui::SameLine();
				ImGui::SetNextItemWidth(rowWidth * 0.28f - ImGui::GetStyle().ItemSpacing.x);
				float threshold = condition.Threshold;
				const bool changed = type == AnimatorParameterType::Int
					? [&]() { int v = static_cast<int>(threshold); const bool r = ImGui::DragInt("##Threshold", &v); threshold = static_cast<float>(v); return r; }()
					: ImGui::DragFloat("##Threshold", &threshold, 0.01f, 0.0f, 0.0f, "%.2f");
				if (changed) {
					if (!m_DragEditStarted) { BeginEdit(); m_DragEditStarted = true; }
					m_Controller.Transitions[static_cast<size_t>(transitionIndex)].Conditions[c].Threshold = threshold;
					LiveEdit();
				}
				if (ImGui::IsItemDeactivatedAfterEdit()) { CommitEdit(); m_DragEditStarted = false; }
			}
			ImGui::SameLine();
			if (EditorUI::IconButton("##Remove", "minus", "Remove condition")) removeCondition = static_cast<int>(c);
			ImGui::PopID();
		}
		if (removeCondition >= 0) {
			BeginEdit();
			auto& conditions = m_Controller.Transitions[static_cast<size_t>(transitionIndex)].Conditions;
			conditions.erase(conditions.begin() + removeCondition);
			CommitEdit();
		}
		ImGui::BeginDisabled(m_Controller.Parameters.empty());
		if (EditorUI::Button("Add Condition")) {
			BeginEdit();
			AnimatorCondition condition;
			const AnimatorParameter& p = m_Controller.Parameters.front();
			condition.Parameter = p.Name;
			condition.Mode = p.Type == AnimatorParameterType::Bool || p.Type == AnimatorParameterType::Trigger ? AnimatorConditionMode::If : AnimatorConditionMode::Greater;
			m_Controller.Transitions[static_cast<size_t>(transitionIndex)].Conditions.push_back(condition);
			CommitEdit();
		}
		ImGui::EndDisabled();
		if (m_Controller.Parameters.empty()) EditorUI::Tooltip("Add a parameter first");
		ImGui::Spacing();
		if (EditorUI::Button("Delete Transition", EditorUI::ButtonVariant::Danger)) DeleteSelection();
		ImGui::PopID();
	}

	void AnimatorWindow::HandleShortcuts() {
		if (!m_FocusedLastFrame || ImGui::GetIO().WantTextInput) return;
		const bool ctrl = ImGui::GetIO().KeyCtrl;
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
			if (ImGui::GetIO().KeyShift) Redo();
			else Undo();
			return;
		}
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_MakingTransitionFrom = k_NoNode;
		if (!ctrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) m_FrameOnNextDraw = true;
		if (!ctrl && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) DeleteSelection();
	}

}
