#pragma once

#include <imgui.h>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Funke::EditorDockLayout {

	using WindowVisibility = std::map<std::string, bool>;

	// ImGui persists tab groups/order/selection, but the editor owns whether each tool is open.
	// Install before loading the ini; attach the editor callbacks once its panels are initialized.
	class WindowSettings {
	public:
		void Install();
		static WindowSettings* Get();
		void SetCallbacks(std::function<WindowVisibility()> capture, std::function<void(const WindowVisibility&)> apply);
		void ClearCallbacks();

	private:
		struct TabGroup {
			ImGuiID Selected = 0;
			std::map<ImGuiID, short> Order;
		};
		void RestoreTabs();
		std::map<ImGuiID, TabGroup> m_PendingTabs;
		WindowVisibility m_Saved;
		bool m_HasSaved = false;
		std::function<WindowVisibility()> m_Capture;
		std::function<void(const WindowVisibility&)> m_Apply;
	};

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
