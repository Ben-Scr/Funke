#pragma once

#include "Gui/ReorderUI.hpp"
#include <span>

namespace Funke::HierarchyDropUI {

	enum class Placement { Before, After, Child, Root };

	inline Placement RowPlacement(float mouseY, float top, float bottom, bool rootOnlyChild = false) {
		const float edge = (bottom - top) * 0.25f;
		if (!rootOnlyChild && mouseY < top + edge) return Placement::Before;
		if (!rootOnlyChild && mouseY > bottom - edge) return Placement::After;
		return Placement::Child;
	}

	// The next sibling follows the entire expanded subtree, not just the parent row.
	// A new child is appended at the same boundary, one indentation level deeper.
	inline int InsertionSlot(std::span<const int> depths, int row, Placement placement) {
		if (row < 0 || row >= static_cast<int>(depths.size())) return -1;
		if (placement == Placement::Before) return row;
		int slot = row + 1;
		while (slot < static_cast<int>(depths.size()) && depths[slot] > depths[row]) ++slot;
		return slot;
	}

	struct Preview {
		bool Active = false;
		bool Allowed = false;
		bool ShowInsertion = false;
		Placement Mode = Placement::Child;
		ImVec2 RowMin{}, RowMax{};
		float Left = 0.0f, Right = 0.0f, Y = 0.0f;

		// Draw after every row so subsequent row backgrounds cannot cover the marker.
		// This is purely visual: hit rectangles and the virtualized list stay stable.
		void Draw() const {
			if (!Active) return;
			if (!Allowed) {
				ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
				return;
			}
			const float scale = ImGui::GetFontSize() / 13.0f;
			ImDrawList* draw = ImGui::GetWindowDrawList();
			if (Mode == Placement::Child || Mode == Placement::Root || !ShowInsertion) {
				ImVec4 tint = EditorTheme::Colors::Accent();
				tint.w = 0.18f;
				draw->AddRectFilled(RowMin, RowMax, ImGui::GetColorU32(tint), ImGui::GetStyle().FrameRounding);
				draw->AddRect(RowMin, RowMax, ImGui::GetColorU32(EditorTheme::Colors::Highlight()),
					ImGui::GetStyle().FrameRounding, 0, 2.0f * scale);
			}
			if (ShowInsertion) ReorderUI::DrawInsertionMarker(Left, Right, Y);
		}
	};
}
