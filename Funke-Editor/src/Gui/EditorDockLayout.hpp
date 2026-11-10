#pragma once

#include <imgui.h>
#include <string_view>
#include <vector>

namespace Funke::EditorDockLayout {

	struct PreviewPane {
		ImVec2 Position;
		ImVec2 Size;
		bool Central = false;
	};

	struct Preview {
		ImVec2 Size;
		// Pane coordinates are relative to the main dockspace, in the range 0..1.
		std::vector<PreviewPane> Panes;
	};

	Preview ParsePreview(std::string_view settings);

	// True when a main panel (Entities, Inspector, views, Project, Log) is docked inside dockspaceId, live or in saved
	// settings. False on a fresh install or when the saved layout belongs to another dockspace ID.
	bool HasDockedPanels(ImGuiID dockspaceId);

	// Replaces the dockspace tree with the default arrangement. Call inside a frame, before ImGui::DockSpace(dockspaceId).
	void BuildDefault(ImGuiID dockspaceId, ImVec2 size);

}
