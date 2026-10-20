#pragma once
#include <imgui.h>

namespace Funke::GlassBackdrop {

	// Slowly drifting colour blobs on the main viewport's background draw list, behind the translucent
	// Liquid Glass panels. Main window only: ImGui draws windows that own an OS window opaque.
	void Draw();

	// Native caption colour that continues the menu bar below it, so the OS titlebar isn't a grey strip.
	ImVec4 TitlebarColor(bool focused);

	// Gives the OS captions of floating windows (dialogs, tool windows) the glass colour, or the default back.
	// Every frame: viewports come and go.
	void SyncViewportCaptions(bool glass);

}
