#pragma once

#include "Gui/EditorTheme.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Funke::ReorderUI {

	// Opt-in affordance for ordered lists and component headers, not generic asset/reference drags.
	inline void DragHandle(const char* id, const ImVec2& size) {
		ImGui::InvisibleButton(id, size);
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		const ImVec2 min = ImGui::GetItemRectMin();
		const ImVec2 center(min.x + size.x * 0.5f, min.y + size.y * 0.5f);
		const float radius = std::max(1.25f, ImGui::GetFontSize() * 0.09f);
		const float offset = radius * 2.0f;
		const ImU32 color = ImGui::GetColorU32(active || hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
		for (int y : { -1, 1 }) {
			for (int x : { -1, 1 }) {
				ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(center.x + x * offset, center.y + y * offset), radius, color);
			}
		}
		if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		if (hovered && !active) ImGui::SetTooltip("Drag to reorder");
	}

	inline void DrawInsertionMarker(float left, float right, float y) {
		const float scale = ImGui::GetFontSize() / 13.0f;
		const float thickness = std::max(3.0f, std::round(3.0f * scale));
		const float radius = thickness;
		left += radius;
		right -= radius;
		if (right <= left) return;
		ImDrawList* draw = ImGui::GetWindowDrawList();
		const ImU32 color = ImGui::GetColorU32(EditorTheme::Colors::Highlight());
		ImVec4 halo = EditorTheme::Colors::Accent();
		halo.w = 0.28f;
		draw->AddRectFilled(ImVec2(left - radius, y - thickness * 1.5f), ImVec2(right + radius, y + thickness * 1.5f),
			ImGui::GetColorU32(halo), radius);
		// Opaque outline keeps the insertion point legible over both dark and light cards.
		draw->AddLine(ImVec2(left, y), ImVec2(right, y), ImGui::GetColorU32(ImGuiCol_WindowBg), thickness + 2.0f * scale);
		draw->AddLine(ImVec2(left, y), ImVec2(right, y), color, thickness);
		draw->AddCircleFilled(ImVec2(left, y), radius, color);
		draw->AddCircleFilled(ImVec2(right, y), radius, color);
	}

	// A visible gap is an insertion slot [0, count]; erasing the source shifts later slots left.
	inline int DestinationIndex(int source, int slot, int count) {
		if (source < 0 || source >= count || slot < 0 || slot > count) return -1;
		return slot - (source < slot ? 1 : 0);
	}
}
