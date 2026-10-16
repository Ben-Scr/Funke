using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Funke.Interop;
using Xunit;

namespace Funke.Tests;

public class StructFieldLayoutTests
{
    private enum Small : byte { A, B }

    [StructLayout(LayoutKind.Sequential)]
    private struct Mixed
    {
        public bool Alive;      // 1 byte in managed layout (4 when marshalled)
        public float Hp;
        public Small Kind;
        public long Score;
        public Vector2 Vel;
        public short Level;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct Overlay
    {
        [FieldOffset(0)] public int Raw;
        [FieldOffset(0)] public float AsFloat;
        [FieldOffset(8)] public byte Tag;
    }

#pragma warning disable CS0649 // only the layout matters
    private struct Unattributed { public int A; public int B; } // C# structs default to Sequential
#pragma warning restore CS0649

    [Fact]
    public void Sequential_offsets_match_the_runtime_layout()
    {
        var slots = StructFieldLayout.Compute(typeof(Mixed));
        Assert.NotNull(slots);
        unsafe
        {
            Mixed probe = default;
            byte* basePtr = (byte*)&probe;
            Assert.Equal((int)((byte*)&probe.Hp - basePtr), slots!["Hp"].Offset);
            Assert.Equal((int)((byte*)&probe.Kind - basePtr), slots["Kind"].Offset);
            Assert.Equal((int)((byte*)&probe.Score - basePtr), slots["Score"].Offset);
            Assert.Equal((int)((byte*)&probe.Vel - basePtr), slots["Vel"].Offset);
            Assert.Equal((int)((byte*)&probe.Vel.Y - basePtr), slots["Vel.Y"].Offset);
            Assert.Equal((int)((byte*)&probe.Level - basePtr), slots["Level"].Offset);
        }
        Assert.Equal(1, slots["Alive"].Size);
        Assert.Equal(8, slots["Vel"].Size);
        Assert.Equal(Unsafe.SizeOf<Mixed>(), StructFieldLayout.RuntimeSize(typeof(Mixed)));
    }

    [Fact]
    public void Explicit_offsets_come_from_the_attributes()
    {
        var slots = StructFieldLayout.Compute(typeof(Overlay));
        Assert.NotNull(slots);
        Assert.Equal(0, slots!["AsFloat"].Offset);
        Assert.Equal(8, slots["Tag"].Offset);
    }

    [Fact]
    public void Default_struct_layout_is_sequential()
    {
        var slots = StructFieldLayout.Compute(typeof(Unattributed));
        Assert.NotNull(slots);
        Assert.Equal(4, slots!["B"].Offset);
    }
}
