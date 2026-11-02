#include "pch.hpp"
#include "Core/Window.hpp"
#include "Core/Application.hpp"
#include "Core/Platform/SDLInputMapping.hpp"
#include <SDL3/SDL.h>
#include <cmath>

namespace Funke {
	 namespace {
		SDL_Joystick* s_Joystick = nullptr;
		SDL_JoystickID s_Id = 0;
	}

	int Window::ConnectAutomationGamepad() {
		auto* app = Application::GetInstance();
		if (!app) return -1;
		if (!s_Joystick) {
			bool free = false;
			for (const auto& pad : app->m_Input.m_Gamepads) if (!pad.Handle) free = true;
			if (!free) return -1;
			SDL_VirtualJoystickDesc desc{};
			SDL_INIT_INTERFACE(&desc);
			desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
			desc.name = "Funke Automation";
			desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
			desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
			desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
			desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
			s_Id = SDL_AttachVirtualJoystick(&desc);
			if (!s_Id) return -1;
			s_Joystick = SDL_OpenJoystick(s_Id);
			if (!s_Joystick) { SDL_DetachVirtualJoystick(s_Id); s_Id = 0; return -1; }
			SDL_SetJoystickVirtualAxis(s_Joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
			SDL_SetJoystickVirtualAxis(s_Joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
			SDL_UpdateJoysticks();
			app->m_Input.OnGamepadAdded(s_Id);
		}
		for (int i = 0; i < Input::k_MaxGamepads; ++i) if (app->m_Input.m_Gamepads[i].JoystickId == s_Id) return i;
		DisconnectAutomationGamepad();
		return -1;
	}

	bool Window::InjectGamepadButton(int button, bool pressed) {
		const int mapped = SDLInputMapping::SDLGamepadButtonFromIndex(button);
		return s_Joystick && mapped >= 0 && SDL_SetJoystickVirtualButton(s_Joystick, mapped, pressed);
	}

	bool Window::InjectGamepadAxis(int axis, float value) {
		if (!s_Joystick || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT || !std::isfinite(value) || value < -1 || value > 1) return false;
		return SDL_SetJoystickVirtualAxis(s_Joystick, axis, value == -1 ? -32768 : static_cast<Sint16>(std::lround(value * 32767)));
	}

	void Window::RefreshAutomationGamepad() {
		if (!s_Joystick) return;
		SDL_UpdateJoysticks();
		if (auto* app = Application::GetInstance()) app->m_Input.PollGamepads(false);
	}

	void Window::DisconnectAutomationGamepad() {
		if (!s_Id) return;
		if (auto* app = Application::GetInstance()) app->m_Input.OnGamepadRemoved(s_Id);
		if (s_Joystick) SDL_CloseJoystick(s_Joystick);
		SDL_DetachVirtualJoystick(s_Id);
		s_Joystick = nullptr;
		s_Id = 0;
	}
}
