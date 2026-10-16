#include "pch.hpp"
#include "Core/Platform/SDLInputMapping.hpp"
#include "Core/KeyCode.hpp"
#include "Core/MouseButton.hpp"

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>

namespace Funke::SDLInputMapping {

	namespace {
		struct KeyPair {
			SDL_Scancode Scancode;
			KeyCode Key;
		};

		constexpr KeyPair k_KeyPairs[] = {
			{ SDL_SCANCODE_SPACE, KeyCode::Space },
			{ SDL_SCANCODE_APOSTROPHE, KeyCode::Apostrophe },
			{ SDL_SCANCODE_COMMA, KeyCode::Comma },
			{ SDL_SCANCODE_MINUS, KeyCode::Minus },
			{ SDL_SCANCODE_PERIOD, KeyCode::Period },
			{ SDL_SCANCODE_SLASH, KeyCode::Slash },
			{ SDL_SCANCODE_0, KeyCode::Alpha0 },
			{ SDL_SCANCODE_1, KeyCode::Alpha1 },
			{ SDL_SCANCODE_2, KeyCode::Alpha2 },
			{ SDL_SCANCODE_3, KeyCode::Alpha3 },
			{ SDL_SCANCODE_4, KeyCode::Alpha4 },
			{ SDL_SCANCODE_5, KeyCode::Alpha5 },
			{ SDL_SCANCODE_6, KeyCode::Alpha6 },
			{ SDL_SCANCODE_7, KeyCode::Alpha7 },
			{ SDL_SCANCODE_8, KeyCode::Alpha8 },
			{ SDL_SCANCODE_9, KeyCode::Alpha9 },
			{ SDL_SCANCODE_SEMICOLON, KeyCode::Semicolon },
			{ SDL_SCANCODE_EQUALS, KeyCode::Equal },
			{ SDL_SCANCODE_A, KeyCode::A }, { SDL_SCANCODE_B, KeyCode::B }, { SDL_SCANCODE_C, KeyCode::C },
			{ SDL_SCANCODE_D, KeyCode::D }, { SDL_SCANCODE_E, KeyCode::E }, { SDL_SCANCODE_F, KeyCode::F },
			{ SDL_SCANCODE_G, KeyCode::G }, { SDL_SCANCODE_H, KeyCode::H }, { SDL_SCANCODE_I, KeyCode::I },
			{ SDL_SCANCODE_J, KeyCode::J }, { SDL_SCANCODE_K, KeyCode::K }, { SDL_SCANCODE_L, KeyCode::L },
			{ SDL_SCANCODE_M, KeyCode::M }, { SDL_SCANCODE_N, KeyCode::N }, { SDL_SCANCODE_O, KeyCode::O },
			{ SDL_SCANCODE_P, KeyCode::P }, { SDL_SCANCODE_Q, KeyCode::Q }, { SDL_SCANCODE_R, KeyCode::R },
			{ SDL_SCANCODE_S, KeyCode::S }, { SDL_SCANCODE_T, KeyCode::T }, { SDL_SCANCODE_U, KeyCode::U },
			{ SDL_SCANCODE_V, KeyCode::V }, { SDL_SCANCODE_W, KeyCode::W }, { SDL_SCANCODE_X, KeyCode::X },
			{ SDL_SCANCODE_Y, KeyCode::Y }, { SDL_SCANCODE_Z, KeyCode::Z },
			{ SDL_SCANCODE_LEFTBRACKET, KeyCode::LeftBracket },
			{ SDL_SCANCODE_BACKSLASH, KeyCode::Backslash },
			{ SDL_SCANCODE_RIGHTBRACKET, KeyCode::RightBracket },
			{ SDL_SCANCODE_GRAVE, KeyCode::GraveAccent },
			{ SDL_SCANCODE_NONUSHASH, KeyCode::World1 },
			{ SDL_SCANCODE_NONUSBACKSLASH, KeyCode::World2 },
			{ SDL_SCANCODE_ESCAPE, KeyCode::Esc },
			{ SDL_SCANCODE_RETURN, KeyCode::Enter },
			{ SDL_SCANCODE_TAB, KeyCode::Tab },
			{ SDL_SCANCODE_BACKSPACE, KeyCode::Backspace },
			{ SDL_SCANCODE_INSERT, KeyCode::Insert },
			{ SDL_SCANCODE_DELETE, KeyCode::Delete },
			{ SDL_SCANCODE_RIGHT, KeyCode::Right },
			{ SDL_SCANCODE_LEFT, KeyCode::Left },
			{ SDL_SCANCODE_DOWN, KeyCode::Bottom },
			{ SDL_SCANCODE_UP, KeyCode::Up },
			{ SDL_SCANCODE_PAGEUP, KeyCode::PageUp },
			{ SDL_SCANCODE_PAGEDOWN, KeyCode::PageDown },
			{ SDL_SCANCODE_HOME, KeyCode::Home },
			{ SDL_SCANCODE_END, KeyCode::End },
			{ SDL_SCANCODE_CAPSLOCK, KeyCode::CapsLock },
			{ SDL_SCANCODE_SCROLLLOCK, KeyCode::ScrollLock },
			{ SDL_SCANCODE_NUMLOCKCLEAR, KeyCode::NumLock },
			{ SDL_SCANCODE_PRINTSCREEN, KeyCode::PrintScreen },
			{ SDL_SCANCODE_PAUSE, KeyCode::Pause },
			{ SDL_SCANCODE_F1, KeyCode::F1 }, { SDL_SCANCODE_F2, KeyCode::F2 }, { SDL_SCANCODE_F3, KeyCode::F3 },
			{ SDL_SCANCODE_F4, KeyCode::F4 }, { SDL_SCANCODE_F5, KeyCode::F5 }, { SDL_SCANCODE_F6, KeyCode::F6 },
			{ SDL_SCANCODE_F7, KeyCode::F7 }, { SDL_SCANCODE_F8, KeyCode::F8 }, { SDL_SCANCODE_F9, KeyCode::F9 },
			{ SDL_SCANCODE_F10, KeyCode::F10 }, { SDL_SCANCODE_F11, KeyCode::F11 }, { SDL_SCANCODE_F12, KeyCode::F12 },
			{ SDL_SCANCODE_F13, KeyCode::F13 }, { SDL_SCANCODE_F14, KeyCode::F14 }, { SDL_SCANCODE_F15, KeyCode::F15 },
			{ SDL_SCANCODE_F16, KeyCode::F16 }, { SDL_SCANCODE_F17, KeyCode::F17 }, { SDL_SCANCODE_F18, KeyCode::F18 },
			{ SDL_SCANCODE_F19, KeyCode::F19 }, { SDL_SCANCODE_F20, KeyCode::F20 }, { SDL_SCANCODE_F21, KeyCode::F21 },
			{ SDL_SCANCODE_F22, KeyCode::F22 }, { SDL_SCANCODE_F23, KeyCode::F23 }, { SDL_SCANCODE_F24, KeyCode::F24 },
			{ SDL_SCANCODE_KP_0, KeyCode::Kp0 }, { SDL_SCANCODE_KP_1, KeyCode::Kp1 }, { SDL_SCANCODE_KP_2, KeyCode::Kp2 },
			{ SDL_SCANCODE_KP_3, KeyCode::Kp3 }, { SDL_SCANCODE_KP_4, KeyCode::Kp4 }, { SDL_SCANCODE_KP_5, KeyCode::Kp5 },
			{ SDL_SCANCODE_KP_6, KeyCode::Kp6 }, { SDL_SCANCODE_KP_7, KeyCode::Kp7 }, { SDL_SCANCODE_KP_8, KeyCode::Kp8 },
			{ SDL_SCANCODE_KP_9, KeyCode::Kp9 },
			{ SDL_SCANCODE_KP_PERIOD, KeyCode::KpDecimal },
			{ SDL_SCANCODE_KP_DIVIDE, KeyCode::KpDivide },
			{ SDL_SCANCODE_KP_MULTIPLY, KeyCode::KpMultiply },
			{ SDL_SCANCODE_KP_MINUS, KeyCode::KpSubtract },
			{ SDL_SCANCODE_KP_PLUS, KeyCode::KpAdd },
			{ SDL_SCANCODE_KP_ENTER, KeyCode::KpEnter },
			{ SDL_SCANCODE_KP_EQUALS, KeyCode::KpEqual },
			{ SDL_SCANCODE_LSHIFT, KeyCode::LeftShift },
			{ SDL_SCANCODE_LCTRL, KeyCode::LeftControl },
			{ SDL_SCANCODE_LALT, KeyCode::LeftAlt },
			{ SDL_SCANCODE_LGUI, KeyCode::LeftSuper },
			{ SDL_SCANCODE_RSHIFT, KeyCode::RightShift },
			{ SDL_SCANCODE_RCTRL, KeyCode::RightControl },
			{ SDL_SCANCODE_RALT, KeyCode::RightAlt },
			{ SDL_SCANCODE_RGUI, KeyCode::RightSuper },
			{ SDL_SCANCODE_APPLICATION, KeyCode::Menu },
			// Android's back button arrives as AC_BACK; games treat it as Escape.
			{ SDL_SCANCODE_AC_BACK, KeyCode::Esc },
		};

		struct KeyTables {
			std::array<int16_t, SDL_SCANCODE_COUNT> FromScancode{};
			std::array<int16_t, 512> ToScancode{};

			KeyTables() {
				FromScancode.fill(-1);
				ToScancode.fill(-1);
				for (const KeyPair& pair : k_KeyPairs) {
					FromScancode[pair.Scancode] = static_cast<int16_t>(pair.Key);
					int16_t& reverse = ToScancode[static_cast<size_t>(pair.Key)];
					if (reverse < 0) {
						reverse = static_cast<int16_t>(pair.Scancode);
					}
				}
			}
		};

		const KeyTables& Tables() {
			static const KeyTables s_Tables;
			return s_Tables;
		}
	}

	int KeyCodeFromScancode(int sdlScancode) {
		if (sdlScancode < 0 || sdlScancode >= SDL_SCANCODE_COUNT) return -1;
		return Tables().FromScancode[static_cast<size_t>(sdlScancode)];
	}

	int ScancodeFromKeyCode(int keyCode) {
		if (keyCode < 0 || keyCode >= static_cast<int>(Tables().ToScancode.size())) return -1;
		return Tables().ToScancode[static_cast<size_t>(keyCode)];
	}

	int MouseButtonFromSDL(int sdlButton) {
		switch (sdlButton) {
			case SDL_BUTTON_LEFT:   return static_cast<int>(MouseButton::Left);
			case SDL_BUTTON_RIGHT:  return static_cast<int>(MouseButton::Right);
			case SDL_BUTTON_MIDDLE: return static_cast<int>(MouseButton::Middle);
			case SDL_BUTTON_X1:     return static_cast<int>(MouseButton::Back);
			case SDL_BUTTON_X2:     return static_cast<int>(MouseButton::Revert);
			default: break;
		}
		if (sdlButton > SDL_BUTTON_X2 && sdlButton <= 8) {
			return sdlButton - 1;
		}
		return -1;
	}

	int SDLGamepadButtonFromIndex(int gamepadButton) {
		static constexpr SDL_GamepadButton k_Buttons[] = {
			SDL_GAMEPAD_BUTTON_SOUTH,          // A
			SDL_GAMEPAD_BUTTON_EAST,           // B
			SDL_GAMEPAD_BUTTON_WEST,           // X
			SDL_GAMEPAD_BUTTON_NORTH,          // Y
			SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
			SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
			SDL_GAMEPAD_BUTTON_BACK,
			SDL_GAMEPAD_BUTTON_START,
			SDL_GAMEPAD_BUTTON_GUIDE,
			SDL_GAMEPAD_BUTTON_LEFT_STICK,
			SDL_GAMEPAD_BUTTON_RIGHT_STICK,
			SDL_GAMEPAD_BUTTON_DPAD_UP,
			SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
			SDL_GAMEPAD_BUTTON_DPAD_DOWN,
			SDL_GAMEPAD_BUTTON_DPAD_LEFT,
		};
		if (gamepadButton < 0 || gamepadButton >= static_cast<int>(std::size(k_Buttons))) return -1;
		return static_cast<int>(k_Buttons[gamepadButton]);
	}

}
