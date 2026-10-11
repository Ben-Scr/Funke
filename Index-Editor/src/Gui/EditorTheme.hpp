#pragma once
#include <imgui.h>

namespace Index::EditorTheme {

	// Brand colors from the Index pixel-diamond; functions (not constants) so they follow dark/light mode.
	inline bool IsDark() {
		const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		return (0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z) < 0.5f;
	}

	namespace Colors {

		// Interactive accent (selection, active toggles, primary buttons). Blue reads as "active" and stays
		// clearly apart from the warning/error tones; the warm logo colours are kept for branding only.
		inline ImVec4 AccentFor(bool dark) {
			return dark ? ImVec4(0.357f, 0.553f, 0.937f, 1.00f) : ImVec4(0.184f, 0.420f, 0.859f, 1.00f);
		}
		inline ImVec4 Accent() { return AccentFor(IsDark()); }

		// Lighter accent for glyphs and thin marks on dark surfaces (active icons, tab overline, checkmarks).
		inline ImVec4 HighlightFor(bool dark) {
			return dark ? ImVec4(0.616f, 0.737f, 0.969f, 1.00f) : ImVec4(0.184f, 0.420f, 0.859f, 1.00f);
		}
		inline ImVec4 Highlight() { return HighlightFor(IsDark()); }

		// Semantic status colours (text strength). Panels use these instead of literal reds/greens/yellows.
		inline ImVec4 Success() { return IsDark() ? ImVec4(0.482f, 0.788f, 0.435f, 1.00f) : ImVec4(0.212f, 0.533f, 0.188f, 1.00f); }
		inline ImVec4 Warning() { return IsDark() ? ImVec4(0.973f, 0.749f, 0.333f, 1.00f) : ImVec4(0.690f, 0.447f, 0.020f, 1.00f); }
		inline ImVec4 Error()   { return IsDark() ? ImVec4(0.949f, 0.420f, 0.400f, 1.00f) : ImVec4(0.769f, 0.200f, 0.220f, 1.00f); }
		inline ImVec4 Info()    { return IsDark() ? ImVec4(0.420f, 0.660f, 0.950f, 1.00f) : ImVec4(0.120f, 0.420f, 0.780f, 1.00f); }

		// Prefab instance whose source GUID resolves in the AssetRegistry. Blue to match the prefab glyph.
		inline ImVec4 PrefabInstance() {
			return IsDark() ? ImVec4(0.42f, 0.66f, 0.95f, 1.00f) : ImVec4(0.12f, 0.42f, 0.78f, 1.00f);
		}

		// Unresolvable source GUID (deleted file/missing metadata); apply/revert unavailable.
		inline ImVec4 PrefabOrphan() {
			return IsDark() ? ImVec4(0.949f, 0.420f, 0.400f, 1.00f) : ImVec4(0.769f, 0.200f, 0.220f, 1.00f);
		}

		// Marker next to a component header when any field differs from the source prefab.
		inline ImVec4 OverrideMarker() { return Highlight(); }

		// Selected row in the hierarchy (painted behind the row; alpha so guides and icons stay visible).
		inline ImVec4 RowSelection(bool pressed = false) {
			const ImVec4 a = Accent();
			return ImVec4(a.x, a.y, a.z, pressed ? (IsDark() ? 0.36f : 0.28f) : (IsDark() ? 0.26f : 0.20f));
		}

		// Fill for toggle buttons that are switched on (gizmo mode, grid snap, stats...).
		inline ImVec4 ActiveToggle() {
			const ImVec4 a = Accent();
			return ImVec4(a.x, a.y, a.z, IsDark() ? 0.32f : 0.24f);
		}

		inline ImVec4 AssetTileSelection() {
			const ImVec4 a = Accent();
			return ImVec4(a.x, a.y, a.z, IsDark() ? 0.22f : 0.18f);
		}

		inline ImVec4 AssetTileSelectionBorder() {
			const ImVec4 a = Accent();
			return ImVec4(a.x, a.y, a.z, 0.70f);
		}
	}
}
