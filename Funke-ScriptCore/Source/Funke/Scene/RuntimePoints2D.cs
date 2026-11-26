using System;
using Funke.Interop;

namespace Funke;

/// <summary>Scene-local runtime entities with a numeric ID and an XY position. Use on the scene thread.</summary>
public static unsafe class RuntimePoints2D
{
    public static int Create(Span<ulong> ids)
    {
        fixed (ulong* p = ids) return NativeCallbacks.Bindings.RuntimePoints_Create(p, ids.Length);
    }

    public static int Translate(ReadOnlySpan<ulong> ids, float dx, float dy)
    {
        fixed (ulong* p = ids) return NativeCallbacks.Bindings.RuntimePoints_Translate(p, ids.Length, dx, dy);
    }

    /// <summary>Writes interleaved X,Y pairs in input order. Missing IDs produce zeroes.</summary>
    public static int ReadPositions(ReadOnlySpan<ulong> ids, Span<float> xy)
    {
        if (xy.Length / 2 < ids.Length) throw new ArgumentException("Expected two floats per ID.", nameof(xy));
        fixed (ulong* p = ids)
        fixed (float* positions = xy)
            return NativeCallbacks.Bindings.RuntimePoints_Read(p, ids.Length, positions);
    }

    public static int CountAlive(ReadOnlySpan<ulong> ids)
    {
        fixed (ulong* p = ids) return NativeCallbacks.Bindings.RuntimePoints_Read(p, ids.Length, null);
    }

    public static int Destroy(ReadOnlySpan<ulong> ids)
    {
        fixed (ulong* p = ids) return NativeCallbacks.Bindings.RuntimePoints_Destroy(p, ids.Length);
    }
}
