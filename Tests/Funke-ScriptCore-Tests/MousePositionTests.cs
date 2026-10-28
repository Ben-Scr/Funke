using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[CollectionDefinition(Name, DisableParallelization = true)]
public sealed class NativeCallbacksCollection
{
    public const string Name = "Native callbacks";
}

[Collection(NativeCallbacksCollection.Name)]
public unsafe class MousePositionTests
{
    private static bool s_GamepadConnected;
    private static float s_TriggerAxis;

    [UnmanagedCallersOnly]
    private static int IsGamepadConnected(int gamepad) => gamepad == 0 && s_GamepadConnected ? 1 : 0;

    [UnmanagedCallersOnly]
    private static float GetGamepadAxis(int axis, int gamepad) => gamepad == 0 && s_GamepadConnected ? s_TriggerAxis : 0f;

    [Theory]
    [InlineData(false, 0, 0f, 0f)]
    [InlineData(false, -1, 0f, 0f)]
    [InlineData(true, 4, 0f, 0f)]
    [InlineData(true, 0, -1f, 0f)]
    [InlineData(true, 0, 0f, 0.5f)]
    [InlineData(true, 0, 1f, 1f)]
    public void TriggerPressureIsZeroWithoutAConnectedGamepad(bool connected, int gamepad, float rawAxis, float expected)
    {
        NativeBindingsStruct previous = NativeCallbacks.Bindings;
        try
        {
            s_GamepadConnected = connected;
            s_TriggerAxis = rawAxis;
            NativeBindingsStruct bindings = previous;
            bindings.Input_IsGamepadConnected = &IsGamepadConnected;
            bindings.Input_GetGamepadAxis = &GetGamepadAxis;
            NativeCallbacks.Bindings = bindings;
            Assert.Equal(expected, Input.GetGamepadTrigger(GamepadAxis.LeftTrigger, gamepad));
            Assert.Equal(expected, Input.GetGamepadTrigger(GamepadAxis.RightTrigger, gamepad));
        }
        finally { NativeCallbacks.Bindings = previous; }
    }

    [UnmanagedCallersOnly]
    private static void ReturnViewportPosition(float* outX, float* outY)
    {
        *outX = 321.0f;
        *outY = 123.0f;
    }

    [Fact]
    public void MousePositionReturnsNativeViewportCoordinatesUnchanged()
    {
        NativeBindingsStruct previous = NativeCallbacks.Bindings;
        try
        {
            NativeBindingsStruct bindings = previous;
            bindings.Input_GetMousePosition = &ReturnViewportPosition;
            NativeCallbacks.Bindings = bindings;

            Vector2 position = Input.MousePosition;

            Assert.Equal(321.0f, position.X);
            Assert.Equal(123.0f, position.Y);
        }
        finally
        {
            NativeCallbacks.Bindings = previous;
        }
    }
}

public class GamepadDeadzoneTests
{
    [Fact]
    public void InsideDeadzoneIsZero()
    {
        Assert.Equal(Vector2.Zero, Input.ApplyDeadzone(0.1f, 0.1f, 0.2f));
    }

    [Fact]
    public void FullDeflectionStaysUnitLength()
    {
        Vector2 v = Input.ApplyDeadzone(1f, 0f, 0.2f);
        Assert.Equal(1f, v.X, 5);
        Assert.Equal(0f, v.Y, 5);
    }

    [Fact]
    public void OutputRampsFromZeroAtTheDeadzoneEdge()
    {
        Vector2 v = Input.ApplyDeadzone(0.6f, 0f, 0.2f);
        Assert.Equal(0.5f, v.X, 5);
    }
}
