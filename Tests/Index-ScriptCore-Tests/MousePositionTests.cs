using System.Runtime.InteropServices;
using Index;
using Index.Interop;
using Xunit;

namespace IndexScriptCoreTests;

[CollectionDefinition(Name, DisableParallelization = true)]
public sealed class NativeCallbacksCollection
{
    public const string Name = "Native callbacks";
}

[Collection(NativeCallbacksCollection.Name)]
public unsafe class MousePositionTests
{
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
