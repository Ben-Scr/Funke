#pragma once
#include "Core/Export.hpp"

namespace Funke::TitlebarRegions {

	enum class Hit : int {
		Client,
		Caption,
		ResizeTopLeft,
		ResizeTop,
		ResizeTopRight,
		ResizeRight,
		ResizeBottomRight,
		ResizeBottom,
		ResizeBottomLeft,
		ResizeLeft,
	};

	// Logical-pixel height of the draggable caption row.
	FUNKE_API void SetHeight(int logicalPx);
	FUNKE_API int GetHeight();

	// Rects are in window-client coordinates, as the custom ImGui titlebar lays them out.
	FUNKE_API void SetCaptionRect(int x, int y, int w, int h);
	// Caption-row points inside these rects stay Client so ImGui buttons in the titlebar receive clicks.
	FUNKE_API void AddNonClientRect(int x, int y, int w, int h);
	FUNKE_API void ResetNonClientRects();

	// resizeBorder <= 0 or maximized disables the edge resize zones.
	FUNKE_API Hit HitTest(int x, int y, int windowWidth, int windowHeight, int resizeBorder, bool resizable, bool maximized);

}
