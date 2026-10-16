#include "pch.hpp"
#include "Input.hpp"
#include "Window.hpp"
#include "Core/Platform/SDLInputMapping.hpp"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_sensor.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cmath>

namespace Funke {
    namespace {
        int OrientationQuarterTurns(SDL_DisplayOrientation orientation) {
            switch (orientation) {
                case SDL_ORIENTATION_LANDSCAPE:         return 1;  // right side up
                case SDL_ORIENTATION_PORTRAIT_FLIPPED:  return 2;
                case SDL_ORIENTATION_LANDSCAPE_FLIPPED: return 3;
                default:                                return 0;
            }
        }
    }

    Vec3 Input::DeviceToScreenAxes(const Vec3& v, int quarterTurns) {
        switch (quarterTurns & 3) {
            case 1:  return { -v.y,  v.x, v.z };
            case 2:  return { -v.x, -v.y, v.z };
            case 3:  return {  v.y, -v.x, v.z };
            default: return v;
        }
    }

    Input* Input::s_Current = nullptr;

    Input* Input::TryGetCurrent() {
        return s_Current;
    }

    void Input::SetCurrent(Input* input) {
        s_Current = input;
    }

    void Input::Reset() {
        m_CurrentKeyStates.fill(false);
        m_CurrentMouseButtons.fill(false);
        m_KeysPressedThisFrame.fill(false);
        m_KeysReleasedThisFrame.fill(false);
        m_MousePressedThisFrame.fill(false);
        m_MouseReleasedThisFrame.fill(false);
        // Device handles outlive a scene reload; only the sampled state resets.
        for (GamepadState& pad : m_Gamepads) {
            pad.CurrentButtons.fill(false);
            pad.PreviousButtons.fill(false);
            pad.Axes.fill(0.f);
        }
        m_Acceleration = Vec3{ 0.f };
        m_AngularVelocity = Vec3{ 0.f };
        m_ScrollValue = 0.f;
        m_MousePosition = { 0, 0 };
        m_MouseDelta = { 0, 0 };
        m_HasMousePosition = false;
        m_Axis = { 0, 0 };
        m_CharsThisFrame.clear();
        m_Touches.Clear();
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
        return (i >= 0 && i < k_KeyCount) && input->m_KeysPressedThisFrame[i];
    }

    bool Input::GetKeyUp(KeyCode keycode) {
        const Input* input = TryGetCurrent();
        if (!input) return false;
        const int i = static_cast<int>(keycode);
        return (i >= 0 && i < k_KeyCount) && input->m_KeysReleasedThisFrame[i];
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
        return (btn >= 0 && btn < k_MouseCount) && input->m_MousePressedThisFrame[btn];
    }

    bool Input::GetMouseUp(MouseButton keycode) {
        const Input* input = TryGetCurrent();
        if (!input) return false;
        const int btn = static_cast<int>(keycode);
        return (btn >= 0 && btn < k_MouseCount) && input->m_MouseReleasedThisFrame[btn];
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

    int Input::GetTouchCount() {
        const Input* input = TryGetCurrent();
        return input ? input->m_Touches.GetCount() : 0;
    }

    bool Input::GetTouch(int index, Touch& outTouch) {
        const Input* input = TryGetCurrent();
        const Touch* touch = input ? input->m_Touches.Get(index) : nullptr;
        if (!touch) return false;
        outTouch = *touch;
        outTouch.Position = Window::ToViewportLocalPosition(touch->Position);
        outTouch.StartPosition = Window::ToViewportLocalPosition(touch->StartPosition);
        return true;
    }

    void TouchTracker::BeginFrame() {
        std::erase_if(m_Touches, [](const Entry& entry) { return entry.Data.Phase == TouchPhase::Ended; });
        for (Entry& entry : m_Touches) {
            entry.Data.Phase = entry.EndPending ? TouchPhase::Ended : TouchPhase::Stationary;
            entry.EndPending = false;
        }
    }

    TouchTracker::Entry* TouchTracker::Find(int64_t id) {
        for (Entry& entry : m_Touches) {
            if (entry.Data.Id == id && entry.Data.Phase != TouchPhase::Ended && !entry.EndPending) return &entry;
        }
        return nullptr;
    }

    void TouchTracker::OnDown(int64_t id, Vec2 position) {
        const Touch touch{ id, position, position, TouchPhase::Began };
        // A missed up event restarts the finger rather than keeping a stale one.
        if (Entry* existing = Find(id)) *existing = Entry{ touch, false };
        else m_Touches.push_back(Entry{ touch, false });
    }

    void TouchTracker::OnMove(int64_t id, Vec2 position) {
        Entry* entry = Find(id);
        if (!entry) return;
        entry->Data.Position = position;
        if (entry->Data.Phase == TouchPhase::Stationary) entry->Data.Phase = TouchPhase::Moved;
    }

    void TouchTracker::OnUp(int64_t id, Vec2 position) {
        Entry* entry = Find(id);
        if (!entry) return;
        entry->Data.Position = position;
        if (entry->Data.Phase == TouchPhase::Began) entry->EndPending = true;
        else entry->Data.Phase = TouchPhase::Ended;
    }

    const Touch* TouchTracker::Get(int index) const {
        if (index < 0 || index >= static_cast<int>(m_Touches.size())) return nullptr;
        return &m_Touches[static_cast<size_t>(index)].Data;
    }

    Vec3 Input::GetAcceleration() {
        Input* input = TryGetCurrent();
        if (!input) return Vec3{ 0.f };
        input->EnsureSensorsOpen();
        return input->m_Acceleration;
    }

    Vec3 Input::GetGyroscope() {
        Input* input = TryGetCurrent();
        if (!input) return Vec3{ 0.f };
        input->EnsureSensorsOpen();
        return input->m_AngularVelocity;
    }

    bool Input::IsAccelerometerAvailable() {
        Input* input = TryGetCurrent();
        if (!input) return false;
        input->EnsureSensorsOpen();
        return input->m_Accelerometer != nullptr;
    }

    bool Input::IsGyroscopeAvailable() {
        Input* input = TryGetCurrent();
        if (!input) return false;
        input->EnsureSensorsOpen();
        return input->m_Gyroscope != nullptr;
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

            if (g.Handle && SDL_GamepadConnected(g.Handle)) {
                g.Connected = true;
                for (int b = 0; b < k_GamepadButtonCount; ++b) {
                    const int sdlButton = SDLInputMapping::SDLGamepadButtonFromIndex(b);
                    g.CurrentButtons[b] = sdlButton >= 0
                        && SDL_GetGamepadButton(g.Handle, static_cast<SDL_GamepadButton>(sdlButton));
                }
                // GamepadAxis matches SDL_GamepadAxis order. SDL triggers are [0, 32767]; the public contract keeps them in [-1, 1].
                for (int a = 0; a < k_GamepadAxisCount; ++a) {
                    const Sint16 raw = SDL_GetGamepadAxis(g.Handle, static_cast<SDL_GamepadAxis>(a));
                    const float value = std::max(-1.f, static_cast<float>(raw) / 32767.f);
                    const bool isTrigger = a == static_cast<int>(GamepadAxis::LeftTrigger)
                        || a == static_cast<int>(GamepadAxis::RightTrigger);
                    g.Axes[a] = isTrigger ? value * 2.f - 1.f : value;
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
        m_KeysPressedThisFrame.fill(false);
        m_KeysReleasedThisFrame.fill(false);
        m_MousePressedThisFrame.fill(false);
        m_MouseReleasedThisFrame.fill(false);

        m_ScrollValue = 0.f;
        m_MouseDelta = { 0, 0 };
        m_CharsThisFrame.clear();

        m_Touches.BeginFrame();
    }

    void Input::PostPoll() {
        // Runs AFTER Window::PollEvents so gamepads and the axis reflect *this*
        // frame's input, not last frame's snapshot.
        PollGamepads();
        PollSensors();

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
                        -pad.Axes[static_cast<int>(GamepadAxis::LeftY)],  // SDL reports up as negative
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
        // Filter out 0: some IMEs surface stray null codepoints during composition.
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
        if (key < 0 || key >= k_KeyCount) return;
        // Key repeat arrives as further downs while held; only the first is a press.
        if (!m_CurrentKeyStates[key]) m_KeysPressedThisFrame[key] = true;
        m_CurrentKeyStates[key] = true;
    }

    void Input::OnKeyUp(int key) {
        if (key < 0 || key >= k_KeyCount) return;
        if (m_CurrentKeyStates[key]) m_KeysReleasedThisFrame[key] = true;
        m_CurrentKeyStates[key] = false;
    }

    void Input::OnMouseDown(int btn) {
        if (btn < 0 || btn >= k_MouseCount) return;
        if (!m_CurrentMouseButtons[btn]) m_MousePressedThisFrame[btn] = true;
        m_CurrentMouseButtons[btn] = true;
    }

    void Input::OnMouseUp(int btn) {
        if (btn < 0 || btn >= k_MouseCount) return;
        if (m_CurrentMouseButtons[btn]) m_MouseReleasedThisFrame[btn] = true;
        m_CurrentMouseButtons[btn] = false;
    }

    void Input::OnMouseMove(double x, double y) {
        const Vec2 pos{ static_cast<float>(x), static_cast<float>(y) };
        // Accumulate: SDL delivers several motion events per frame and Update() zeroes the
        // delta once per frame, so assigning kept only the last segment and made mouse-look
        // under-sensitive and frame-rate dependent. The first sample only establishes the
        // position; otherwise the delta would equal the whole cursor position.
        if (m_HasMousePosition) {
            m_MouseDelta += pos - m_MousePosition;
        }
        m_MousePosition = pos;
        m_HasMousePosition = true;
    }

    void Input::OnMouseMoveRelative(Vec2 position, Vec2 delta) {
        m_MouseDelta += delta;
        m_MousePosition = position;
        m_HasMousePosition = true;
    }

    void Input::OnGamepadAdded(uint32_t joystickId) {
        for (const GamepadState& pad : m_Gamepads) {
            if (pad.Handle && pad.JoystickId == joystickId) return;
        }
        for (GamepadState& pad : m_Gamepads) {
            if (pad.Handle) continue;
            pad.Handle = SDL_OpenGamepad(joystickId);
            if (!pad.Handle) {
                FNK_CORE_WARN_TAG("Input", "SDL_OpenGamepad({}) failed: {}", joystickId, SDL_GetError());
                return;
            }
            pad.JoystickId = joystickId;
            const char* name = SDL_GetGamepadName(pad.Handle);
            FNK_CORE_INFO_TAG("Input", "Gamepad connected: {}", name ? name : "unknown");
            return;
        }
        FNK_CORE_WARN_TAG("Input", "Ignoring gamepad {}: all {} slots are in use", joystickId, k_MaxGamepads);
    }

    void Input::OnGamepadRemoved(uint32_t joystickId) {
        for (GamepadState& pad : m_Gamepads) {
            if (!pad.Handle || pad.JoystickId != joystickId) continue;
            SDL_CloseGamepad(pad.Handle);
            pad = GamepadState{};
            FNK_CORE_INFO_TAG("Input", "Gamepad disconnected ({})", joystickId);
            return;
        }
    }

    void Input::CloseGamepads() {
        for (GamepadState& pad : m_Gamepads) {
            if (pad.Handle) SDL_CloseGamepad(pad.Handle);
            pad = GamepadState{};
        }
    }

    void Input::EnsureSensorsOpen() {
        if (m_SensorsOpened) return;
        m_SensorsOpened = true;

        if (!SDL_InitSubSystem(SDL_INIT_SENSOR)) {
            FNK_CORE_WARN_TAG("Input", "SDL sensor subsystem unavailable: {}", SDL_GetError());
            return;
        }
        // Values are sampled in PollSensors; per-reading events would only flood the queue.
        SDL_SetEventEnabled(SDL_EVENT_SENSOR_UPDATE, false);

        int count = 0;
        SDL_SensorID* ids = SDL_GetSensors(&count);
        for (int i = 0; ids && i < count; ++i) {
            const SDL_SensorType type = SDL_GetSensorTypeForID(ids[i]);
            SDL_Sensor** slot = type == SDL_SENSOR_ACCEL ? &m_Accelerometer
                : type == SDL_SENSOR_GYRO ? &m_Gyroscope : nullptr;
            if (!slot || *slot) continue;
            *slot = SDL_OpenSensor(ids[i]);
            if (!*slot) FNK_CORE_WARN_TAG("Input", "SDL_OpenSensor({}) failed: {}", ids[i], SDL_GetError());
        }
        SDL_free(ids);

        FNK_CORE_INFO_TAG("Input", "Motion sensors: accelerometer {}, gyroscope {}",
            m_Accelerometer ? "yes" : "no", m_Gyroscope ? "yes" : "no");
        PollSensors();
    }

    void Input::PollSensors() {
        if (!m_Accelerometer && !m_Gyroscope) return;

        const SDL_DisplayID display = SDL_GetPrimaryDisplay();
        const SDL_DisplayOrientation current = SDL_GetCurrentDisplayOrientation(display);
        const SDL_DisplayOrientation natural = SDL_GetNaturalDisplayOrientation(display);
        const int quarterTurns = (current == SDL_ORIENTATION_UNKNOWN || natural == SDL_ORIENTATION_UNKNOWN)
            ? 0 : OrientationQuarterTurns(current) - OrientationQuarterTurns(natural);

        float data[3]{};
        if (m_Accelerometer && SDL_GetSensorData(m_Accelerometer, data, 3)) {
            // SDL reports the reaction force in m/s²; flip it to point toward gravity, in g.
            m_Acceleration = DeviceToScreenAxes({ data[0], data[1], data[2] }, quarterTurns) * (-1.f / SDL_STANDARD_GRAVITY);
        }
        if (m_Gyroscope && SDL_GetSensorData(m_Gyroscope, data, 3)) {
            m_AngularVelocity = DeviceToScreenAxes({ data[0], data[1], data[2] }, quarterTurns);
        }
    }

    void Input::CloseSensors() {
        if (m_Accelerometer) SDL_CloseSensor(m_Accelerometer);
        if (m_Gyroscope) SDL_CloseSensor(m_Gyroscope);
        if (m_SensorsOpened && SDL_WasInit(SDL_INIT_SENSOR)) SDL_QuitSubSystem(SDL_INIT_SENSOR);
        m_Accelerometer = nullptr;
        m_Gyroscope = nullptr;
        m_SensorsOpened = false;
        m_Acceleration = Vec3{ 0.f };
        m_AngularVelocity = Vec3{ 0.f };
    }
}
