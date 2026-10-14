#pragma once
#include "Core/Export.hpp"

namespace Index::TitlebarRegions {

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
	INDEX_API void SetHeight(int logicalPx);
	INDEX_API int GetHeight();

	// Rects are in window-client coordinates, as the custom ImGui titlebar lays them out.
	INDEX_API void SetCaptionRect(int x, int y, int w, int h);
	// Caption-row points inside these rects stay Client so ImGui buttons in the titlebar receive clicks.
	INDEX_API void AddNonClientRect(int x, int y, int w, int h);
	INDEX_API void ResetNonClientRects();

	// resizeBorder <= 0 or maximized disables the edge resize zones.
	INDEX_API Hit HitTest(int x, int y, int windowWidth, int windowHeight, int resizeBorder, bool resizable, bool maximized);

}
