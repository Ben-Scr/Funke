#pragma once

#include "Gui/EditorWindowIcons.hpp"

#include <imgui_internal.h>

namespace Funke::EditorMenu {

	using Icon = EditorWindowIcons::Type;

	namespace Detail {
		struct IconSlot {
			ImGuiWindow* Window;
			ImVec2 Pos;
			float Size;
			ImU32 Color;
			bool Visible;
		};

		inline IconSlot Reserve(bool enabled) {
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			const float size = ImGui::GetFontSize();
			const bool visible = !window->SkipItems && window->DC.LayoutType == ImGuiLayoutType_Vertical;
			if (visible) window->DC.MenuColumns.DeclColumns(size, 0, 0, 0);
			ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			if (!enabled && !(GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled)) color.w *= ImGui::GetStyle().DisabledAlpha;
			return { window, ImVec2(window->DC.CursorPos.x + window->DC.MenuColumns.OffsetIcon,
				window->DC.CursorPos.y + window->DC.CurrLineTextBaseOffset), size, ImGui::GetColorU32(color), visible };
		}

		inline void Draw(const IconSlot& slot, Icon icon) {
			if (slot.Visible) EditorWindowIcons::DrawIcon(icon, slot.Window->DrawList, slot.Pos, slot.Size, slot.Color);
		}
	}

	inline bool Item(Icon icon, const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true) {
		const auto slot = Detail::Reserve(enabled);
		const bool pressed = ImGui::MenuItem(label, shortcut, selected, enabled);
		Detail::Draw(slot, icon);
		return pressed;
	}

	inline bool Item(Icon icon, const char* label, const char* shortcut, bool* selected, bool enabled = true) {
		const auto slot = Detail::Reserve(enabled);
		const bool pressed = ImGui::MenuItem(label, shortcut, selected, enabled);
		Detail::Draw(slot, icon);
		return pressed;
	}

	// BeginMenu may switch to a child popup; keep drawing in the parent row.
	inline bool Begin(Icon icon, const char* label, bool enabled = true) {
		const auto slot = Detail::Reserve(enabled);
		const bool alreadySubmitted = GImGui->MenusIdSubmittedThisFrame.contains(slot.Window->GetID(label));
		const bool open = ImGui::BeginMenu(label, enabled);
		if (!alreadySubmitted) Detail::Draw(slot, icon);
		return open;
	}
}
