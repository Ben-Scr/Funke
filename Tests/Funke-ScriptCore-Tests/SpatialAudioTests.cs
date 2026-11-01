using System;
using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe class SpatialAudioTests
{
    private static readonly float[] s_Packed = new float[10];
    private static int s_Count;
    [Fact]
    public void RolloffValuesMatchNative()
    {
        // Mirrors Funke-Engine/src/Audio/AudioSpatial.hpp; scenes store the integers.
        Assert.Equal(0, (int)AudioRolloff.Logarithmic);
        Assert.Equal(1, (int)AudioRolloff.Linear);
    }

    [Fact]
    public void PackedLayoutsMatchScriptBindings()
    {
        Assert.Equal(7, InternalCalls.AudioSourceSpatialValueCount);
    }

    [Fact]
    public void HrtfOneShotsAppendTheFlagAndLegacyCallsKeepItDisabled()
    {
        NativeBindingsStruct previous = NativeCallbacks.Bindings;
        try
        {
            NativeBindingsStruct bindings = previous;
            bindings.Audio_PlayOneShotAt = &CaptureOneShot;
            NativeCallbacks.Bindings = bindings;
            var clip = new Audio(42);
            clip.PlayOneShotAt(new Vector3(1, 2, 3), hrtf: true, volume: 0.25f, minDistance: 2, maxDistance: 80,
                rolloff: AudioRolloff.Linear, panning: 0.4f, dopplerLevel: 0.5f);
            Assert.Equal(10, s_Count);
            Assert.Equal(new float[] { 1, 2, 3, 0.25f, 2, 80, 1, 0.4f, 0.5f, 1 }, s_Packed);
            clip.PlayOneShotAt(Vector3.Zero);
            Assert.Equal(0, s_Packed[9]);
            Audio.PlayOneShotAt(clip, Vector3.Zero, hrtf: true);
            Assert.Equal(1, s_Packed[9]);
        }
        finally { NativeCallbacks.Bindings = previous; }
    }

    [UnmanagedCallersOnly]
    private static void CaptureOneShot(ulong asset, float* values, int count)
    {
        s_Count = count;
        for (int i = 0; i < Math.Min(count, s_Packed.Length); ++i) s_Packed[i] = values[i];
    }
}
