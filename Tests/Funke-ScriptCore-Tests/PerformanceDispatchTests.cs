using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using Funke;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public sealed class PerformanceDispatchTests
{
    public sealed class Counter : EntityScript
    {
        public int Awakes, Starts, Updates;
        public override void OnAwake() => ++Awakes;
        public override void OnStart() => ++Starts;
        public override void OnUpdate() => ++Updates;
    }

    [Fact]
    public unsafe void DispatchPreservesLifecycleAndRejectsDestroyedHandles()
    {
        string? output = Environment.GetEnvironmentVariable("FUNKE_DISPATCH_BENCH_OUTPUT");
        using var writer = output == null ? null : new StreamWriter(output, append: false);
        writer?.WriteLine("entities,mode,repeat,sample,cpu_ms,allocated_bytes,gc_pause_ms");
        delegate* unmanaged<byte*, ulong, int> create = &ScriptInstanceManager.CreateScriptInstance;
        delegate* unmanaged<int, void> awake = &ScriptInstanceManager.InvokeAwake;
        delegate* unmanaged<int, void> start = &ScriptInstanceManager.InvokeStart;
        delegate* unmanaged<int, void> update = &ScriptInstanceManager.InvokeUpdate;
        delegate* unmanaged<int, void> destroy = &ScriptInstanceManager.DestroyScriptInstance;
        ScriptInstanceManager.SetCoreAssembly(typeof(Counter).Assembly);
        ScriptInstanceManager.ClearReloadState();
        try {
            foreach (int count in output == null ? new[] { 16 } : new[] { 100, 1000, 10000 }) {
                var handles = new int[count];
                var instances = new Counter[count];
                try {
                    byte[] name = Encoding.UTF8.GetBytes("type:" + typeof(Counter).FullName + "\0");
                    fixed (byte* pointer = name) {
                        for (int i = 0; i < count; ++i) {
                            ulong entity = (ulong)(8000000 + i);
                            handles[i] = create(pointer, entity);
                            Assert.NotEqual(0, handles[i]);
                            instances[i] = (Counter)ScriptInstanceManager.GetScriptInstance(entity, typeof(Counter))!;
                            awake(handles[i]); awake(handles[i]); start(handles[i]); start(handles[i]);
                            Assert.Equal(1, instances[i].Awakes);
                            Assert.Equal(1, instances[i].Starts);
                        }
                    }
                    for (int mode = 0; mode < 2; ++mode) {
                        for (int repeat = 0; repeat < (output == null ? 1 : 3); ++repeat) {
                            int before = instances[0].Updates;
                            int samples = output == null ? 2 : 60;
                            var times = new double[samples];
                            var allocations = new long[samples];
                            var pauses = new double[samples];
                            for (int sample = -10; sample < samples; ++sample) {
                                long allocated = GC.GetAllocatedBytesForCurrentThread();
                                double pause = GC.GetTotalPauseDuration().TotalMilliseconds;
                                long begin = Stopwatch.GetTimestamp();
                                for (int i = 0; i < count; ++i) {
                                    if (mode == 0) instances[i].OnUpdate();
                                    else update(handles[i]);
                                }
                                double milliseconds = Stopwatch.GetElapsedTime(begin).TotalMilliseconds;
                                if (sample >= 0) {
                                    times[sample] = milliseconds;
                                    allocations[sample] = GC.GetAllocatedBytesForCurrentThread() - allocated;
                                    pauses[sample] = GC.GetTotalPauseDuration().TotalMilliseconds - pause;
                                }
                            }
                            foreach (var instance in instances) Assert.Equal(before + samples + 10, instance.Updates);
                            for (int sample = 0; sample < samples; ++sample)
                                writer?.WriteLine(FormattableString.Invariant($"{count},{mode},{repeat},{sample},{times[sample]:R},{allocations[sample]},{pauses[sample]:R}"));
                        }
                    }
                }
                finally {
                    for (int i = 0; i < handles.Length; ++i) {
                        if (handles[i] == 0) continue;
                        destroy(handles[i]);
                        int before = instances[i]?.Updates ?? 0;
                        update(handles[i]);
                        if (instances[i] != null) Assert.Equal(before, instances[i].Updates);
                    }
                }
            }
        }
        finally {
            ScriptInstanceManager.ClearReloadState();
            ScriptInstanceManager.SetCoreAssembly(typeof(EntityScript).Assembly);
        }
    }
}
