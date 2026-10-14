#pragma once
#include "Core/Export.hpp"

namespace Index::SDLInputMapping {

	// KeyCode values predate SDL and are serialized by projects and C# scripts, so SDL's codes are translated, never exposed.
	// Returns -1 for scancodes without an Index KeyCode.
	INDEX_API int KeyCodeFromScancode(int sdlScancode);
	INDEX_API int ScancodeFromKeyCode(int keyCode);

	// SDL buttons are 1-based (1 left, 2 middle, 3 right); MouseButton is Left/Right/Middle/Back/Forward. -1 when out of range.
	INDEX_API int MouseButtonFromSDL(int sdlButton);

	// Index GamepadButton -> SDL_GamepadButton; -1 when out of range.
	INDEX_API int SDLGamepadButtonFromIndex(int gamepadButton);

}
