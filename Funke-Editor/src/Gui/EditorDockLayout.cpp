#include "Gui/EditorDockLayout.hpp"

#include <imgui_internal.h>

#include <algorithm>

namespace Funke::EditorDockLayout {

	namespace {
		constexpr const char* k_MainPanels[] = { "Entities", "Inspector", "Editor View", "Game View", "Project", "Log" };

		ImGuiID GetDockId(const char* windowName) {
			if (const ImGuiWindow* window = ImGui::FindWindowByName(windowName)) {
				return window->DockNode != nullptr ? window->DockNode->ID : window->DockId;
			}
			const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(windowName));
			return settings != nullptr ? settings->DockId : 0;
		}

		// Extents in font heights so the panels follow DPI and editor zoom; clamped so a small window keeps its centre.
		float SplitRatio(float extentEm, float available, float minRatio, float maxRatio) {
			if (available <= 0.0f) return minRatio;
			return std::clamp(ImGui::GetFontSize() * extentEm / available, minRatio, maxRatio);
		}
	}

	bool HasDockedPanels(ImGuiID dockspaceId) {
		for (const char* name : k_MainPanels) {
			const ImGuiID dockId = GetDockId(name);
			if (dockId == 0) continue;
			if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockId); node != nullptr && ImGui::DockNodeGetRootNode(node)->ID == dockspaceId) {
				return true;
			}
		}
		return false;
	}

	void BuildDefault(ImGuiID dockspaceId, ImVec2 size) {
		ImGui::DockBuilderRemoveNode(dockspaceId);
		ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspaceId, size);

		const float leftRatio = SplitRatio(25.0f, size.x, 0.12f, 0.3f);
		const float rightRatio = SplitRatio(26.0f, size.x * (1.0f - leftRatio), 0.12f, 0.3f);
		const float bottomRatio = SplitRatio(20.0f, size.y, 0.2f, 0.45f);

		// Each split hands the central node to the opposite side, so the views end up central.
		ImGuiID rest = 0;
		ImGuiID centre = 0;
		ImGuiID views = 0;
		ImGuiID log = 0;
		const ImGuiID left = ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, leftRatio, nullptr, &rest);
		const ImGuiID right = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, rightRatio, nullptr, &centre);
		const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, bottomRatio, nullptr, &views);
		const ImGuiID project = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.5f, nullptr, &log);

		ImGui::DockBuilderDockWindow("Entities", left);
		ImGui::DockBuilderDockWindow("Inspector", right);
		ImGui::DockBuilderDockWindow("Editor View", views);
		ImGui::DockBuilderDockWindow("Game View", views);
		ImGui::DockBuilderDockWindow("Animator", views);
		ImGui::DockBuilderDockWindow("Project", project);
		ImGui::DockBuilderDockWindow("Animation", project);
		ImGui::DockBuilderDockWindow("Log", log);
		ImGui::DockBuilderFinish(dockspaceId);
	}

}
