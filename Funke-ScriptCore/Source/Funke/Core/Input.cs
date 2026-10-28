using System;

namespace Funke;


/// <summary>
/// Provides static access to keyboard and mouse input state.
/// Maps to the native Funke InputManager.
/// </summary>
public static class Input
{
    public static event Action<KeyCode>? KeyDown;
    public static event Action<KeyCode>? KeyUp;
    public static event Action<char>? EnterChar;

    public static event Action<MouseButton>? MouseDown;
    public static event Action<MouseButton>? MouseUp;
    public static event Action<float>? MouseScroll;
    public static event Action<Vector2>? MouseMove;

    public static int KeyCount => 349;
    public static int MouseCount => 8;

    /// <summary>Viewport-local pixels with the origin at the visible game viewport's top-left.</summary>
    public static Vector2 MousePosition
    {
        get
        {
            InternalCalls.Input_GetMousePosition(out float x, out float y);
            return new Vector2(x, y);
        }
    }

    internal static void RaiseEnterChar(char c) => EnterChar?.Invoke(c);
    internal static void RaiseKeyDown(KeyCode key) => KeyDown?.Invoke(key);
    internal static void RaiseKeyUp(KeyCode key) => KeyUp?.Invoke(key);
    internal static void RaiseMouseDown(MouseButton button) => MouseDown?.Invoke(button);
    internal static void RaiseMouseUp(MouseButton button) => MouseUp?.Invoke(button);
    internal static void RaiseMouseScroll(float delta) => MouseScroll?.Invoke(delta);
    internal static void RaiseMouseMove(Vector2 position) => MouseMove?.Invoke(position);

    /// <summary>
    /// Movement axis from WASD/arrow keys, falling back to the first gamepad's left stick or D-pad
    /// when no key is held. X = horizontal (-1 to 1), Y = vertical (-1 to 1, up is positive).
    /// </summary>
    public static Vector2 GetAxis()
    {
        InternalCalls.Input_GetAxis(out float x, out float y);
        return new Vector2(x, y);
    }

    /// <summary>
    /// Like <see cref="GetAxis"/> but snapped to -1, 0 or 1 per axis.
    /// </summary>
    public static Vector2 GetAxisRaw()
    {
        float x = 0f, y = 0f;
        if (GetKey(KeyCode.D) || GetKey(KeyCode.Right)) x += 1f;
        if (GetKey(KeyCode.A) || GetKey(KeyCode.Left))  x -= 1f;
        if (GetKey(KeyCode.W) || GetKey(KeyCode.Up))    y += 1f;
        if (GetKey(KeyCode.S) || GetKey(KeyCode.Down))  y -= 1f;
        if (x == 0f && y == 0f)
        {
            Vector2 axis = GetAxis();
            x = MathF.Sign(axis.X);
            y = MathF.Sign(axis.Y);
        }
        return new Vector2(x, y);
    }

    // ── Touch ───────────────────────────────────────────────────────

    /// <summary>
    /// Number of fingers on a touchscreen this frame (0 on desktop without one). The mouse only follows the first
    /// finger, so read touches for anything that needs two at once, e.g. a joystick plus a jump button.
    /// </summary>
    public static int TouchCount => InternalCalls.Input_GetTouchCount();

    /// <summary>
    /// Finger <paramref name="index"/> (0..<see cref="TouchCount"/>-1), in the order they went down.
    /// Track a finger across frames by <see cref="Touch.Id"/>, not by index.
    /// </summary>
    public static Touch GetTouch(int index)
    {
        if (!InternalCalls.Input_GetTouch(index, out Touch touch))
            throw new ArgumentOutOfRangeException(nameof(index), $"Touch index {index} is outside 0..{TouchCount - 1}.");
        return touch;
    }

    // ── Motion sensors ──────────────────────────────────────────────

    /// <summary>
    /// Device acceleration in g, pointing toward gravity: about (0, -1, 0) with a phone held upright and (0, 0, -1)
    /// lying face up; tilting the right edge down makes X positive. Axes follow the screen as currently rotated
    /// (+X right, +Y up, +Z out of the screen). <see cref="Vector3.Zero"/> without an accelerometer (desktop, most Web).
    /// The sensor is switched on by the first read, so the first frame may still be zero.
    /// </summary>
    public static Vector3 Acceleration => InternalCalls.Input_GetAcceleration();

    /// <summary>
    /// Device rotation rate in radians per second around the screen axes of <see cref="Acceleration"/>,
    /// counter-clockwise positive. <see cref="Vector3.Zero"/> without a gyroscope.
    /// </summary>
    public static Vector3 Gyroscope => InternalCalls.Input_GetGyroscope();

    public static bool IsAccelerometerAvailable => InternalCalls.Input_IsMotionSensorAvailable(0);
    public static bool IsGyroscopeAvailable => InternalCalls.Input_IsMotionSensorAvailable(1);

    // ── Gamepad ─────────────────────────────────────────────────────

    /// <summary>Number of gamepad slots polled by the engine (indices 0..MaxGamepads-1).</summary>
    public const int MaxGamepads = 4;

    /// <summary>Radial deadzone applied by <see cref="GetLeftStick"/> and <see cref="GetRightStick"/>.</summary>
    public static float GamepadDeadzone { get; set; } = 0.2f;

    public static bool IsGamepadConnected(int gamepad = 0) => InternalCalls.Input_IsGamepadConnected(gamepad);

    /// <summary>Returns true every frame the gamepad button is held down.</summary>
    public static bool GetGamepadButton(GamepadButton button, int gamepad = 0)
        => InternalCalls.Input_GetGamepadButton((int)button, gamepad);

    /// <summary>Returns true only on the frame the gamepad button was first pressed.</summary>
    public static bool GetGamepadButtonDown(GamepadButton button, int gamepad = 0)
        => InternalCalls.Input_GetGamepadButtonDown((int)button, gamepad);

    /// <summary>Returns true only on the frame the gamepad button was released.</summary>
    public static bool GetGamepadButtonUp(GamepadButton button, int gamepad = 0)
        => InternalCalls.Input_GetGamepadButtonUp((int)button, gamepad);

    /// <summary>Raw axis value without deadzone: sticks in [-1, 1] (Y is down-positive), triggers in [-1, 1] (-1 = released).</summary>
    public static float GetGamepadAxis(GamepadAxis axis, int gamepad = 0)
        => InternalCalls.Input_GetGamepadAxis((int)axis, gamepad);

    /// <summary>Left stick with deadzone applied, up is positive Y.</summary>
    public static Vector2 GetLeftStick(int gamepad = 0)
        => ApplyDeadzone(GetGamepadAxis(GamepadAxis.LeftX, gamepad), -GetGamepadAxis(GamepadAxis.LeftY, gamepad), GamepadDeadzone);

    /// <summary>Right stick with deadzone applied, up is positive Y.</summary>
    public static Vector2 GetRightStick(int gamepad = 0)
        => ApplyDeadzone(GetGamepadAxis(GamepadAxis.RightX, gamepad), -GetGamepadAxis(GamepadAxis.RightY, gamepad), GamepadDeadzone);

    /// <summary>Trigger pressure remapped to [0, 1].</summary>
    public static float GetGamepadTrigger(GamepadAxis trigger, int gamepad = 0)
    {
        if (trigger != GamepadAxis.LeftTrigger && trigger != GamepadAxis.RightTrigger)
            throw new ArgumentOutOfRangeException(nameof(trigger), "Expected LeftTrigger or RightTrigger.");
        if (!IsGamepadConnected(gamepad)) return 0f;
        return Math.Clamp((GetGamepadAxis(trigger, gamepad) + 1f) * 0.5f, 0f, 1f);
    }

    // Rescales so output starts at 0 just outside the deadzone instead of jumping to its radius.
    internal static Vector2 ApplyDeadzone(float x, float y, float deadzone)
    {
        float length = MathF.Sqrt(x * x + y * y);
        if (length <= deadzone || length == 0f) return Vector2.Zero;
        float scaled = Math.Min(1f, (length - deadzone) / (1f - deadzone));
        return new Vector2(x / length * scaled, y / length * scaled);
    }

    /// <summary>
    /// Returns the mouse movement delta since the last frame.
    /// </summary>
    public static Vector2 GetMouseDelta()
    {
        InternalCalls.Input_GetMouseDelta(out float x, out float y);
        return new Vector2(x, y);
    }

    /// <summary>
    /// Returns the scroll wheel delta since the last frame.
    /// </summary>
    public static float GetScrollWheelDelta()
    {
        return InternalCalls.Input_GetScrollWheelDelta();
    }

    public static bool GetAnyKey() => InternalCalls.Input_GetAnyKey();

    /// <summary>
    /// Returns true every frame the key is held down.
    /// </summary>
    public static bool GetKey(KeyCode key) => InternalCalls.Input_GetKey((int)key);

    /// <summary>
    /// Returns true only on the frame the key was first pressed.
    /// </summary>
    public static bool GetKeyDown(KeyCode key) => InternalCalls.Input_GetKeyDown((int)key);

    /// <summary>
    /// Returns true only on the frame the key was released.
    /// </summary>
    public static bool GetKeyUp(KeyCode key) => InternalCalls.Input_GetKeyUp((int)key);

    /// <summary>
    /// Returns true every frame the mouse button is held down.
    /// </summary>
    public static bool GetMouseButton(MouseButton button) => InternalCalls.Input_GetMouseButton((int)button);

    /// <summary>
    /// Returns true only on the frame the mouse button was first pressed.
    /// </summary>
    public static bool GetMouseButtonDown(MouseButton button) => InternalCalls.Input_GetMouseButtonDown((int)button);

    /// <summary>
    /// Returns true only on the frame the mouse button was released.
    /// </summary>
    public static bool GetMouseButtonUp(MouseButton button) => InternalCalls.Input_GetMouseButtonUp((int)button);
}

/// <summary>Xbox-style button names; other controllers are mapped to the same layout by SDL's controller database.</summary>
public enum GamepadButton
{
    A = 0,
    B = 1,
    X = 2,
    Y = 3,
    LeftBumper = 4,
    RightBumper = 5,
    Back = 6,
    Start = 7,
    Guide = 8,
    LeftThumb = 9,
    RightThumb = 10,
    DPadUp = 11,
    DPadRight = 12,
    DPadDown = 13,
    DPadLeft = 14,
}

public enum TouchPhase
{
    /// <summary>The finger touched down this frame.</summary>
    Began = 0,
    Moved = 1,
    Stationary = 2,
    /// <summary>The finger was lifted (or the touch was cancelled); it is gone next frame.</summary>
    Ended = 3,
}

/// <summary>One finger on a touchscreen. Positions are viewport-local pixels like <see cref="Input.MousePosition"/>.</summary>
public readonly struct Touch
{
    public long Id { get; }
    public Vector2 Position { get; }
    /// <summary>Where the finger touched down.</summary>
    public Vector2 StartPosition { get; }
    public TouchPhase Phase { get; }

    public Touch(long id, Vector2 position, Vector2 startPosition, TouchPhase phase)
    {
        Id = id;
        Position = position;
        StartPosition = startPosition;
        Phase = phase;
    }
}

public enum GamepadAxis
{
    LeftX = 0,
    LeftY = 1,
    RightX = 2,
    RightY = 3,
    LeftTrigger = 4,
    RightTrigger = 5,
}
