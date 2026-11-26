#pragma once

namespace Funke::GrabCursor {

	// Request the open palm or closed grabbing hand for the current ImGui frame.
	void Request(bool grabbing);
	// Call after all panels; restores ImGui's native cursor when no hand is requested.
	void Apply();
	// Release native cursors before shutting down the SDL/ImGui backends.
	void Shutdown();
}
