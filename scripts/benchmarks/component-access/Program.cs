using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using Funke;
using Funke.Interop;

internal static unsafe class Program
{
    [DllImport("ComponentAccessProbe", CallingConvention = CallingConvention.Cdecl)]
    private static extern int InitializeProbe(NativeBindingsStruct* bindings, nuint size);
    [DllImport("ComponentAccessProbe", CallingConvention = CallingConvention.Cdecl)]
    private static extern void CreateProbeEntities(ulong* ids, int count);
    [DllImport("ComponentAccessProbe", CallingConvention = CallingConvention.Cdecl)]
    private static extern void ShutdownProbe();

    private static int Main(string[] args)
    {
        int count = int.Parse(args[1]);
        int samples = args.Length > 2 ? int.Parse(args[2]) : 31;
        NativeBindingsStruct bindings = default;
        if (InitializeProbe(&bindings, (nuint)sizeof(NativeBindingsStruct)) != 1)
            throw new InvalidOperationException("Native binding layout mismatch");
        NativeCallbacks.Bindings = bindings;
        var ids = new ulong[count];
        fixed (ulong* data = ids) CreateProbeEntities(data, count);
        var entities = new Entity[count];
        var wrappers = new Transform2D[count];
        for (int i = 0; i < count; ++i)
        {
            entities[i] = Entity.FromID(ids[i]);
            wrappers[i] = entities[i].GetComponent<Transform2D>() ?? throw new Exception("Missing fixture component");
        }
        var results = new List<object>();
        foreach (string mode in new[] { "has", "get-cached", "write-cached", "get-write" })
        {
            for (int sample = -10; sample < samples; ++sample)
            {
                int found = 0;
                long allocated = GC.GetAllocatedBytesForCurrentThread();
                long start = Stopwatch.GetTimestamp();
                switch (mode)
                {
                    case "has":
                        foreach (Entity entity in entities) if (entity.HasComponent<Transform2D>()) ++found;
                        break;
                    case "get-cached":
                        foreach (Entity entity in entities) if (entity.GetComponent<Transform2D>() != null) ++found;
                        break;
                    case "write-cached":
                        foreach (Transform2D transform in wrappers) transform.LocalTransform += new Vector2(1, 2);
                        break;
                    case "get-write":
                        foreach (Entity entity in entities) entity.GetComponent<Transform2D>()!.LocalTransform += new Vector2(1, 2);
                        break;
                }
                long end = Stopwatch.GetTimestamp();
                allocated = GC.GetAllocatedBytesForCurrentThread() - allocated;
                if (mode is "has" or "get-cached")
                {
                    if (found != count) throw new Exception("Incorrect query result");
                }
                else
                {
                    int iteration = sample + 11 + (mode == "get-write" ? samples + 10 : 0);
                    foreach (Transform2D transform in wrappers)
                    {
                        Vector2 value = transform.LocalTransform;
                        if (value.X != iteration || value.Y != iteration * 2) throw new Exception("Incorrect transform result");
                    }
                }
                if (sample >= 0) results.Add(new { mode, count, sample, ms = Stopwatch.GetElapsedTime(start, end).TotalMilliseconds, allocated });
            }
        }
        Entity.ClearManagedComponentStore();
        ShutdownProbe();
        string core = typeof(Entity).Assembly.Location;
        File.WriteAllText(args[0], JsonSerializer.Serialize(new {
            runtime = Environment.Version.ToString(), coreSha256 = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(core))),
            scope = "Managed API plus real native component bindings; excludes setup, validation, hierarchy propagation and rendering",
            warmups = 10, samples, results
        }, new JsonSerializerOptions { WriteIndented = true }));
        return 0;
    }
}
