#include "Gui/BuildProgressOverlay.hpp"

#include "Core/Platform/Win32BuildProgressWindow.hpp"

#include <imgui.h>

#include <algorithm>

namespace Index::EditorRuntime {

	void RenderBuildProgressOverlay() {
#ifndef IDX_PLATFORM_WINDOWS
		const Win32BuildProgressWindow::State state = Win32BuildProgressWindow::GetState();
		if (!state.Visible) return;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float width = std::min(ImGui::GetFontSize() * 26.0f, viewport->WorkSize.x - 32.0f);
		ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
			viewport->WorkPos.y + viewport->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowFocus();
		constexpr ImGuiWindowFlags k_Flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_AlwaysAutoResize;
		// ### keeps one window identity while the title changes between build stages.
		const std::string windowName = state.Title + "###IndexBuildProgress";
		if (ImGui::Begin(windowName.c_str(), nullptr, k_Flags)) {
			if (!state.Stage.empty()) ImGui::TextUnformatted(state.Stage.c_str());
			ImGui::ProgressBar(std::clamp(state.Progress, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f));
		}
		ImGui::End();
#endif
	}

}
