#pragma once

namespace Funke::EditorRuntime {

	// Draws the Win32BuildProgressWindow state as a centered ImGui window on Linux and macOS, where there is
	// no native progress popup. No-op on Windows. Call once per frame inside an ImGui frame, after the host's
	// Show / Update / Hide calls for that frame.
	void RenderBuildProgressOverlay();

}
