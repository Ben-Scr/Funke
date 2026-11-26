using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;
using Funke;

// Diagnostic fixture only. Forced collections are never part of normal engine behavior.
public class MemoryLifecycle : SceneSystem
{
    public const int Revision = 1;
    private static long frames;
    private static long sceneStarts, sceneDestroys;
    private static int sceneTransitions;
    private static readonly string? outputPath = Environment.GetEnvironmentVariable("FUNKE_MEMORY_SAMPLE_OUTPUT");
    private static readonly int collectInterval = int.TryParse(Environment.GetEnvironmentVariable("FUNKE_MEMORY_COLLECT_INTERVAL"), out int seconds) ? seconds : 0;
    private static readonly int requestedTransitions = int.TryParse(Environment.GetEnvironmentVariable("FUNKE_MEMORY_SCENE_CYCLES"), out int cycles) ? cycles : 0;
    private static readonly Stopwatch clock = Stopwatch.StartNew();
    private static int lastSecond = -1;
    public override void OnAwake()
    {
        ++sceneStarts;
        if (Environment.GetEnvironmentVariable("FUNKE_MEMORY_UNCAPPED") == "1")
        {
            Application.RunningInBackground = true;
            Application.VsyncEnabled = false;
            Application.TargetFrameRate = 10000;
        }
    }
    public override void OnUpdate()
    {
        ++frames;
        int second = (int)clock.Elapsed.TotalSeconds;
        if (outputPath != null && second != lastSecond)
        {
            lastSecond = second;
            File.AppendAllText(outputPath, State() + Environment.NewLine);
            if (collectInterval > 0 && second > 0 && second % collectInterval == 0) Collect();
            if (second > 0 && sceneTransitions < requestedTransitions)
            {
                ++sceneTransitions;
                if (SceneManager.LoadScene(sceneTransitions % 2 == 1 ? "Second" : "First") == null)
                    throw new InvalidOperationException("Diagnostic scene transition failed");
            }
        }
    }
    public override void OnDestroy() => ++sceneDestroys;

    [DevCommand("memory.lifecycle.collect")]
    public static string Collect()
    {
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        return "collected";
    }

    [DevCommand("memory.lifecycle.state")]
    public static string State()
    {
        using var process = Process.GetCurrentProcess();
        var memory = GC.GetGCMemoryInfo();
        var values = new Dictionary<string, long>
        {
            ["revision"] = Revision,
            ["frames"] = frames,
            ["sceneStarts"] = sceneStarts,
            ["sceneDestroys"] = sceneDestroys,
            ["sceneTransitions"] = sceneTransitions,
            ["elapsedSeconds"] = (long)clock.Elapsed.TotalSeconds,
            ["privateBytes"] = process.PrivateMemorySize64,
            ["workingSetBytes"] = process.WorkingSet64,
            ["managedLiveEstimateBytes"] = GC.GetTotalMemory(false),
            // Before the first GC these fields are unavailable, not measured zero.
            ["lastGcIndex"] = memory.Index,
            ["managedCommittedBytes"] = memory.Index == 0 ? -1 : memory.TotalCommittedBytes,
            ["lastGcHeapBytes"] = memory.Index == 0 ? -1 : memory.HeapSizeBytes,
            ["lastGcFragmentedBytes"] = memory.Index == 0 ? -1 : memory.FragmentedBytes,
            ["allocatedBytes"] = GC.GetTotalAllocatedBytes(false),
            ["gen0"] = GC.CollectionCount(0),
            ["gen1"] = GC.CollectionCount(1),
            ["gen2"] = GC.CollectionCount(2),
            ["activeCollectibleContexts"] = AssemblyLoadContext.All.Count(c => c.IsCollectible),
            ["loadedAssemblies"] = AppDomain.CurrentDomain.GetAssemblies().Length,
            ["loadedProbeAssemblies"] = AppDomain.CurrentDomain.GetAssemblies().Count(a => a.GetName().Name == "MemoryLifecycle")
        };
        Type host = typeof(EntityScript).Assembly.GetType("Funke.Interop.ScriptInstanceManager")!;
        foreach (string name in new[] { "s_Instances", "s_SceneSystems", "s_SceneSystemAwoken", "s_ClassCache", "s_DataAssets", "s_ReloadStates", "s_UnloadedContexts" })
        {
            object? value = host.GetField(name, BindingFlags.NonPublic | BindingFlags.Static)?.GetValue(null);
            if (value is ICollection collection) values[name] = collection.Count;
            if (name == "s_UnloadedContexts" && value is IEnumerable pending)
            {
                long alive = 0;
                foreach (object entry in pending)
                    if (entry.GetType().GetField("Item1")?.GetValue(entry) is WeakReference reference && reference.IsAlive) ++alive;
                values["pendingContextsAlive"] = alive;
            }
        }
        return JsonSerializer.Serialize(values);
    }
}
