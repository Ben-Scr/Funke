#pragma once
#include <imgui.h>

namespace Funke::GlassBackdrop {

	bool IsEnabled();
	void Draw();
	void DrawWindowEdges();
	void DrawLastItemSurface();
	void DrawLastItemSurface(ImVec2 min, ImVec2 max);
	ImVec4 TitlebarColor(bool focused);
	void SyncViewportCaptions(bool glass);

}
