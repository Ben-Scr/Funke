using System;
using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe class DebugStatsTests
{
    private static int calls;
    private static bool available;

    [UnmanagedCallersOnly]
    private static int Snapshot(void* buffer, int size)
    {
        ++calls;
        if (buffer == null) return size == 0 && available ? 1 : 0;
        if (size != 360) return 0;
        // Deliberately write even on failure: the managed API must discard a rejected payload.
        var words = new Span<ulong>(buffer, 45);
        words.Clear();
        words[0] = (ulong)(DebugMetric.CompletedFrame | DebugMetric.ProcessMemory);
        words[1] = 123;
        words[2] = (ulong)BitConverter.DoubleToInt64Bits(20.0);
        words[8] = 5_000_000_000;
        words[25] = 42;
        words[44] = 7;
        return available ? 1 : 0;
    }

    [Fact]
    public void UninitializedHostIsUnavailableWithoutCallingNullPointers()
    {
        var previous = NativeCallbacks.Bindings;
        try
        {
            NativeCallbacks.Bindings = default;
            Assert.False(DebugStats.IsAvailable);
            Assert.False(DebugStats.TryGetSnapshot(out var snapshot));
            Assert.Equal(default, snapshot);
        }
        finally { NativeCallbacks.Bindings = previous; }
    }

    [Fact]
    public void SnapshotUsesOneCallAndPreservesLargeCountersAndDoublePrecision()
    {
        var previous = NativeCallbacks.Bindings;
        try
        {
            NativeCallbacks.Bindings.DebugStats_GetSnapshot = &Snapshot;
            available = true;
            calls = 0;
            Assert.True(DebugStats.IsAvailable);
            Assert.Equal(1, calls);
            Assert.True(DebugStats.TryGetSnapshot(out var value));
            Assert.Equal(2, calls);
            Assert.Equal(123UL, value.FrameNumber);
            Assert.Equal(50.0, value.Fps);
            Assert.Equal(5_000_000_000UL, value.Triangles);
            Assert.Equal(42UL, value.EntityCount);
            Assert.Equal(7UL, value.LoadedDataAssets);
            Assert.True(value.Has(DebugMetric.ProcessMemory));
            Assert.False(value.Has(DebugMetric.RenderThreadTiming));
            Assert.False(value.Has(DebugMetric.Lights2D));
            Assert.Equal(360, sizeof(DebugSnapshot));
            Assert.Equal(352, Marshal.OffsetOf<DebugSnapshot>(nameof(DebugSnapshot.LoadedDataAssets)).ToInt32());
        }
        finally { NativeCallbacks.Bindings = previous; }
    }

    [Fact]
    public void UnavailableNativeHostCannotLeakRejectedData()
    {
        var previous = NativeCallbacks.Bindings;
        try
        {
            NativeCallbacks.Bindings.DebugStats_GetSnapshot = &Snapshot;
            available = false;
            Assert.False(DebugStats.IsAvailable);
            Assert.False(DebugStats.TryGetSnapshot(out var value));
            Assert.Equal(default, value);
        }
        finally { NativeCallbacks.Bindings = previous; }
    }
}
