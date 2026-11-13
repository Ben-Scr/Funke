using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using Funke;
using Funke.Components;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public sealed unsafe class EntityCommandBufferTests : IDisposable
{
    private readonly NativeBindingsStruct m_Previous = NativeCallbacks.Bindings;
    private readonly delegate* unmanaged<byte*, byte*, int, int> m_PreviousLayout = ComponentTypes.NativeLayoutQuery;
    private static byte[] s_Wire = Array.Empty<byte>();
    private static int s_Playbacks;
    private static int s_PlaybackMode;
    private static int s_ModePlaybacks;
    private static int s_PlaybackError;

    [StructLayout(LayoutKind.Sequential)]
    private struct Payload : IComponent
    {
        internal const string NativeName = "EcbTestPayload";
        public int Value;
    }

    [StructLayout(LayoutKind.Sequential, Size = 65528)]
    private struct LargePayload : IComponent
    {
        internal const string NativeName = "EcbTestLargePayload";
        public byte Value;
    }

    public EntityCommandBufferTests()
    {
        NativeBindingsStruct bindings = m_Previous;
        bindings.Entity_GetComponentSize = &ComponentSize;
        bindings.Component_GetTypeId = &ComponentTypeId;
        bindings.Ecb_Playback = &Playback;
        bindings.Ecb_PlaybackWithMode = &PlaybackWithMode;
        NativeCallbacks.Bindings = bindings;
        ComponentTypes.NativeLayoutQuery = &DynamicLayout;
        s_Wire = Array.Empty<byte>();
        s_Playbacks = 0;
        s_PlaybackMode = -1;
        s_ModePlaybacks = 0;
        s_PlaybackError = 0;
    }

    public void Dispose()
    {
        NativeCallbacks.Bindings = m_Previous;
        ComponentTypes.NativeLayoutQuery = m_PreviousLayout;
    }

    [UnmanagedCallersOnly]
    private static int DynamicLayout(byte* name, byte* output, int capacity)
    {
        ReadOnlySpan<byte> json = "{\"version\":1,\"dynamic\":true}"u8;
        if (output != null && capacity > json.Length)
        {
            json.CopyTo(new Span<byte>(output, json.Length));
            output[json.Length] = 0;
        }
        return json.Length;
    }

    [UnmanagedCallersOnly]
    private static int ComponentSize(byte* name)
    {
        ReadOnlySpan<byte> bytes = MemoryMarshal.CreateReadOnlySpanFromNullTerminated(name);
        if (bytes.SequenceEqual("EcbTestPayload"u8)) return sizeof(Payload);
        if (bytes.SequenceEqual("EcbTestLargePayload"u8)) return sizeof(LargePayload);
        return 0;
    }

    [UnmanagedCallersOnly]
    private static uint ComponentTypeId(byte* name)
        => MemoryMarshal.CreateReadOnlySpanFromNullTerminated(name).SequenceEqual("EcbTestPayload"u8) ? 71u : 72u;

    [UnmanagedCallersOnly]
    private static int Playback(byte* bytes, int length, ulong* ids, int capacity)
        => CapturePlayback(bytes, length, ids, capacity, 0);

    [UnmanagedCallersOnly]
    private static int PlaybackWithMode(byte* bytes, int length, ulong* ids, int capacity, int mode)
    {
        s_ModePlaybacks++;
        return CapturePlayback(bytes, length, ids, capacity, mode);
    }

    private static int CapturePlayback(byte* bytes, int length, ulong* ids, int capacity, int mode)
    {
        s_Wire = new ReadOnlySpan<byte>(bytes, length).ToArray();
        s_Playbacks++;
        s_PlaybackMode = mode;
        if (s_PlaybackError != 0) return s_PlaybackError;
        int count = checked((int)BinaryPrimitives.ReadUInt32LittleEndian(s_Wire));
        for (int i = 0; i < count; i++) ids[i] = 1001ul + (ulong)i;
        return count;
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void PlaybackModesPreserveWireAndCreatedIds(bool parallelRecording)
    {
        using var ecb = new EntityCommandBuffer();
        ecb.CreateWith<Payload>();
        if (parallelRecording) ecb.AsParallelWriter().CreateWith<Payload>();
        else ecb.CreateWith<Payload>();
        Assert.Equal(2, ecb.Playback());
        Assert.Equal(0, s_PlaybackMode);
        Assert.Equal(0, s_ModePlaybacks);
        byte[] expected = (byte[])s_Wire.Clone();
        foreach (EntityCommandBufferPlaybackMode mode in Enum.GetValues<EntityCommandBufferPlaybackMode>())
        {
            Assert.Equal(2, ecb.Playback(mode));
            Assert.Equal((int)mode, s_PlaybackMode);
            Assert.Equal(expected, s_Wire);
            Assert.Equal(1001ul, ecb.GetCreatedEntityId(0));
            Assert.Equal(1002ul, ecb.GetCreatedEntityId(1));
            ulong[] ids = { 0, 0, 987ul };
            Assert.Equal(2, ecb.PlaybackInto(ids, mode));
            Assert.Equal((int)mode, s_PlaybackMode);
            Assert.Equal(expected, s_Wire);
            Assert.Equal(new ulong[] { 1001ul, 1002ul, 987ul }, ids);
            Assert.Equal(1002ul, ecb.GetCreatedEntityId(1));
        }
        Assert.Equal(4, s_ModePlaybacks);
    }

    [Theory]
    [InlineData(-1)]
    [InlineData(3)]
    [InlineData(int.MaxValue)]
    public void InvalidPlaybackModesRejectEmptyAndRecordedBuffers(int value)
    {
        using var ecb = new EntityCommandBuffer();
        var mode = (EntityCommandBufferPlaybackMode)value;
        for (int pass = 0; pass < 2; ++pass)
        {
            Assert.Equal("mode", Assert.Throws<ArgumentOutOfRangeException>(() => ecb.Playback(mode)).ParamName);
            Assert.Equal("mode", Assert.Throws<ArgumentOutOfRangeException>(() => ecb.PlaybackInto(Span<ulong>.Empty, mode)).ParamName);
            if (pass == 0) ecb.CreateWith<Payload>();
        }
        Assert.Equal(0, s_Playbacks);
    }

    [Theory]
    [InlineData(EntityCommandBufferPlaybackMode.Automatic)]
    [InlineData(EntityCommandBufferPlaybackMode.Sequential)]
    [InlineData(EntityCommandBufferPlaybackMode.Parallel)]
    public void PlaybackModeRetainsEmptyAndOutputCapacityContracts(EntityCommandBufferPlaybackMode mode)
    {
        using var ecb = new EntityCommandBuffer();
        Assert.Equal(0, ecb.Playback(mode));
        Assert.Equal(0, ecb.PlaybackInto(Span<ulong>.Empty, mode));
        Assert.Equal(0, s_Playbacks);
        ecb.CreateWith<Payload>();
        Assert.Throws<ArgumentException>(() => ecb.PlaybackInto(Span<ulong>.Empty, mode));
        Assert.Equal(0, s_Playbacks);
        Assert.Equal(1, ecb.Playback(mode));
        Assert.Equal(1, ecb.PlaybackInto(new ulong[1], mode));
        Assert.Equal(1001ul, ecb.GetCreatedEntityId(0));
        ecb.Clear();
        ecb.RemoveComponent<Payload>(Entity.FromID(1234ul));
        Assert.Equal(0, ecb.PlaybackInto(Span<ulong>.Empty, mode));
        Assert.Throws<ArgumentOutOfRangeException>(() => ecb.GetCreatedEntityId(0));
        ecb.Clear();
        Assert.Equal(0, ecb.Playback(mode));
        Assert.Throws<ArgumentOutOfRangeException>(() => ecb.GetCreatedEntityId(0));
    }

    [Fact]
    public void NativeInvalidModeErrorIsReportedWithoutReplacingCreatedIds()
    {
        using var ecb = new EntityCommandBuffer();
        ecb.CreateWith<Payload>();
        Assert.Equal(1, ecb.Playback());
        s_PlaybackError = -9;
        var error = Assert.Throws<InvalidOperationException>(() => ecb.Playback(EntityCommandBufferPlaybackMode.Parallel));
        Assert.Contains("playback mode is invalid", error.Message);
        Assert.Equal(1001ul, ecb.GetCreatedEntityId(0));
    }

    private sealed record Command(byte Opcode, uint EntityIndex, uint TypeId, byte[] Data);

    private static List<Command> ReadCommands()
    {
        uint entityCount = BinaryPrimitives.ReadUInt32LittleEndian(s_Wire);
        uint commandCount = BinaryPrimitives.ReadUInt32LittleEndian(s_Wire.AsSpan(4));
        int cursor = checked(8 + (int)entityCount * 4);
        var commands = new List<Command>();
        for (int i = 0; i < commandCount; i++)
        {
            ReadOnlySpan<byte> command = s_Wire.AsSpan(cursor);
            int size = BinaryPrimitives.ReadUInt16LittleEndian(command.Slice(9));
            commands.Add(new Command(command[0], BinaryPrimitives.ReadUInt32LittleEndian(command.Slice(1)),
                BinaryPrimitives.ReadUInt32LittleEndian(command.Slice(5)), command.Slice(11, size).ToArray()));
            cursor += 11 + size;
        }
        Assert.Equal(s_Wire.Length, cursor);
        return commands;
    }

    [Fact]
    public void ComponentCommandsPreserveLocalAndLiveOrderingAndCopyPayloadsImmediately()
    {
        using var ecb = new EntityCommandBuffer(initialCapacity: 8);
        EntityRef local = ecb.CreateWith<Payload>();
        Entity live = Entity.FromID(0x0123456789ABCDEFul);
        var value = new Payload { Value = 17 };
        ecb.AddComponent(local, in value);
        ecb.RemoveComponent<Payload>(local);
        ecb.AddComponent<Payload>(live);
        value.Value = 29;
        ecb.AddComponent(live, in value);
        value.Value = 99;
        ecb.RemoveComponent<Payload>(live);

        Assert.Equal(1, ecb.Playback());
        Assert.Equal(1001ul, ecb.GetCreatedEntityId(0));
        List<Command> commands = ReadCommands();
        Assert.Equal(new byte[] { 4, 1, 7, 4, 1, 7 }, commands.ConvertAll(c => c.Opcode));
        for (int i = 0; i < commands.Count; i++)
        {
            Assert.Equal(71u, commands[i].TypeId);
            Assert.Equal(i < 3 ? 0u : uint.MaxValue, commands[i].EntityIndex);
        }
        Assert.Empty(commands[0].Data);
        Assert.Equal(17, BinaryPrimitives.ReadInt32LittleEndian(commands[1].Data));
        Assert.Empty(commands[2].Data);
        Assert.Equal(8, commands[3].Data.Length);
        Assert.Equal(12, commands[4].Data.Length);
        Assert.Equal(8, commands[5].Data.Length);
        for (int i = 3; i < commands.Count; i++)
            Assert.Equal(live.ID, BinaryPrimitives.ReadUInt64LittleEndian(commands[i].Data));
        Assert.Equal(29, BinaryPrimitives.ReadInt32LittleEndian(commands[4].Data.AsSpan(8)));
    }

    [Fact]
    public void ParallelMergeRemapsOnlyLocalRefsForEveryComponentOpcode()
    {
        using var ecb = new EntityCommandBuffer(initialCapacity: 8);
        ecb.CreateWith<Payload>();
        EntityCommandBuffer.ParallelWriter writer = ecb.AsParallelWriter();
        Exception? failureA = null, failureB = null;
        void Record(int value, ulong id)
        {
            EntityRef local = writer.Create();
            writer.AddComponent(local, new Payload { Value = value });
            writer.AddComponent(Entity.FromID(id), new Payload { Value = value + 100 });
            writer.RemoveComponent<Payload>(local);
            writer.RemoveComponent<Payload>(Entity.FromID(id));
            writer.AddComponent<Payload>(Entity.FromID(id));
        }
        var a = new Thread(() => { try { Record(1, 41ul); } catch (Exception ex) { failureA = ex; } });
        var b = new Thread(() => { try { Record(2, 42ul); } catch (Exception ex) { failureB = ex; } });
        a.Start();
        b.Start();
        a.Join();
        b.Join();
        Assert.Null(failureA);
        Assert.Null(failureB);

        Assert.Equal(3, ecb.Playback());
        List<Command> commands = ReadCommands();
        Assert.Equal(11, commands.Count);
        Assert.Equal(0u, commands[0].EntityIndex);
        for (int worker = 0; worker < 2; worker++)
        {
            int first = 1 + worker * 5;
            uint index = (uint)(worker + 1);
            int value = BinaryPrimitives.ReadInt32LittleEndian(commands[first].Data);
            ulong id = (ulong)(40 + value);
            Assert.Equal(index, commands[first].EntityIndex);
            Assert.Equal(index, commands[first + 2].EntityIndex);
            Assert.Equal((byte)7, commands[first + 2].Opcode);
            foreach (int offset in new[] { 1, 3, 4 })
            {
                Assert.Equal(uint.MaxValue, commands[first + offset].EntityIndex);
                Assert.Equal(id, BinaryPrimitives.ReadUInt64LittleEndian(commands[first + offset].Data));
            }
            Assert.Equal(value + 100, BinaryPrimitives.ReadInt32LittleEndian(commands[first + 1].Data.AsSpan(8)));
            Assert.Equal((byte)7, commands[first + 3].Opcode);
            Assert.Equal((byte)4, commands[first + 4].Opcode);
        }
        Assert.NotEqual(BinaryPrimitives.ReadInt32LittleEndian(commands[1].Data),
            BinaryPrimitives.ReadInt32LittleEndian(commands[6].Data));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void LiveOnlyCommandsPlayBackAfterClearWithoutReplayingPreviousCommands(bool parallel)
    {
        using var ecb = new EntityCommandBuffer(initialCapacity: 8);
        Entity live = Entity.FromID(1234ul);
        if (parallel) ecb.AsParallelWriter().CreateWith<Payload>();
        else ecb.CreateWith<Payload>();
        Assert.Equal(1, ecb.Playback());
        ecb.Clear();
        Assert.Equal(0, ecb.EntityCount);
        Assert.Equal(0, ecb.CommandCount);
        if (parallel) ecb.AsParallelWriter().RemoveComponent<Payload>(live);
        else ecb.RemoveComponent<Payload>(live);
        Assert.Equal(0, ecb.PlaybackInto(Span<ulong>.Empty));
        Command command = Assert.Single(ReadCommands());
        Assert.Equal((byte)7, command.Opcode);
        Assert.Equal(uint.MaxValue, command.EntityIndex);
        Assert.Equal(live.ID, BinaryPrimitives.ReadUInt64LittleEndian(command.Data));
        Assert.Equal(2, s_Playbacks);
    }

    [Fact]
    public void MissingLocalRefsAndInvalidEntityArgumentsDoNotRecordCommands()
    {
        using var ecb = new EntityCommandBuffer();
        var data = new Payload();
        Assert.Throws<ArgumentException>(() => ecb.AddComponent(default(EntityRef), data));
        Assert.Throws<ArgumentException>(() => ecb.AddComponent<Payload>(default(EntityRef)));
        Assert.Throws<ArgumentException>(() => ecb.RemoveComponent<Payload>(default(EntityRef)));
        Assert.Throws<ArgumentNullException>(() => ecb.AddComponent<Payload>((Entity)null!));
        Assert.Throws<ArgumentNullException>(() => ecb.RemoveComponent<Payload>((Entity)null!));
        Assert.Throws<InvalidOperationException>(() => ecb.RemoveComponent<Payload>(Entity.Invalid));
        Assert.Throws<InvalidOperationException>(() => ecb.AddComponent<Payload>(Entity.FromPrefabGUID(123)));
        EntityRef mainLocal = ecb.Create();
        EntityCommandBuffer.ParallelWriter writer = ecb.AsParallelWriter();
        Assert.Throws<ArgumentException>(() => writer.AddComponent(mainLocal, data));
        Assert.Throws<ArgumentException>(() => writer.AddComponent<Payload>(mainLocal));
        Assert.Throws<ArgumentException>(() => writer.RemoveComponent<Payload>(mainLocal));
        Assert.Equal(0, ecb.CommandCount);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void LiveReferenceBytesCountTowardsPayloadLimit(bool parallel)
    {
        using var ecb = new EntityCommandBuffer();
        var data = new LargePayload { Value = 81 };
        Entity live = Entity.FromID(1234ul);
        if (parallel)
        {
            var writer = ecb.AsParallelWriter();
            Assert.Throws<ArgumentException>(() => writer.AddComponent(live, data));
            writer.AddComponent(writer.Create(), data);
        }
        else
        {
            Assert.Throws<ArgumentException>(() => ecb.AddComponent(live, data));
            ecb.AddComponent(ecb.Create(), data);
        }
        Assert.Equal(1, ecb.CommandCount);
        Assert.Equal(1, ecb.Playback());
        Command command = Assert.Single(ReadCommands());
        Assert.Equal(65528, command.Data.Length);
        Assert.Equal((byte)81, command.Data[0]);
    }

    [Theory]
    [InlineData(0u)]
    [InlineData(73u)]
    public void CopyAndRemapPreservesEveryByteExceptLocalIndices(uint baseOffset)
    {
        using var ecb = new EntityCommandBuffer();
        EntityRef local = ecb.CreateWith<Payload>();
        EntityRef live = Entity.FromID(0x0123456789ABCDEFul).ToCommandBufferRef();
        ecb.AddComponent(local, new LargePayload { Value = 81 });
        ecb.AddComponent(live, new Payload { Value = 123 });
        ecb.SetEnabled(local, true);
        ecb.SetEnabled(live, false);
        ecb.RemoveComponent<Payload>(local);
        ecb.RemoveComponent<Payload>(live);
        ecb.Destroy(local);
        ecb.Destroy(live);
        ecb.Instantiate(7654321ul);
        Assert.Equal(2, ecb.Playback());

        byte[] source = s_Wire.AsSpan(8 + 2 * sizeof(uint)).ToArray();
        byte[] original = (byte[])source.Clone();
        byte[] expected = (byte[])source.Clone();
        for (int cursor = 0; cursor < expected.Length;)
        {
            uint index = BinaryPrimitives.ReadUInt32LittleEndian(expected.AsSpan(cursor + 1));
            if (index != uint.MaxValue)
                BinaryPrimitives.WriteUInt32LittleEndian(expected.AsSpan(cursor + 1), index + baseOffset);
            cursor += 11 + BinaryPrimitives.ReadUInt16LittleEndian(expected.AsSpan(cursor + 9));
        }
        byte[] destination = new byte[source.Length + 2];
        destination[0] = 0xA5;
        destination[^1] = 0x5A;
        fixed (byte* target = destination)
            EcbWire.CopyAndRemapCommands(target + 1, source, source.Length, baseOffset);
        Assert.Equal(expected, destination.AsSpan(1, source.Length).ToArray());
        Assert.Equal(original, source);
        Assert.Equal((byte)0xA5, destination[0]);
        Assert.Equal((byte)0x5A, destination[^1]);
    }

    [Fact]
    public void RepeatedPlaybackPreservesLiveOnlyWorkerRecordsAfterMainCreations()
    {
        using var ecb = new EntityCommandBuffer();
        ecb.CreateWith<Payload>();
        EntityRef live = Entity.FromID(98765ul).ToCommandBufferRef();
        EntityCommandBuffer.ParallelWriter writer = ecb.AsParallelWriter();
        writer.AddComponent(live, new Payload { Value = 41 });
        writer.RemoveComponent<Payload>(live);
        writer.SetEnabled(live, false);
        Assert.Equal(1, ecb.Playback());
        byte[] first = (byte[])s_Wire.Clone();
        Assert.Equal(1, ecb.Playback());
        Assert.Equal(first, s_Wire);
        List<Command> commands = ReadCommands();
        Assert.Equal(4, commands.Count);
        Assert.Equal(0u, commands[0].EntityIndex);
        Assert.Equal(new byte[] { 1, 7, 6 }, commands.GetRange(1, 3).ConvertAll(command => command.Opcode));
        for (int i = 1; i < commands.Count; ++i)
            Assert.Equal(uint.MaxValue, commands[i].EntityIndex);
        Assert.Equal(live.ID, BinaryPrimitives.ReadUInt64LittleEndian(commands[1].Data));
        Assert.Equal(live.ID, BinaryPrimitives.ReadUInt64LittleEndian(commands[2].Data));
        Assert.Equal(live.ID, BinaryPrimitives.ReadUInt64LittleEndian(commands[3].Data.AsSpan(1)));
    }

    [Fact]
    public void ClearReusesWarmParallelStorageWithoutAllocations()
    {
        using var ecb = new EntityCommandBuffer();
        ecb.AsParallelWriter().CreateWith<Payload>();
        Assert.Equal(1, ecb.Playback());
        ecb.Clear();
        long before = GC.GetAllocatedBytesForCurrentThread();
        for (int i = 0; i < 32; ++i) ecb.Clear();
        long allocated = GC.GetAllocatedBytesForCurrentThread() - before;
        Assert.Equal(0L, allocated);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void BulkCreationPreservesAllAritiesOffsetsAndFollowingCommands(bool parallel)
    {
        using var ecb = new EntityCommandBuffer(initialCapacity: 8);
        var writer = ecb.AsParallelWriter();
        for (int pass = 0; pass < 2; ++pass)
        {
            ecb.Clear();
            ecb.CreateWith<Payload>();
            EntityRef[] output = new EntityRef[4];
            int expectedCommands = 1;
            for (int arity = 1; arity <= 8; ++arity)
            {
                output[3] = new EntityRef(999u);
                if (parallel)
                {
                    switch (arity)
                    {
                        case 1: writer.CreateEntitiesWith<Payload>(3, output); break;
                        case 2: writer.CreateEntitiesWith<Payload, Payload>(3, output); break;
                        case 3: writer.CreateEntitiesWith<Payload, Payload, Payload>(3, output); break;
                        case 4: writer.CreateEntitiesWith<Payload, Payload, Payload, Payload>(3, output); break;
                        case 5: writer.CreateEntitiesWith<Payload, Payload, Payload, Payload, Payload>(3, output); break;
                        case 6: writer.CreateEntitiesWith<Payload, Payload, Payload, Payload, Payload, Payload>(3, output); break;
                        case 7: writer.CreateEntitiesWith<Payload, Payload, Payload, Payload, Payload, Payload, Payload>(3, output); break;
                        case 8: writer.CreateEntitiesWith<Payload, Payload, Payload, Payload, Payload, Payload, Payload, Payload>(3, output); break;
                    }
                    writer.AddComponent(output[2], new Payload { Value = arity });
                }
                else
                {
                    switch (arity)
                    {
                        case 1: ecb.CreateEntitiesWith<Payload>(3, output); break;
                        case 2: ecb.CreateEntitiesWith<Payload, Payload>(3, output); break;
                        case 3: ecb.CreateEntitiesWith<Payload, Payload, Payload>(3, output); break;
                        case 4: ecb.CreateEntitiesWith<Payload, Payload, Payload, Payload>(3, output); break;
                        case 5: ecb.CreateEntitiesWith<Payload, Payload, Payload, Payload, Payload>(3, output); break;
                        case 6: ecb.CreateEntitiesWith<Payload, Payload, Payload, Payload, Payload, Payload>(3, output); break;
                        case 7: ecb.CreateEntitiesWith<Payload, Payload, Payload, Payload, Payload, Payload, Payload>(3, output); break;
                        case 8: ecb.CreateEntitiesWith<Payload, Payload, Payload, Payload, Payload, Payload, Payload, Payload>(3, output); break;
                    }
                    ecb.AddComponent(output[2], new Payload { Value = arity });
                }
                for (int i = 0; i < 3; ++i)
                    Assert.Equal((uint)((parallel ? 0 : 1) + (arity - 1) * 3 + i), output[i].Index);
                Assert.Equal(999u, output[3].Index);
                expectedCommands += 3 * arity + 1;
            }
            Assert.Equal(25, ecb.EntityCount);
            Assert.Equal(expectedCommands, ecb.CommandCount);
            Assert.Equal(25, ecb.Playback());
            List<Command> commands = ReadCommands();
            Assert.Equal(expectedCommands, commands.Count);
            int cursor = 1;
            for (int arity = 1; arity <= 8; ++arity)
            {
                for (int i = 0; i < 3; ++i)
                    for (int component = 0; component < arity; ++component)
                    {
                        Command command = commands[cursor++];
                        Assert.Equal((byte)4, command.Opcode);
                        Assert.Equal((uint)(1 + (arity - 1) * 3 + i), command.EntityIndex);
                        Assert.Empty(command.Data);
                    }
                Command payload = commands[cursor++];
                Assert.Equal((uint)(arity * 3), payload.EntityIndex);
                Assert.Equal(arity, BinaryPrimitives.ReadInt32LittleEndian(payload.Data));
            }
        }
    }

    [Fact]
    public void ThreadSlotsKeepInterleavedBuffersIndependent()
    {
        using var first = new EntityCommandBuffer();
        using var second = new EntityCommandBuffer();
        var a = first.AsParallelWriter();
        var b = second.AsParallelWriter();
        for (int pass = 0; pass < 3; ++pass)
        {
            first.Clear(); second.Clear();
            for (int i = 0; i < 9; ++i)
            {
                a.AddComponent(a.Create(), new Payload { Value = i });
                b.AddComponent(b.Create(), new Payload { Value = 100 + i });
            }
            Assert.Equal(9, first.Playback());
            Assert.Equal(8, BinaryPrimitives.ReadInt32LittleEndian(ReadCommands()[8].Data));
            Assert.Equal(9, second.Playback());
            Assert.Equal(108, BinaryPrimitives.ReadInt32LittleEndian(ReadCommands()[8].Data));
        }
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void BulkCreationRejectsInvalidLengthsWithoutChangingTheBuffer(bool parallel)
    {
        using var ecb = new EntityCommandBuffer();
        var writer = ecb.AsParallelWriter();
        if (parallel)
        {
            writer.CreateEntitiesWith<Payload>(0, Span<EntityRef>.Empty);
            Assert.Throws<ArgumentOutOfRangeException>(() => writer.CreateEntitiesWith<Payload>(-1, Span<EntityRef>.Empty));
            Assert.Throws<ArgumentException>(() => writer.CreateEntitiesWith<Payload>(2, new EntityRef[1]));
        }
        else
        {
            ecb.CreateEntitiesWith<Payload>(0, Span<EntityRef>.Empty);
            Assert.Throws<ArgumentOutOfRangeException>(() => ecb.CreateEntitiesWith<Payload>(-1, Span<EntityRef>.Empty));
            Assert.Throws<ArgumentException>(() => ecb.CreateEntitiesWith<Payload>(2, new EntityRef[1]));
        }
        Assert.Equal(0, ecb.EntityCount);
        Assert.Equal(0, ecb.CommandCount);
        Assert.Equal(0, ecb.Playback());
    }

    [Fact]
    public void WorkerCacheDoesNotRetainAnAbandonedRecorder()
    {
        (WeakReference recorder, WeakReference slot) = RecordAndAbandon();
        GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
        Assert.False(recorder.IsAlive);
        Assert.False(slot.IsAlive);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static (WeakReference, WeakReference) RecordAndAbandon()
    {
        var ecb = new EntityCommandBuffer();
        ecb.AsParallelWriter().CreateWith<Payload>();
        return (new WeakReference(ecb), new WeakReference(ecb.GetOrCreateSlotForCurrentThread()));
    }

    [Fact]
    public void DisposeReleasesStorageCachedOnAnotherLiveThread()
    {
        using var ecb = new EntityCommandBuffer();
        var writer = ecb.AsParallelWriter();
        using var ready = new ManualResetEventSlim();
        using var finish = new ManualResetEventSlim();
        WeakReference? slot = null;
        var worker = new Thread(() =>
        {
            slot = RecordWorker(ecb, writer);
            ready.Set();
            finish.Wait();
        });
        worker.Start();
        try
        {
            Assert.True(ready.Wait(TimeSpan.FromSeconds(10)));
            Assert.Equal(1, ecb.Playback());
            ecb.Dispose();
            GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect();
            Assert.False(slot!.IsAlive);
        }
        finally { finish.Set(); worker.Join(); }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference RecordWorker(EntityCommandBuffer ecb, EntityCommandBuffer.ParallelWriter writer)
    {
        writer.CreateWith<Payload>();
        return new WeakReference(ecb.GetOrCreateSlotForCurrentThread());
    }

    [Fact]
    public void DisposeReleasesWorkerStorageWhileRecorderRemainsAlive()
    {
        var ecb = new EntityCommandBuffer();
        WeakReference worker = RecordAndDispose(ecb);
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        Assert.False(worker.IsAlive);
        GC.KeepAlive(ecb);
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference RecordAndDispose(EntityCommandBuffer ecb)
    {
        ecb.AsParallelWriter().CreateWith<Payload>();
        ecb.Playback();
        var worker = new WeakReference(ecb.GetOrCreateSlotForCurrentThread());
        ecb.Dispose();
        return worker;
    }
}
