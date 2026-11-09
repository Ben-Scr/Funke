#pragma once
#include <imgui.h>

namespace Funke::GlassBackdrop {

	bool IsEnabled();
	void Draw();
	void DrawWindowEdges();
	void DrawLastItemSurface();
	ImVec4 TitlebarColor(bool focused);
	void SyncViewportCaptions(bool glass);

}
