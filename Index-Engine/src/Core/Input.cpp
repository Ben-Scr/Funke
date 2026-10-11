#include "pch.hpp"
#include "Input.hpp"
#include "Window.hpp"

#include <algorithm>
#include <cmath>

namespace Index {
    Input* Input::s_Current = nullptr;

    Input* Input::TryGetCurrent() {
        return s_Current;
    }

    void Input::SetCurrent(Input* input) {
        s_Current = input;
    }

    void Input::Reset() {
        m_CurrentKeyStates.fill(false);
        m_PreviousKeyStates.fill(false);
        m_CurrentMouseButtons.fill(false);
        m_PreviousMouseButtons.fill(false);
        m_Gamepads.fill(GamepadState{});
        m_ScrollValue = 0.f;
        m_MousePosition = { 0, 0 };
        m_MouseDelta = { 0, 0 };
        m_HasMousePosition = false;
        m_Axis = { 0, 0 };
        m_CharsThisFrame.clear();
    }

    bool Input::GetKey(KeyCode keycode) {
        const Input* input = TryGetCurrent();
        if (!input) return false;
        const int i = static_cast<int>(keycode);
        return (i >= 0 && i < k_KeyCount) ? input->m_CurrentKeyStates[i] : false;
    }

    bool Input::GetKeyDown(KeyCode keycode) {
        const Input* input = TryGetCurrent();
        if (!input) return false;
        const int i = static_cast<int>(keycode);
        return (i >= 0 && i < k_KeyCount) && input->m_CurrentKeyStates[i] && !input->m_PreviousKeyStates[i];
    }

    bool Input::GetKeyUp(KeyCode keycode) {
        const Input* input = TryGetCurrent();
        if (!input) return false;
        const int i = static_cast<int>(keycode);
        return (i >= 0 && i < k_KeyCount) && !input->m_CurrentKeyStates[i] && input->m_PreviousKeyStates[i];
    }

    bool Input::GetAnyKey() {
        const Input* input = TryGetCurrent();
        if (!input) return false;
        return std::any_of(input->m_CurrentKeyStates.begin(), input->m_CurrentKeyStates.end(), [](bool pressed) { return pressed; })
            || std::any_of(input->m_CurrentMouseButtons.begin(), input->m_CurrentMouseButtons.end(), [](bool pressed) { return pressed; });
    }

    bool Input::GetMouse(MouseButton keycode) {
        const Input* input = TryGetCurrent();
        if (!input) return false;
        const int btn = static_cast<int>(keycode);
        return (btn >= 0 && btn < k_MouseCount) ? input->m_CurrentMouseButtons[btn] : false;
    }

    bool Input::GetMouseDown(MouseButton keycode) {
        const Input* input = TryGetCurrent();
        if (!input) return false;
        const int btn = static_cast<int>(keycode);
        return (btn >= 0 && btn < k_MouseCount) && input->m_CurrentMouseButtons[btn] && !input->m_PreviousMouseButtons[btn];
    }

    bool Input::GetMouseUp(MouseButton keycode) {
        const Input* input = TryGetCurrent();
        if (!input) return false;
        const int btn = static_cast<int>(keycode);
        return (btn >= 0 && btn < k_MouseCount) && !input->m_CurrentMouseButtons[btn] && input->m_PreviousMouseButtons[btn];
    }

    bool Input::IsGamepadConnected(int gamepadIndex) {
        const Input* input = TryGetCurrent();
        return (gamepadIndex >= 0 && gamepadIndex < k_MaxGamepads)
            && input && input->m_Gamepads[gamepadIndex].Connected;
    }

    bool Input::GetGamepadButton(GamepadButton button, int gamepadIndex) {
        const Input* input = TryGetCurrent();
        if (!input) return false;
        if (gamepadIndex < 0 || gamepadIndex >= k_MaxGamepads) return false;
        const auto& g = input->m_Gamepads[gamepadIndex];
        if (!g.Connected) return false;
        const int b = static_cast<int>(button);
        return (b >= 0 && b < k_GamepadButtonCount) && g.CurrentButtons[b];
    }

    bool Input::GetGamepadButtonDown(GamepadButton button, int gamepadIndex) {
        const Input* input = TryGetCurrent();
        if (!input) return false;
        if (gamepadIndex < 0 || gamepadIndex >= k_MaxGamepads) return false;
        const auto& g = input->m_Gamepads[gamepadIndex];
        if (!g.Connected) return false;
        const int b = static_cast<int>(button);
        return (b >= 0 && b < k_GamepadButtonCount)
            && g.CurrentButtons[b] && !g.PreviousButtons[b];
    }

    bool Input::GetGamepadButtonUp(GamepadButton button, int gamepadIndex) {
        const Input* input = TryGetCurrent();
        if (!input) return false;
        if (gamepadIndex < 0 || gamepadIndex >= k_MaxGamepads) return false;
        const auto& g = input->m_Gamepads[gamepadIndex];
        if (!g.Connected) return false;
        const int b = static_cast<int>(button);
        return (b >= 0 && b < k_GamepadButtonCount)
            && !g.CurrentButtons[b] && g.PreviousButtons[b];
    }

    float Input::GetGamepadAxis(GamepadAxis axis, int gamepadIndex) {
        const Input* input = TryGetCurrent();
        if (!input) return 0.f;
        if (gamepadIndex < 0 || gamepadIndex >= k_MaxGamepads) return 0.f;
        const auto& g = input->m_Gamepads[gamepadIndex];
        if (!g.Connected) return 0.f;
        const int a = static_cast<int>(axis);
        return (a >= 0 && a < k_GamepadAxisCount) ? g.Axes[a] : 0.f;
    }

    Vec2 Input::GetAxis() {
        const Input* input = TryGetCurrent();
        return input ? input->m_Axis : Vec2{};
    }

    Vec2 Input::GetMousePosition() {
        const Input* input = TryGetCurrent();
        return input ? Window::ToViewportLocalPosition(input->m_MousePosition) : Vec2{};
    }

    Vec2 Input::GetMouseDelta() {
        const Input* input = TryGetCurrent();
        if (!input) return {};
        const Vec2 current = Window::ToViewportLocalPosition(input->m_MousePosition);
        const Vec2 previous = Window::ToViewportLocalPosition(input->m_MousePosition - input->m_MouseDelta);
        return current - previous;
    }

    float Input::ScrollValue() {
        const Input* input = TryGetCurrent();
        return input ? input->m_ScrollValue : 0.f;
    }

    const std::vector<uint32_t>& Input::GetCharsThisFrame() {
        static const std::vector<uint32_t> empty;
        const Input* input = TryGetCurrent();
        return input ? input->m_CharsThisFrame : empty;
    }

    void Input::PollGamepads() {
        for (int i = 0; i < k_MaxGamepads; ++i) {
            auto& g = m_Gamepads[i];
            g.PreviousButtons = g.CurrentButtons;

            GLFWgamepadstate state{};
            const int connected = glfwGetGamepadState(GLFW_JOYSTICK_1 + i, &state);
            if (connected == GLFW_TRUE) {
                g.Connected = true;
                for (int b = 0; b < k_GamepadButtonCount; ++b) {
                    g.CurrentButtons[b] = (state.buttons[b] == GLFW_PRESS);
                }
                for (int a = 0; a < k_GamepadAxisCount; ++a) {
                    g.Axes[a] = state.axes[a];
                }
            }
            else {
                g.Connected = false;
                g.CurrentButtons.fill(false);
                g.Axes.fill(0.f);
            }
        }
    }

    void Input::Update() {
        std::copy(m_CurrentKeyStates.begin(), m_CurrentKeyStates.end(), m_PreviousKeyStates.begin());
        std::copy(m_CurrentMouseButtons.begin(), m_CurrentMouseButtons.end(), m_PreviousMouseButtons.begin());

        PollGamepads();

        m_ScrollValue = 0.f;
        m_MouseDelta = { 0, 0 };
        m_CharsThisFrame.clear();
    }

    void Input::PostPoll() {
        // Recomputed AFTER glfwPollEvents() so the axis reflects the keys
        // pressed *this* frame, not last frame's snapshot. Application::Run
        // calls this immediately after polling.
        const bool inputRight = GetKey(KeyCode::D) || GetKey(KeyCode::Right);
        const bool inputUp = GetKey(KeyCode::W) || GetKey(KeyCode::Up);
        const bool inputDown = GetKey(KeyCode::S) || GetKey(KeyCode::Bottom);
        const bool inputLeft = GetKey(KeyCode::A) || GetKey(KeyCode::Left);

        float x = inputRight ? 1.f : (inputLeft ? -1.f : 0.f);
        float y = inputUp ? 1.f : (inputDown ? -1.f : 0.f);

        // Keyboard wins; otherwise the first connected pad drives the axis.
        if (x == 0.f && y == 0.f) {
            for (const GamepadState& pad : m_Gamepads) {
                if (!pad.Connected) continue;

                const auto pressed = [&](GamepadButton b) { return pad.CurrentButtons[static_cast<int>(b)]; };
                x = pressed(GamepadButton::DPadRight) ? 1.f : (pressed(GamepadButton::DPadLeft) ? -1.f : 0.f);
                y = pressed(GamepadButton::DPadUp) ? 1.f : (pressed(GamepadButton::DPadDown) ? -1.f : 0.f);
                if (x == 0.f && y == 0.f) {
                    const Vec2 stick = ApplyRadialDeadzone(
                        pad.Axes[static_cast<int>(GamepadAxis::LeftX)],
                        -pad.Axes[static_cast<int>(GamepadAxis::LeftY)],  // GLFW reports up as negative
                        k_GamepadAxisDeadzone);
                    x = stick.x;
                    y = stick.y;
                }
                break;
            }
        }
        m_Axis = { x, y };
    }

    Vec2 Input::ApplyRadialDeadzone(float x, float y, float deadzone) {
        const float length = std::sqrt(x * x + y * y);
        if (length <= deadzone || length == 0.f) return { 0.f, 0.f };
        const float scaled = std::min(1.f, (length - deadzone) / (1.f - deadzone));
        return { x / length * scaled, y / length * scaled };
    }

    void Input::OnChar(uint32_t codepoint) {
        // Filter out 0 (defensive — GLFW shouldn't send it but some drivers
        // surface stray null codepoints during composition).
        if (codepoint == 0) return;
        m_CharsThisFrame.push_back(codepoint);
    }

    std::string Input::GetTypedTextUtf8() {
        const Input* input = TryGetCurrent();
        if (!input) return {};
        std::string out;
        out.reserve(input->m_CharsThisFrame.size() * 4);
        for (uint32_t cp : input->m_CharsThisFrame) {
            // UTF-8 encoder. Same shape as the standard 4-step branchy
            // encoder; kept inline so Input has no extra dependencies.
            if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x110000) {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        return out;
    }

    void Input::OnKeyDown(int key) {
        if (key >= 0 && key < k_KeyCount)
            m_CurrentKeyStates[key] = true;
    }

    void Input::OnKeyUp(int key) {
        if (key >= 0 && key < k_KeyCount)
            m_CurrentKeyStates[key] = false;
    }

    void Input::OnMouseDown(int btn) {
        if (btn >= 0 && btn < k_MouseCount)
            m_CurrentMouseButtons[btn] = true;
    }

    void Input::OnMouseUp(int btn) {
        if (btn >= 0 && btn < k_MouseCount)
            m_CurrentMouseButtons[btn] = false;
    }

    void Input::OnMouseMove(double x, double y) {
        const Vec2 pos{ static_cast<float>(x), static_cast<float>(y) };
        // Accumulate: GLFW delivers several cursor events per frame and Update() zeroes the
        // delta once per frame, so assigning kept only the last segment and made mouse-look
        // under-sensitive and frame-rate dependent. The first sample only establishes the
        // position; otherwise the delta would equal the whole cursor position.
        if (m_HasMousePosition) {
            m_MouseDelta += pos - m_MousePosition;
        }
        m_MousePosition = pos;
        m_HasMousePosition = true;
    }
}
