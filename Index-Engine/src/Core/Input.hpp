#pragma once

#include "Collections/Vec2.hpp"
#include "Core/Export.hpp"
#include "KeyCodes.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Gamepad;

namespace Index {
    class Application;
    class Window;

    // Positional names on the Xbox layout (A = bottom face button); SDL's
    // controller database maps other pad families onto the same positions.
    // Values are stable for C# and serialized input bindings.
    enum class GamepadButton : std::uint8_t {
        A            = 0,
        B            = 1,
        X            = 2,
        Y            = 3,
        LeftBumper   = 4,
        RightBumper  = 5,
        Back         = 6,
        Start        = 7,
        Guide        = 8,
        LeftThumb    = 9,
        RightThumb   = 10,
        DPadUp       = 11,
        DPadRight    = 12,
        DPadDown     = 13,
        DPadLeft     = 14,
    };

    enum class GamepadAxis : std::uint8_t {
        LeftX        = 0,
        LeftY        = 1,
        RightX       = 2,
        RightY       = 3,
        LeftTrigger  = 4,
        RightTrigger = 5,
    };

    class INDEX_API Input {
    public:
        friend class Application;
        friend class Window;

        // Keyboard
        static bool GetKey(KeyCode keycode);
        static bool GetKeyDown(KeyCode keycode);
        static bool GetKeyUp(KeyCode keycode);
        static bool GetAnyKey();

        // Mouse
        static bool GetMouse(MouseButton keycode);
        static bool GetMouseDown(MouseButton keycode);
        static bool GetMouseUp(MouseButton keycode);

        static constexpr int k_MaxGamepads = 4;
        static bool IsGamepadConnected(int gamepadIndex = 0);
        static bool GetGamepadButton(GamepadButton button, int gamepadIndex = 0);
        static bool GetGamepadButtonDown(GamepadButton button, int gamepadIndex = 0);
        static bool GetGamepadButtonUp(GamepadButton button, int gamepadIndex = 0);
        // Returns the raw axis value in [-1, 1]; triggers rest at -1 (C# GetGamepadTrigger remaps to [0, 1]).
        // Apply your own deadzone — Input does no shaping here so the
        // raw values stay available for analog gameplay use.
        static float GetGamepadAxis(GamepadAxis axis, int gamepadIndex = 0);

		// WASD/arrows; falls back to the first connected gamepad's D-pad or left stick (deadzoned).
		static Vec2 GetAxis();
		// Scales the stick so output starts at 0 at the deadzone edge instead of jumping to its radius.
		static Vec2 ApplyRadialDeadzone(float x, float y, float deadzone);
		static constexpr float k_GamepadAxisDeadzone = 0.2f;
		// Viewport-local pixels; raw window coordinates stay stored so resize remapping remains current.
		static Vec2 GetMousePosition();
        static Vec2 GetMouseDelta();
        static float ScrollValue();

        // Unicode codepoints typed this frame, in the order they arrived
        // as text-input events. Cleared at the start of every Update.
        // UI input fields read this each frame to append typed characters.
        static const std::vector<uint32_t>& GetCharsThisFrame();

        // Convenience: UTF-8 encoded version of GetCharsThisFrame, ready
        // to append to a std::string. Returns empty when nothing was
        // typed this frame.
        static std::string GetTypedTextUtf8();

    private:
        Input() = default;
        Input(const Input&) = delete;
        Input& operator=(const Input&) = delete;

        static Input* TryGetCurrent();
        static void SetCurrent(Input* input);

        void Reset();
        void Update();
        // Recompute frame-derived state (gamepads, m_Axis) after Window::PollEvents.
        // Called by Application::Run AFTER polling so reads reflect *this*
        // frame's keys, not last frame's.
        void PostPoll();
        void PollGamepads();

        void OnKeyDown(int key);
        void OnKeyUp(int key);
        void OnMouseDown(int btn);
        void OnMouseUp(int btn);
        void OnScroll(float delta) { m_ScrollValue += delta; }
        void OnMouseMove(double x, double y);
        void OnChar(uint32_t codepoint);
        void OnGamepadAdded(uint32_t joystickId);
        void OnGamepadRemoved(uint32_t joystickId);
        void CloseGamepads();

        static constexpr int k_KeyCount = static_cast<int>(KeyCode::Menu) + 1;
        static constexpr int k_MouseCount = 8;
        static constexpr int k_GamepadButtonCount = 15;
        static constexpr int k_GamepadAxisCount = 6;

        std::array<bool, k_KeyCount> m_CurrentKeyStates{};
        std::array<bool, k_MouseCount> m_CurrentMouseButtons{};
        // Press/release edges seen during this frame's poll. Diffing held state against last frame loses
        // a press and release that land in one poll (soft-keyboard Backspace, very short taps).
        std::array<bool, k_KeyCount> m_KeysPressedThisFrame{};
        std::array<bool, k_KeyCount> m_KeysReleasedThisFrame{};
        std::array<bool, k_MouseCount> m_MousePressedThisFrame{};
        std::array<bool, k_MouseCount> m_MouseReleasedThisFrame{};

        struct GamepadState {
            SDL_Gamepad* Handle = nullptr;
            uint32_t JoystickId = 0;
            bool Connected = false;
            std::array<bool, k_GamepadButtonCount> CurrentButtons{};
            std::array<bool, k_GamepadButtonCount> PreviousButtons{};
            std::array<float, k_GamepadAxisCount> Axes{};
        };
        std::array<GamepadState, k_MaxGamepads> m_Gamepads{};

        float m_ScrollValue = 0.f;
        Vec2 m_MousePosition = { 0, 0 };
        Vec2 m_MouseDelta = { 0, 0 };
        bool m_HasMousePosition = false;
        Vec2 m_Axis = { 0, 0 };

        std::vector<uint32_t> m_CharsThisFrame;

        static Input* s_Current;
    };
}
