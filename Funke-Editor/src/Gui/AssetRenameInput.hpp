#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>

namespace Funke {
	class AssetRenameInput {
	public:
		enum class Result { Editing, Commit, Cancel };

		void Reset() { m_HasBeenActive = false; }

		Result Render(const char* label, char* buffer, std::size_t capacity) {
			const ImGuiID id = ImGui::GetID(label);
			ImGuiContext& context = *ImGui::GetCurrentContext();
			const bool popupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
			const ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
			ImGuiViewport* viewport = ImGui::GetWindowViewport();
			const bool platformFocused = !platform.Platform_GetWindowFocus
				|| (viewport->PlatformWindowCreated && platform.Platform_GetWindowFocus(viewport));
			const bool waitingForFocus = !m_HasBeenActive && (popupOpen || !platformFocused);
			if (waitingForFocus) {
				// A focus request from the preceding frame must not activate on a closing native popup.
				if (context.NavActivateId == id) context.NavActivateId = 0;
				if (context.NavActivateDownId == id) context.NavActivateDownId = 0;
				if (context.NavActivatePressedId == id) context.NavActivatePressedId = 0;
				if (context.InputTextReactivateId == id) context.InputTextReactivateId = 0;
				if (context.ActiveId == id) ImGui::ClearActiveID();
			}
			// macOS must return native focus from the closing popup before starting text input.
			else if (!m_HasBeenActive
				&& context.ActiveId != id && context.NavActivateId != id) {
				ImGui::SetKeyboardFocusHere();
			}

			ImGui::BeginDisabled(waitingForFocus);
			const bool committed = ImGui::InputText(label, buffer, capacity,
				ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
			ImGui::EndDisabled();
			if (!popupOpen && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) return Result::Cancel;
			if (committed) return Result::Commit;
			if (ImGui::IsItemActive()) {
				m_HasBeenActive = true;
			}
			else if (m_HasBeenActive && ImGui::IsItemDeactivated()) {
				return Result::Commit;
			}
			return Result::Editing;
		}

	private:
		bool m_HasBeenActive = false;
	};
}
