using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

public class SpatialAudioTests
{
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
        Assert.Equal(6, InternalCalls.AudioSourceSpatialValueCount);
    }
}
