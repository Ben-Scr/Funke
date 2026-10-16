#pragma once

#include <imgui.h>

namespace Funke::EditorDockLayout {

	// True when a main panel (Entities, Inspector, views, Project, Log) is docked inside dockspaceId, live or in saved
	// settings. False on a fresh install or when the saved layout belongs to another dockspace ID.
	bool HasDockedPanels(ImGuiID dockspaceId);

	// Replaces the dockspace tree with the default arrangement. Call inside a frame, before ImGui::DockSpace(dockspaceId).
	void BuildDefault(ImGuiID dockspaceId, ImVec2 size);

}
