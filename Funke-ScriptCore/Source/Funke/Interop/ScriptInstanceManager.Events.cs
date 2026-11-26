using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Reflection;
using System.Linq;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Threading;
using Funke;
using Funke.Coroutines;

namespace Funke.Interop;

internal static partial class ScriptInstanceManager
{


    private static void DispatchToScripts(Action<EntityScript> invoke, string eventName)
    {
        foreach (var data in SnapshotInstances())
        {
            try { invoke(data.Instance); }
            catch (Exception ex) { ScriptExceptionLog.Report(eventName, ex); }
        }
    }

    private static void DispatchToSceneSystems(Action<SceneSystem> invoke, string eventName)
    {
        foreach (var system in new List<SceneSystem>(s_SceneSystems.Values))
        {
            try { invoke(system); }
            catch (Exception ex) { ScriptExceptionLog.Report(eventName, ex); }
        }
    }

    private static void DispatchToGlobalSystems(Action<GlobalSystem> invoke, string eventName)
    {
        foreach (var system in new List<GlobalSystem>(s_GlobalSystems.Values))
        {
            try { invoke(system); }
            catch (Exception ex) { ScriptExceptionLog.Report(eventName, ex); }
        }
    }


    private static Scene SceneFromName(string name) => new Scene { Name = name };

    private static unsafe string PtrToString(byte* value)
    {
        return Marshal.PtrToStringUTF8((IntPtr)value) ?? "";
    }

    // ── Input / Window / Scene / Application event raisers ──────────────
    // Each [UnmanagedCallersOnly] method below is a reverse-pinvoke entry point: a
    // managed exception that unwinds past it into the native caller is FATAL (the
    // CLR fail-fasts the whole process). These raisers fan out to public
    // `event Action<>`s that user scripts subscribe to, and those events have no
    // per-subscriber guard — so a bug in a game handler (e.g. an NRE in OnMouseDown)
    // must be caught and logged here, never allowed to escape. The try/catch is
    // inlined per method rather than via a shared Action helper to stay
    // allocation-free in the per-frame input path (RaiseMouseMove). Log.* is itself
    // boundary-safe (Log.RaiseLogMessage swallows throwing subscribers).

    [UnmanagedCallersOnly]
    public static unsafe void PlatformEvent(int kind, byte* channel, int channelBytes, byte* message, int messageBytes)
    {
        try
        {
            switch (kind)
            {
                case 0: Window.RaiseMaximize(); break;
                case 1: Window.RaiseMinimize(); break;
                case 2: Window.RaiseRestore(); break;
                case 3: AndroidBridge.Simulate(System.Text.Encoding.UTF8.GetString(channel, channelBytes),
                    System.Text.Encoding.UTF8.GetString(message, messageBytes)); break;
            }
        }
        catch (Exception ex) { ScriptExceptionLog.Report("Platform event handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static void RaiseApplicationStart()
    {
        try
        {
            Application.RaiseApplicationStart();
            DispatchToScripts(script => script.OnApplicationStart(), nameof(EntityScript.OnApplicationStart));
        }
        catch (Exception ex) { ScriptExceptionLog.Report("Application.Started handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static void RaiseApplicationPaused()
    {
        try
        {
            Application.RaiseApplicationPaused();
            DispatchToScripts(script => script.OnApplicationPaused(), nameof(EntityScript.OnApplicationPaused));
            DispatchToSceneSystems(system => system.OnApplicationPaused(), nameof(SceneSystem.OnApplicationPaused));
            DispatchToGlobalSystems(system => system.OnApplicationPaused(), nameof(GlobalSystem.OnApplicationPaused));
        }
        catch (Exception ex) { ScriptExceptionLog.Report("Application.Paused handler", ex); }

        // Phones may end a backgrounded app without a quit callback; persist what the handlers changed.
        PlayerPrefs.SaveQuietly();
    }

    [UnmanagedCallersOnly]
    public static void RaiseApplicationQuit()
    {
        try
        {
            Application.RaiseApplicationQuit();
            DispatchToScripts(script => script.OnApplicationQuit(), nameof(EntityScript.OnApplicationQuit));
            DispatchToSceneSystems(system => system.OnApplicationQuit(), nameof(SceneSystem.OnApplicationQuit));
            DispatchToGlobalSystems(system => system.OnApplicationQuit(), nameof(GlobalSystem.OnApplicationQuit));
        }
        catch (Exception ex) { ScriptExceptionLog.Report("Application.Quitting handler", ex); }

        // After the quit handlers so values they set are persisted too.
        PlayerPrefs.SaveQuietly();
    }

    [UnmanagedCallersOnly]
    public static void RaiseFocusChanged(int focused)
    {
        try
        {
            bool isFocused = focused != 0;
            Window.RaiseFocusChanged(isFocused);
            DispatchToScripts(script => script.OnFocusChanged(isFocused), nameof(EntityScript.OnFocusChanged));
            DispatchToSceneSystems(system => system.OnFocusChanged(isFocused), nameof(SceneSystem.OnFocusChanged));
            DispatchToGlobalSystems(system => system.OnFocusChanged(isFocused), nameof(GlobalSystem.OnFocusChanged));
        }
        catch (Exception ex) { ScriptExceptionLog.Report("Window.FocusChanged handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static void RaiseKeyDown(int key)
    {
        try { Input.RaiseKeyDown((KeyCode)key); }
        catch (Exception ex) { ScriptExceptionLog.Report("Input.KeyDown handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static void RaiseKeyUp(int key)
    {
        try { Input.RaiseKeyUp((KeyCode)key); }
        catch (Exception ex) { ScriptExceptionLog.Report("Input.KeyUp handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static void RaiseEnterChar(uint codepoint)
    {
        if (codepoint > char.MaxValue) return;
        char c = (char)codepoint;
        if (char.IsControl(c)) return;
        try { Input.RaiseCharEntered(c); }
        catch (Exception ex) { ScriptExceptionLog.Report("Input.CharEntered handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static void RaiseMouseDown(int button)
    {
        try { Input.RaiseMouseDown((MouseButton)button); }
        catch (Exception ex) { ScriptExceptionLog.Report("Input.MouseDown handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static void RaiseMouseUp(int button)
    {
        try { Input.RaiseMouseUp((MouseButton)button); }
        catch (Exception ex) { ScriptExceptionLog.Report("Input.MouseUp handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static void RaiseMouseScroll(float delta)
    {
        try { Input.RaiseMouseScroll(delta); }
        catch (Exception ex) { ScriptExceptionLog.Report("Input.MouseScroll handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static void RaiseMouseMove(float x, float y)
    {
        try { Input.RaiseMouseMove(new Vector2(x, y)); }
        catch (Exception ex) { ScriptExceptionLog.Report("Input.MouseMove handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static unsafe void RaiseBeforeSceneLoaded(byte* sceneName)
    {
        try { SceneManager.RaiseBeforeSceneLoaded(PtrToString(sceneName)); }
        catch (Exception ex) { ScriptExceptionLog.Report("SceneManager.BeforeSceneLoaded handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static unsafe void RaiseSceneLoaded(byte* sceneName)
    {
        try { SceneManager.RaiseSceneLoaded(PtrToString(sceneName)); }
        catch (Exception ex) { ScriptExceptionLog.Report("SceneManager.SceneLoaded handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static unsafe void SceneLoadCompleted(ulong operationId, int status, byte* sceneName, ulong sceneGuid)
    {
        try { SceneManager.CompleteAsyncLoad(operationId, status, PtrToString(sceneName), sceneGuid); }
        catch (Exception ex) { ScriptExceptionLog.Report("SceneManager.LoadSceneAsync completion", ex); }
    }

    [UnmanagedCallersOnly]
    public static void PumpSceneLoadRequests()
    {
        try { SceneManager.PumpSceneLoadRequests(); }
        catch (Exception ex) { ScriptExceptionLog.Report("SceneManager.LoadSceneAsync cancellation", ex); }
        try { AssetManager.PumpAssetLoadRequests(); }
        catch (Exception ex) { ScriptExceptionLog.Report("AssetManager.LoadAsync", ex); }
    }

    [UnmanagedCallersOnly]
    public static unsafe void AssetLoadsCompleted(ulong* operationIds, ulong* assetIds, int* statuses, int count)
    {
        if (count <= 0 || operationIds == null || assetIds == null || statuses == null) return;
        try
        {
            AssetManager.CompleteAsyncLoads(new ReadOnlySpan<ulong>(operationIds, count),
                new ReadOnlySpan<ulong>(assetIds, count), new ReadOnlySpan<int>(statuses, count));
        }
        catch (Exception ex) { ScriptExceptionLog.Report("AssetManager.LoadAsync completion", ex); }
    }

    [UnmanagedCallersOnly]
    public static unsafe void RaiseBeforeSceneUnloaded(byte* sceneName)
    {
        try { SceneManager.RaiseBeforeSceneUnloaded(PtrToString(sceneName)); }
        catch (Exception ex) { ScriptExceptionLog.Report("SceneManager.BeforeSceneUnloaded handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static unsafe void RaiseSceneUnloaded(byte* sceneName)
    {
        try { SceneManager.RaiseSceneUnloaded(PtrToString(sceneName)); }
        catch (Exception ex) { ScriptExceptionLog.Report("SceneManager.SceneUnloaded handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static void RaiseWindowResize()
    {
        try { Window.InvokeResize(); }
        catch (Exception ex) { ScriptExceptionLog.Report("Window.Resized handler", ex); }
    }

    [UnmanagedCallersOnly]
    public static void RaiseUiEventDispatch()
    {
        try
        {
            Funke.UI.UIEventDispatcher.Tick();
        }
        catch (Exception ex)
        {
            Log.Error($"RaiseUiEventDispatch failed: {ex}");
        }
    }
}
