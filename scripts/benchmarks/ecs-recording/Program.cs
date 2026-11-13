using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using System.Threading.Tasks;
using Funke;
using Funke.Components;
using Funke.Interop;

internal static unsafe class Program
{
    [StructLayout(LayoutKind.Sequential, Size = 64)]
    private struct First : IComponent { internal const string NativeName = "RecordingFirst"; public int Value; }
    [StructLayout(LayoutKind.Sequential, Size = 64)]
    private struct Second : IComponent { internal const string NativeName = "RecordingSecond"; public int Value; }

    private static int s_Count;
    private static bool s_Validate;

    [UnmanagedCallersOnly]
    private static int Size(byte* name) => 64;
    [UnmanagedCallersOnly]
    private static uint TypeId(byte* name)
        => MemoryMarshal.CreateReadOnlySpanFromNullTerminated(name).SequenceEqual("RecordingFirst"u8) ? 71u : 72u;
    [UnmanagedCallersOnly]
    private static int Layout(byte* name, byte* output, int capacity)
    {
        ReadOnlySpan<byte> json = "{\"version\":1,\"dynamic\":true}"u8;
        if (output != null && capacity > json.Length) { json.CopyTo(new Span<byte>(output, json.Length)); output[json.Length] = 0; }
        return json.Length;
    }
    [UnmanagedCallersOnly]
    private static int Playback(byte* bytes, int length, ulong* ids, int capacity)
    {
        int count = *(int*)bytes;
        if (count != s_Count || capacity < count || *(int*)(bytes + 4) != count * 2) return -7;
        if (s_Validate)
        {
            var seen = new byte[count];
            int cursor = 8 + count * 4;
            for (int i = 0; i < count * 2; ++i)
            {
                if (cursor + 11 > length) return -1;
                uint entity = System.Runtime.CompilerServices.Unsafe.ReadUnaligned<uint>(bytes + cursor + 1);
                uint type = System.Runtime.CompilerServices.Unsafe.ReadUnaligned<uint>(bytes + cursor + 5);
                ushort size = System.Runtime.CompilerServices.Unsafe.ReadUnaligned<ushort>(bytes + cursor + 9);
                if (entity >= count || (type != 71 && type != 72) || (bytes[cursor] != 1 && bytes[cursor] != 4)) return -7;
                byte bit = type == 71 ? (byte)1 : (byte)2;
                if ((seen[entity] & bit) != 0 || cursor + 11 + size > length) return -7;
                seen[entity] |= bit;
                if (size != 0 && (size != 64 || System.Runtime.CompilerServices.Unsafe.ReadUnaligned<int>(bytes + cursor + 11) != (type == 71 ? 17 : 29))) return -7;
                cursor += 11 + size;
            }
            if (cursor != length || seen.Any(bits => bits != 3)) return -7;
        }
        for (int i = 0; i < count; ++i) ids[i] = (ulong)i + 1;
        return count;
    }

    private static void Main(string[] args)
    {
        string output = args[0];
        s_Count = args.Length > 1 ? int.Parse(args[1]) : 100_000;
        int samples = args.Length > 2 ? int.Parse(args[2]) : 21;
        NativeCallbacks.Bindings = new NativeBindingsStruct { Entity_GetComponentSize = &Size, Component_GetTypeId = &TypeId, Ecb_Playback = &Playback };
        ComponentTypes.NativeLayoutQuery = &Layout;
        var results = new List<object>();
        foreach (string mode in new[] { "MainDefault", "WriterDefault", "WorkersDefault", "MainPayload", "WriterPayload", "WorkersPayload", "MainBulk", "WorkersBulk" })
        {
            using var ecb = new EntityCommandBuffer(s_Count * 160);
            var writer = ecb.AsParallelWriter();
            int workers = mode.StartsWith("Workers") ? Math.Min(4, Environment.ProcessorCount) : 1;
            bool mainDefault = mode == "MainDefault", mainPayload = mode == "MainPayload";
            bool payload = mode.EndsWith("Payload", StringComparison.Ordinal);
            var refs = Enumerable.Range(0, workers).Select(_ => new EntityRef[s_Count]).ToArray();
            Action<int> recordRange = worker =>
            {
                int begin = s_Count * worker / workers, end = s_Count * (worker + 1) / workers;
                if (mode == "MainBulk") { ecb.CreateEntitiesWith<First, Second>(end - begin, refs[worker]); return; }
                if (mode == "WorkersBulk") { writer.CreateEntitiesWith<First, Second>(end - begin, refs[worker]); return; }
                for (int i = begin; i < end; ++i)
                {
                    if (mainDefault) ecb.CreateWith<First, Second>();
                    else if (mainPayload)
                    {
                        var entity = ecb.Create(); ecb.AddComponent(entity, new First { Value = 17 }); ecb.AddComponent(entity, new Second { Value = 29 });
                    }
                    else if (payload)
                    {
                        var entity = writer.Create(); writer.AddComponent(entity, new First { Value = 17 }); writer.AddComponent(entity, new Second { Value = 29 });
                    }
                    else writer.CreateWith<First, Second>();
                }
            };
            var recording = new List<double>(); var playback = new List<double>(); var allocated = new List<long>();
            for (int sample = -10; sample < samples; ++sample)
            {
                ecb.Clear();
                long bytes = GC.GetTotalAllocatedBytes(true), start = Stopwatch.GetTimestamp();
                if (workers == 1) recordRange(0); else Parallel.For(0, workers, recordRange);
                long recorded = Stopwatch.GetTimestamp();
                if (ecb.Playback() != s_Count) throw new InvalidOperationException("Playback count");
                long played = Stopwatch.GetTimestamp();
                long allocation = GC.GetTotalAllocatedBytes(true) - bytes;
                if (sample >= 0) { recording.Add(Stopwatch.GetElapsedTime(start, recorded).TotalMilliseconds); playback.Add(Stopwatch.GetElapsedTime(recorded, played).TotalMilliseconds); allocated.Add(allocation); }
            }
            s_Validate = true;
            try { if (ecb.Playback() != s_Count) throw new InvalidOperationException("Invalid wire"); }
            finally { s_Validate = false; }
            results.Add(new { mode, workers, recordingMs = recording, playbackMs = playback, allocatedBytes = allocated });
            Console.WriteLine($"{mode}: recording {Median(recording):F4} ms; wire playback {Median(playback):F4} ms");
        }
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output))!);
        File.WriteAllText(output, JsonSerializer.Serialize(new { scope = "Managed recording and wire assembly only; native playback is a validating stub, not scene creation", count = s_Count, samples, warmups = 10, coreSha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(typeof(EntityCommandBuffer).Assembly.Location))), results }, new JsonSerializerOptions { WriteIndented = true }));
    }

    private static double Median(List<double> values) => values.Order().ElementAt(values.Count / 2);
}
