#pragma once

#include "Core/Input.hpp"
#include "Core/PlatformDetection.hpp"

namespace Funke::PlatformShortcuts {

	enum class Modifiers : uint8_t {
		None = 0,
		Control = 1,
		Shift = 2,
		Alt = 4,
		Super = 8
	};

	constexpr Modifiers operator|(Modifiers left, Modifiers right) {
		return static_cast<Modifiers>(static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
	}

	struct Binding {
		KeyCode Key;
		Modifiers RequiredModifiers = Modifiers::None;
	};

#if defined(FNK_PLATFORM_MACOS)
	inline constexpr KeyCode k_PrimaryLeft = KeyCode::LeftSuper;
	inline constexpr KeyCode k_PrimaryRight = KeyCode::RightSuper;
	inline constexpr Binding k_Minimize{ KeyCode::M, Modifiers::Super };
	inline constexpr Binding k_Fullscreen{ KeyCode::F, Modifiers::Control | Modifiers::Super };
	inline constexpr Binding k_Stats{ KeyCode::Alpha6, Modifiers::Super | Modifiers::Shift };
	inline constexpr Binding k_Logs{ KeyCode::Alpha7, Modifiers::Super | Modifiers::Shift };
	inline constexpr Binding k_PhysicsDebug{ KeyCode::Alpha8, Modifiers::Super | Modifiers::Shift };
	inline constexpr Binding k_Profiler{ KeyCode::P, Modifiers::Super | Modifiers::Shift };
	inline constexpr const char* k_FullscreenLabel = "Ctrl+Cmd+F";
	inline constexpr const char* k_StatsLabel = "Cmd+Shift+6";
	inline constexpr const char* k_LogsLabel = "Cmd+Shift+7";
	inline constexpr const char* k_PhysicsDebugLabel = "Cmd+Shift+8";
	inline constexpr const char* k_ProfilerLabel = "Cmd+Shift+P";
#else
	inline constexpr KeyCode k_PrimaryLeft = KeyCode::LeftControl;
	inline constexpr KeyCode k_PrimaryRight = KeyCode::RightControl;
	inline constexpr Binding k_Minimize{ KeyCode::M, Modifiers::Control };
	inline constexpr Binding k_Fullscreen{ KeyCode::F11 };
	inline constexpr Binding k_Stats{ KeyCode::F6 };
	inline constexpr Binding k_Logs{ KeyCode::F7 };
	inline constexpr Binding k_PhysicsDebug{ KeyCode::F8 };
	inline constexpr Binding k_Profiler{ KeyCode::F6, Modifiers::Control };
	inline constexpr const char* k_FullscreenLabel = "F11";
	inline constexpr const char* k_StatsLabel = "F6";
	inline constexpr const char* k_LogsLabel = "F7";
	inline constexpr const char* k_PhysicsDebugLabel = "F8";
	inline constexpr const char* k_ProfilerLabel = "Ctrl+F6";
#endif

	constexpr bool Matches(const Binding& binding, KeyCode key, Modifiers modifiers) {
		return binding.Key == key && binding.RequiredModifiers == modifiers;
	}

	inline bool IsPrimaryDown() {
		return Input::GetKey(k_PrimaryLeft) || Input::GetKey(k_PrimaryRight);
	}

	inline bool IsPressed(const Binding& binding) {
		if (!Input::GetKeyDown(binding.Key)) return false;
		Modifiers modifiers = Modifiers::None;
		if (Input::GetKey(KeyCode::LeftControl) || Input::GetKey(KeyCode::RightControl)) modifiers = modifiers | Modifiers::Control;
		if (Input::GetKey(KeyCode::LeftShift) || Input::GetKey(KeyCode::RightShift)) modifiers = modifiers | Modifiers::Shift;
		if (Input::GetKey(KeyCode::LeftAlt) || Input::GetKey(KeyCode::RightAlt)) modifiers = modifiers | Modifiers::Alt;
		if (Input::GetKey(KeyCode::LeftSuper) || Input::GetKey(KeyCode::RightSuper)) modifiers = modifiers | Modifiers::Super;
		return Matches(binding, binding.Key, modifiers);
	}

}
