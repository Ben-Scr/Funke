using System;
using System.Collections.Concurrent;
using System.Text;

namespace Funke.Interop;
internal static unsafe class InternalCalls
{
    // ── Application ─────────────────────────────────────────────────

    internal static float Application_GetDeltaTime() => NativeCallbacks.Bindings.Application_GetDeltaTime();
    internal static float Application_GetElapsedTime() => NativeCallbacks.Bindings.Application_GetElapsedTime();
    internal static int Application_GetScreenWidth() => NativeCallbacks.Bindings.Application_GetScreenWidth();
    internal static int Application_GetScreenHeight() => NativeCallbacks.Bindings.Application_GetScreenHeight();

    internal static SafeAreaInsets Application_GetSafeAreaInsets()
    {
        float left, top, right, bottom;
        NativeCallbacks.Bindings.Application_GetSafeAreaInsets(&left, &top, &right, &bottom);
        return new SafeAreaInsets(left, top, right, bottom);
    }
    internal static float Application_GetTargetFrameRate() => NativeCallbacks.Bindings.Application_GetTargetFrameRate();
    internal static void Application_SetTargetFrameRate(float fps) => NativeCallbacks.Bindings.Application_SetTargetFrameRate(fps);
    internal static void Application_Quit() => NativeCallbacks.Bindings.Application_Quit();
    internal static void Application_Reload() => NativeCallbacks.Bindings.Application_Reload();
    internal static void Application_Restart() => NativeCallbacks.Bindings.Application_Restart();
    internal static float Application_GetFixedDeltaTime() => NativeCallbacks.Bindings.Application_GetFixedDeltaTime();
    internal static void Application_SetFixedDeltaTime(float step) => NativeCallbacks.Bindings.Application_SetFixedDeltaTime(step);
    internal static float Application_GetUnscaledDeltaTime() => NativeCallbacks.Bindings.Application_GetUnscaledDeltaTime();
    internal static float Application_GetFixedUnscaledDeltaTime() => NativeCallbacks.Bindings.Application_GetFixedUnscaledDeltaTime();
    internal static float Application_GetTimeScale() => NativeCallbacks.Bindings.Application_GetTimeScale();
    internal static void Application_SetTimeScale(float scale) => NativeCallbacks.Bindings.Application_SetTimeScale(scale);
    internal static bool Application_IsEditor() => NativeCallbacks.Bindings.Application_IsEditor() != 0;

    internal static string Application_GetClipboardString()
        => ReadNativeString(NativeCallbacks.Bindings.Application_GetClipboardStringBuffer);

    internal static void Application_SetClipboardString(string? value)
    {
        byte[] buf = EncodeUtf8Z(value);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.Application_SetClipboardString(ptr);
    }

    internal static void Application_RequestPermission(string permission)
    {
        byte[] buf = EncodeUtf8Z(permission);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.Application_RequestPermission(ptr);
    }

    internal static int Application_GetPermissionState(string permission)
    {
        byte[] buf = EncodeUtf8Z(permission);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Application_GetPermissionState(ptr);
    }

    // ── AudioStream ─────────────────────────────────────────────────

    internal static uint AudioStream_Open(string path)
    {
        byte[] buf = EncodeUtf8Z(path);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.AudioStream_Open(ptr);
    }

    internal static unsafe void Media_SetNowPlaying(string title, string artist, string album, float duration, byte[]? rgba, int width, int height)
    {
        byte[] t = EncodeUtf8Z(title), a = EncodeUtf8Z(artist), al = EncodeUtf8Z(album);
        fixed (byte* pt = t)
        fixed (byte* pa = a)
        fixed (byte* pal = al)
        fixed (byte* px = rgba)
            NativeCallbacks.Bindings.Media_SetNowPlaying(pt, pa, pal, duration, px, rgba != null ? width : 0, rgba != null ? height : 0);
    }

    internal static void Media_SetPlaybackState(bool playing, float position) => NativeCallbacks.Bindings.Media_SetPlaybackState(playing ? 1 : 0, position);

    internal static unsafe int Media_PollCommand(out float seekSeconds)
    {
        float seek = 0;
        int command = NativeCallbacks.Bindings.Media_PollCommand(&seek);
        seekSeconds = seek;
        return command;
    }

    internal static void Audio_SetPlayInBackground(bool enabled) => NativeCallbacks.Bindings.Audio_SetPlayInBackground(enabled ? 1 : 0);
    internal static bool Audio_GetPlayInBackground() => NativeCallbacks.Bindings.Audio_GetPlayInBackground() != 0;
    internal static void AudioStream_Close(uint stream) => NativeCallbacks.Bindings.AudioStream_Close(stream);
    internal static bool AudioStream_Play(uint stream) => NativeCallbacks.Bindings.AudioStream_Play(stream) != 0;
    internal static void AudioStream_Pause(uint stream) => NativeCallbacks.Bindings.AudioStream_Pause(stream);
    internal static void AudioStream_Stop(uint stream) => NativeCallbacks.Bindings.AudioStream_Stop(stream);
    internal static bool AudioStream_IsPlaying(uint stream) => NativeCallbacks.Bindings.AudioStream_IsPlaying(stream) != 0;
    internal static bool AudioStream_IsAtEnd(uint stream) => NativeCallbacks.Bindings.AudioStream_IsAtEnd(stream) != 0;
    internal static float AudioStream_GetCursor(uint stream) => NativeCallbacks.Bindings.AudioStream_GetCursor(stream);
    internal static float AudioStream_GetLength(uint stream) => NativeCallbacks.Bindings.AudioStream_GetLength(stream);
    internal static void AudioStream_Seek(uint stream, float seconds) => NativeCallbacks.Bindings.AudioStream_Seek(stream, seconds);
    internal static float AudioStream_GetVolume(uint stream) => NativeCallbacks.Bindings.AudioStream_GetVolume(stream);
    internal static void AudioStream_SetVolume(uint stream, float volume) => NativeCallbacks.Bindings.AudioStream_SetVolume(stream, volume);
    internal static bool AudioStream_GetLoop(uint stream) => NativeCallbacks.Bindings.AudioStream_GetLoop(stream) != 0;
    internal static void AudioStream_SetLoop(uint stream, bool loop) => NativeCallbacks.Bindings.AudioStream_SetLoop(stream, loop ? 1 : 0);

    internal static bool Application_GetVsyncEnabled() => NativeCallbacks.Bindings.Application_GetVsyncEnabled() != 0;
    internal static void Application_SetVsyncEnabled(bool enabled) => NativeCallbacks.Bindings.Application_SetVsyncEnabled(enabled ? 1 : 0);
    internal static bool Application_GetRunInBackground() => NativeCallbacks.Bindings.Application_GetRunInBackground() != 0;
    internal static void Application_SetRunInBackground(bool enabled) => NativeCallbacks.Bindings.Application_SetRunInBackground(enabled ? 1 : 0);
    internal static int Application_GetProcessorCount() => NativeCallbacks.Bindings.Application_GetProcessorCount();
    internal static string Application_GetDataPath()
        => ReadNativeString(NativeCallbacks.Bindings.Application_GetDataPathBuffer);
    internal static string Application_GetName()
        => ReadNativeString(NativeCallbacks.Bindings.Application_GetNameBuffer);
    internal static string Application_GetVersion()
        => ReadNativeString(NativeCallbacks.Bindings.Application_GetVersionBuffer);
    internal static string Application_GetCompany()
        => ReadNativeString(NativeCallbacks.Bindings.Application_GetCompanyBuffer);
    internal static string Application_GetPrefsProfile()
        => NativeCallbacks.Bindings.Application_GetPrefsProfileBuffer == null
            ? "" : ReadNativeString(NativeCallbacks.Bindings.Application_GetPrefsProfileBuffer);

    // ── Dev console ─────────────────────────────────────────────────

    internal static string DevConsole_Execute(string line)
    {
        if (NativeCallbacks.Bindings.DevConsole_Execute == null) return "";
        byte[] input = EncodeUtf8Z(line);
        // One call only: asking again for a bigger buffer would run the command twice.
        byte[] output = new byte[16 * 1024];
        fixed (byte* pIn = input)
        fixed (byte* pOut = output)
            NativeCallbacks.Bindings.DevConsole_Execute(pIn, pOut, output.Length);
        int length = Array.IndexOf(output, (byte)0);
        return Encoding.UTF8.GetString(output, 0, length < 0 ? output.Length : length);
    }

    internal static int DevConsole_GetState()
        => NativeCallbacks.Bindings.DevConsole_GetState == null ? 0 : NativeCallbacks.Bindings.DevConsole_GetState();

    // ── Java bridge ─────────────────────────────────────────────────

    internal static bool Java_Call(string className, string method, string argument, out string result)
    {
        result = "";
        if (NativeCallbacks.Bindings.Java_Call == null) return false;
        byte[] c = EncodeUtf8Z(className), m = EncodeUtf8Z(method), a = EncodeUtf8Z(argument);
        int length;
        fixed (byte* pc = c, pm = m, pa = a) length = NativeCallbacks.Bindings.Java_Call(pc, pm, pa);
        if (length < 0) return false;
        result = Encoding.UTF8.GetString(Java_ReadResult(length));
        return true;
    }

    internal static bool Java_PollMessage(out string channel, out string message)
    {
        channel = message = "";
        if (NativeCallbacks.Bindings.Java_PollMessage == null) return false;
        int length = NativeCallbacks.Bindings.Java_PollMessage();
        if (length < 0) return false;
        byte[] bytes = Java_ReadResult(length);
        int separator = Array.IndexOf(bytes, (byte)0);
        if (separator < 0) separator = bytes.Length;
        channel = Encoding.UTF8.GetString(bytes, 0, separator);
        message = separator < bytes.Length ? Encoding.UTF8.GetString(bytes, separator + 1, bytes.Length - separator - 1) : "";
        return true;
    }

    private static byte[] Java_ReadResult(int length)
    {
        byte[] bytes = new byte[length];
        if (length > 0)
            fixed (byte* p = bytes) NativeCallbacks.Bindings.Java_ReadResult(p, length);
        return bytes;
    }
    internal static int JobSystem_Reconfigure(int workerCount) => NativeCallbacks.Bindings.JobSystem_Reconfigure(workerCount);

    internal static GraphicsSettings RenderSettings_Get()
    {
        float renderScale;
        int upscaleFilter, postProcessingEnabled, postProcessingQuality, particleQuality;
        NativeCallbacks.Bindings.RenderSettings_Get(
            &renderScale, &upscaleFilter, &postProcessingEnabled,
            &postProcessingQuality, &particleQuality);
        var settings = new GraphicsSettings(
            renderScale,
            (UpscaleFilterMode)upscaleFilter,
            postProcessingEnabled != 0,
            (PostProcessingQualityLevel)postProcessingQuality,
            (ParticleQualityLevel)particleQuality);

        int shadowQuality, maxLights, toneMapping;
        float shadowDistance, exposure;
        NativeCallbacks.Bindings.RenderSettings_Get3D(&shadowQuality, &shadowDistance, &maxLights, &exposure, &toneMapping);
        settings.ShadowQuality = (ShadowQualityLevel)shadowQuality;
        settings.ShadowDistance = shadowDistance;
        settings.MaxLights = maxLights;
        settings.Exposure = exposure;
        settings.ToneMapping = (ToneMappingMode)toneMapping;
        return settings;
    }

    internal static GraphicsApplyStatus RenderSettings_Apply(GraphicsSettings settings)
    {
        var status2D = (GraphicsApplyStatus)NativeCallbacks.Bindings.RenderSettings_Apply(
            settings.RenderScale,
            (int)settings.UpscaleFilter,
            settings.PostProcessingEnabled ? 1 : 0,
            (int)settings.PostProcessingQuality,
            (int)settings.ParticleQuality);
        if (status2D != GraphicsApplyStatus.Applied && status2D != GraphicsApplyStatus.Unchanged)
            return status2D;
        var status3D = (GraphicsApplyStatus)NativeCallbacks.Bindings.RenderSettings_Apply3D(
            (int)settings.ShadowQuality,
            settings.ShadowDistance,
            settings.MaxLights,
            settings.Exposure,
            (int)settings.ToneMapping);
        if (status3D != GraphicsApplyStatus.Applied && status3D != GraphicsApplyStatus.Unchanged)
            return status3D;
        return status2D == GraphicsApplyStatus.Applied || status3D == GraphicsApplyStatus.Applied
            ? GraphicsApplyStatus.Applied
            : GraphicsApplyStatus.Unchanged;
    }

    internal static GraphicsApplyStatus RenderSettings_ResetToDefaults()
        => (GraphicsApplyStatus)NativeCallbacks.Bindings.RenderSettings_ResetToDefaults();

    // ── Window ──────────────────────────────────────────────────────

    internal static int Window_GetWidth() => NativeCallbacks.Bindings.Window_GetWidth();
    internal static int Window_GetHeight() => NativeCallbacks.Bindings.Window_GetHeight();

    internal static string Window_GetTitle()
        => ReadNativeString(NativeCallbacks.Bindings.Window_GetTitleBuffer);

    internal static void Window_SetTitle(string? value)
    {
        byte[] buf = EncodeUtf8Z(value);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.Window_SetTitle(ptr);
    }

    internal static void Window_Minimize() => NativeCallbacks.Bindings.Window_Minimize();
    internal static void Window_Maximize() => NativeCallbacks.Bindings.Window_Maximize();
    internal static void Window_Restore() => NativeCallbacks.Bindings.Window_Restore();
    internal static bool Window_IsMaximized() => NativeCallbacks.Bindings.Window_IsMaximized() != 0;
    internal static bool Window_IsFullScreen() => NativeCallbacks.Bindings.Window_IsFullScreen() != 0;
    internal static void Window_SetFullScreen(bool enabled)
        => NativeCallbacks.Bindings.Window_SetFullScreen(enabled ? 1 : 0);

    internal static Vector2Int Window_GetPosition()
    {
        int x, y;
        NativeCallbacks.Bindings.Window_GetPosition(&x, &y);
        return new Vector2Int(x, y);
    }

    internal static void Window_SetPosition(Vector2Int value)
        => NativeCallbacks.Bindings.Window_SetPosition(value.X, value.Y);

    internal static void Window_Focus() => NativeCallbacks.Bindings.Window_Focus();

    internal static Vector2Int Window_GetScreenSize()
    {
        int w, h;
        NativeCallbacks.Bindings.Window_GetScreenSize(&w, &h);
        return new Vector2Int(w, h);
    }

    internal static int Cursor_GetMode() => NativeCallbacks.Bindings.Cursor_GetMode();
    internal static void Cursor_SetMode(int mode) => NativeCallbacks.Bindings.Cursor_SetMode(mode);
    internal static ulong Cursor_GetTexture() => NativeCallbacks.Bindings.Cursor_GetTexture();
    internal static void Cursor_SetTexture(ulong assetId, int hotspotX, int hotspotY) => NativeCallbacks.Bindings.Cursor_SetTexture(assetId, hotspotX, hotspotY);

    // ── Engine ──────────────────────────────────────────────────────

    internal static string Engine_GetVersion()
        => ReadNativeString(NativeCallbacks.Bindings.Engine_GetVersionBuffer);
    internal static string Engine_GetVersionLong()
        => ReadNativeString(NativeCallbacks.Bindings.Engine_GetVersionLongBuffer);
    internal static int Engine_GetBuildConfiguration()
        => NativeCallbacks.Bindings.Engine_GetBuildConfiguration();
    internal static string Engine_GetPlatform()
        => ReadNativeString(NativeCallbacks.Bindings.Engine_GetPlatformBuffer);
    internal static string Engine_GetGraphicsApi()
        => ReadNativeString(NativeCallbacks.Bindings.Engine_GetGraphicsApiBuffer);
    internal static string Engine_GetGpuVendor()
        => ReadNativeString(NativeCallbacks.Bindings.Engine_GetGpuVendorBuffer);
    internal static string Engine_GetGpuRenderer()
        => ReadNativeString(NativeCallbacks.Bindings.Engine_GetGpuRendererBuffer);

    // ── Time ────────────────────────────────────────────────────────

    internal static int Time_GetFrameCount() => NativeCallbacks.Bindings.Time_GetFrameCount();
    internal static float Time_GetTimeSinceStartup() => NativeCallbacks.Bindings.Time_GetTimeSinceStartup();
    internal static float Time_GetRealtimeSinceStartup() => NativeCallbacks.Bindings.Time_GetRealtimeSinceStartup();

    // ── Log ─────────────────────────────────────────────────────────

    // ── Editor windows ─────────────────────────────────────────────
    internal static void EditorGUI_Text(string text, int style)
    {
        byte[] buf = EncodeUtf8Z(text);
        fixed (byte* p = buf) NativeCallbacks.Bindings.EditorGUI_Text(p, style);
    }
    internal static bool EditorGUI_Button(string label, float width, float height)
    {
        byte[] buf = EncodeUtf8Z(label);
        fixed (byte* p = buf) return NativeCallbacks.Bindings.EditorGUI_Button(p, width, height) != 0;
    }
    internal static bool EditorGUI_Toggle(string label, bool value)
    {
        byte[] buf = EncodeUtf8Z(label);
        fixed (byte* p = buf) return NativeCallbacks.Bindings.EditorGUI_Toggle(p, value ? 1 : 0) != 0;
    }
    internal static float EditorGUI_Slider(string label, float value, float min, float max, string? format)
    {
        byte[] l = EncodeUtf8Z(label), f = EncodeUtf8Z(format);
        fixed (byte* pl = l) fixed (byte* pf = f) return NativeCallbacks.Bindings.EditorGUI_Slider(pl, value, min, max, pf);
    }
    internal static float EditorGUI_VerticalSlider(string label, float width, float height, float value, float min, float max, string? format)
    {
        byte[] l = EncodeUtf8Z(label), f = EncodeUtf8Z(format);
        fixed (byte* pl = l) fixed (byte* pf = f) return NativeCallbacks.Bindings.EditorGUI_VerticalSlider(pl, width, height, value, min, max, pf);
    }
    internal static float EditorGUI_FloatField(string label, float value, float speed)
    {
        byte[] buf = EncodeUtf8Z(label);
        fixed (byte* p = buf) return NativeCallbacks.Bindings.EditorGUI_FloatField(p, value, speed);
    }
    internal static bool EditorGUI_TextField(string label, ref string text, int maxBytes)
    {
        byte[] l = EncodeUtf8Z(label);
        int textBytes = Encoding.UTF8.GetByteCount(text);
        byte[] buffer = new byte[Math.Max(maxBytes, textBytes) + 1];
        Encoding.UTF8.GetBytes(text, buffer);
        int changed;
        fixed (byte* pl = l) fixed (byte* pb = buffer) changed = NativeCallbacks.Bindings.EditorGUI_TextField(pl, pb, buffer.Length);
        if (changed == 0) return false;
        int length = Array.IndexOf(buffer, (byte)0);
        text = Encoding.UTF8.GetString(buffer, 0, length < 0 ? buffer.Length : length);
        return true;
    }
    internal static void EditorGUI_ProgressBar(float fraction, float width, float height, string? overlay)
    {
        byte[] buf = EncodeUtf8Z(overlay);
        fixed (byte* p = buf) NativeCallbacks.Bindings.EditorGUI_ProgressBar(fraction, width, height, p);
    }
    internal static void EditorGUI_Layout(int op, float value) => NativeCallbacks.Bindings.EditorGUI_Layout(op, value);
    internal static void EditorGUI_PushId(string id)
    {
        byte[] buf = EncodeUtf8Z(id);
        fixed (byte* p = buf) NativeCallbacks.Bindings.EditorGUI_PushId(p);
    }
    internal static float EditorGUI_Query(int what) => NativeCallbacks.Bindings.EditorGUI_Query(what);

    private static byte[] EncodeUtf8Z(string? value)
    {
        value ??= "";
        int len = Encoding.UTF8.GetByteCount(value);
        byte[] buffer = new byte[len + 1];
        Encoding.UTF8.GetBytes(value, buffer);
        buffer[len] = 0;
        return buffer;
    }

    // Component/class/parameter names repeat every frame; caching their bytes saves a GC allocation per binding call.
    // Callers only pass the buffer as a read-only C string, so sharing it is safe.
    private static readonly ConcurrentDictionary<string, byte[]> s_Utf8NameCache = new(StringComparer.Ordinal);
    private const int k_Utf8NameCacheLimit = 4096;

    private static byte[] EncodeUtf8Name(string? value)
    {
        value ??= "";
        if (s_Utf8NameCache.TryGetValue(value, out byte[]? cached))
            return cached;

        byte[] buffer = EncodeUtf8Z(value);
        if (s_Utf8NameCache.Count < k_Utf8NameCacheLimit)
            s_Utf8NameCache.TryAdd(value, buffer);
        return buffer;
    }

    private static string DecodeNativeString(byte[] buffer, int byteCount)
    {
        byteCount = Math.Clamp(byteCount, 0, Math.Max(0, buffer.Length - 1));
        return byteCount == 0 ? "" : Encoding.UTF8.GetString(buffer, 0, byteCount);
    }

    private static string ReadNativeString(delegate* unmanaged<byte*, int, int> fn)
    {
        int required = fn(null, 0);
        if (required <= 0) return "";

        byte[] buffer = new byte[required + 1];
        fixed (byte* ptr = buffer)
        {
            int written = fn(ptr, buffer.Length);
            return DecodeNativeString(buffer, written);
        }
    }

    private static string ReadNativeString(delegate* unmanaged<ulong, byte*, int, int> fn, ulong value)
    {
        int required = fn(value, null, 0);
        if (required <= 0) return "";

        byte[] buffer = new byte[required + 1];
        fixed (byte* ptr = buffer)
        {
            int written = fn(value, ptr, buffer.Length);
            return DecodeNativeString(buffer, written);
        }
    }

    private static string ReadNativeString(delegate* unmanaged<int, byte*, int, int> fn, int value)
    {
        int required = fn(value, null, 0);
        if (required <= 0) return "";

        byte[] buffer = new byte[required + 1];
        fixed (byte* ptr = buffer)
        {
            int written = fn(value, ptr, buffer.Length);
            return DecodeNativeString(buffer, written);
        }
    }

    // Variant for the Dropdown_GetOptionBuffer signature where the
    // entity ID + option index pair drives a UTF-8 buffer fetch.
    private static string ReadNativeStringIndexed(delegate* unmanaged<ulong, int, byte*, int, int> fn, ulong entityID, int index)
    {
        int required = fn(entityID, index, null, 0);
        if (required <= 0) return "";

        byte[] buffer = new byte[required + 1];
        fixed (byte* ptr = buffer)
        {
            int written = fn(entityID, index, ptr, buffer.Length);
            return DecodeNativeString(buffer, written);
        }
    }

    private static void CallStringBinding(delegate* unmanaged<byte*, void> fn, string? message)
    {
        if (fn == null) return;  // native logging not bound yet (early init / headless tests) — drop rather than deref null
        message ??= "";
        int len = Encoding.UTF8.GetByteCount(message);
        Span<byte> buf = len <= 512 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(message, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) fn(ptr);
    }

    internal static void Log_Trace(string message) => CallStringBinding(NativeCallbacks.Bindings.Log_Trace, message);
    internal static void Log_Info(string message) => CallStringBinding(NativeCallbacks.Bindings.Log_Info, message);
    internal static void Log_Warn(string message) => CallStringBinding(NativeCallbacks.Bindings.Log_Warn, message);
    internal static void Log_Error(string message) => CallStringBinding(NativeCallbacks.Bindings.Log_Error, message);

    // ── Input ───────────────────────────────────────────────────────

    internal static bool Input_GetKey(int keyCode) => NativeCallbacks.Bindings.Input_GetKey(keyCode) != 0;
    internal static bool Input_GetKeyDown(int keyCode) => NativeCallbacks.Bindings.Input_GetKeyDown(keyCode) != 0;
    internal static bool Input_GetKeyUp(int keyCode) => NativeCallbacks.Bindings.Input_GetKeyUp(keyCode) != 0;
    internal static bool Input_GetAnyKey() => NativeCallbacks.Bindings.Input_GetAnyKey() != 0;
    internal static bool Input_GetMouseButton(int button) => NativeCallbacks.Bindings.Input_GetMouseButton(button) != 0;
    internal static bool Input_GetMouseButtonDown(int button) => NativeCallbacks.Bindings.Input_GetMouseButtonDown(button) != 0;
    internal static bool Input_GetMouseButtonUp(int button) => NativeCallbacks.Bindings.Input_GetMouseButtonUp(button) != 0;

    internal static void Input_GetMousePosition(out float x, out float y)
    {
        float ox, oy;
        NativeCallbacks.Bindings.Input_GetMousePosition(&ox, &oy);
        x = ox; y = oy;
    }

    internal static void Input_GetAxis(out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.Input_GetAxis(&ox, &oy); x = ox; y = oy; }
    internal static void Input_GetMouseDelta(out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.Input_GetMouseDelta(&ox, &oy); x = ox; y = oy; }
    internal static float Input_GetScrollWheelDelta() => NativeCallbacks.Bindings.Input_GetScrollWheelDelta();
    internal static bool Input_IsGamepadConnected(int gamepad) => NativeCallbacks.Bindings.Input_IsGamepadConnected(gamepad) != 0;
    internal static bool Input_GetGamepadButton(int button, int gamepad) => NativeCallbacks.Bindings.Input_GetGamepadButton(button, gamepad) != 0;
    internal static bool Input_GetGamepadButtonDown(int button, int gamepad) => NativeCallbacks.Bindings.Input_GetGamepadButtonDown(button, gamepad) != 0;
    internal static bool Input_GetGamepadButtonUp(int button, int gamepad) => NativeCallbacks.Bindings.Input_GetGamepadButtonUp(button, gamepad) != 0;
    internal static float Input_GetGamepadAxis(int axis, int gamepad) => NativeCallbacks.Bindings.Input_GetGamepadAxis(axis, gamepad);

    internal static Vector3 Input_GetAcceleration()
    {
        float x, y, z;
        NativeCallbacks.Bindings.Input_GetAcceleration(&x, &y, &z);
        return new Vector3(x, y, z);
    }

    internal static Vector3 Input_GetGyroscope()
    {
        float x, y, z;
        NativeCallbacks.Bindings.Input_GetGyroscope(&x, &y, &z);
        return new Vector3(x, y, z);
    }

    internal static bool Input_IsMotionSensorAvailable(int sensor) => NativeCallbacks.Bindings.Input_IsMotionSensorAvailable(sensor) != 0;

    internal static int Input_GetTouchCount() => NativeCallbacks.Bindings.Input_GetTouchCount();

    internal static bool Input_GetTouch(int index, out Touch touch)
    {
        long id;
        int phase;
        float* values = stackalloc float[4];
        if (NativeCallbacks.Bindings.Input_GetTouch(index, &id, &phase, values) == 0)
        {
            touch = default;
            return false;
        }
        touch = new Touch(id, new Vector2(values[0], values[1]), new Vector2(values[2], values[3]), (TouchPhase)phase);
        return true;
    }

    internal static void Physics2D_SetQueryLayerMask(ulong mask) => NativeCallbacks.Bindings.Physics2D_SetQueryLayerMask(mask);
    internal static bool Collider2D_GetSensor(ulong id, int type) => NativeCallbacks.Bindings.Collider2D_GetSensor(id, type) != 0;
    internal static void Collider2D_SetSensor(ulong id, int type, bool sensor) => NativeCallbacks.Bindings.Collider2D_SetSensor(id, type, sensor ? 1 : 0);
    internal static ulong Collider2D_GetLayer(ulong id, int type) => NativeCallbacks.Bindings.Collider2D_GetLayer(id, type);
    internal static void Collider2D_SetLayer(ulong id, int type, ulong layer) => NativeCallbacks.Bindings.Collider2D_SetLayer(id, type, layer);
    internal static ulong Collider2D_GetCollisionMask(ulong id, int type) => NativeCallbacks.Bindings.Collider2D_GetCollisionMask(id, type);
    internal static void Collider2D_SetCollisionMask(ulong id, int type, ulong mask) => NativeCallbacks.Bindings.Collider2D_SetCollisionMask(id, type, mask);
    internal static float Collider2D_GetFriction(ulong id, int type) => NativeCallbacks.Bindings.Collider2D_GetFriction(id, type);
    internal static void Collider2D_SetFriction(ulong id, int type, float v) => NativeCallbacks.Bindings.Collider2D_SetFriction(id, type, v);
    internal static float Collider2D_GetBounciness(ulong id, int type) => NativeCallbacks.Bindings.Collider2D_GetBounciness(id, type);
    internal static void Collider2D_SetBounciness(ulong id, int type, float v) => NativeCallbacks.Bindings.Collider2D_SetBounciness(id, type, v);

    internal static void SpriteAnimator2D_Command(ulong id, int command) => NativeCallbacks.Bindings.SpriteAnimator2D_Command(id, command);
    internal static bool SpriteAnimator2D_IsPlaying(ulong id) => NativeCallbacks.Bindings.SpriteAnimator2D_IsPlaying(id) != 0;
    internal static int SpriteAnimator2D_GetFrame(ulong id) => NativeCallbacks.Bindings.SpriteAnimator2D_GetFrame(id);
    internal static void SpriteAnimator2D_SetFrame(ulong id, int frame) => NativeCallbacks.Bindings.SpriteAnimator2D_SetFrame(id, frame);
    internal static float SpriteAnimator2D_GetFramesPerSecond(ulong id) => NativeCallbacks.Bindings.SpriteAnimator2D_GetFramesPerSecond(id);
    internal static void SpriteAnimator2D_SetFramesPerSecond(ulong id, float fps) => NativeCallbacks.Bindings.SpriteAnimator2D_SetFramesPerSecond(id, fps);

    // ── PostProcessing2D ───────────────────────────────────────

    internal static T PostProcessing2D_GetEffect<T>(ulong id, PostProcessingInterop.Effect effect) where T : unmanaged
    {
        T value = default;
        NativeCallbacks.Bindings.PostProcessing2D_GetEffect(id, (int)effect, &value, sizeof(T));
        return value;
    }

    internal static void PostProcessing2D_SetEffect<T>(ulong id, PostProcessingInterop.Effect effect, T value) where T : unmanaged
        => NativeCallbacks.Bindings.PostProcessing2D_SetEffect(id, (int)effect, &value, sizeof(T));

    // ── Scene ───────────────────────────────────────────────────

    internal static string Scene_GetActiveSceneName()
    {
        return ReadNativeString(NativeCallbacks.Bindings.Scene_GetActiveSceneNameBuffer);
    }
    internal static int Scene_GetEntityCount() => NativeCallbacks.Bindings.Scene_GetEntityCount();

    internal static int Scene_GetEntityCount(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_GetEntityCountByName(ptr);
    }

    internal static string Scene_GetEntityNameByUUID(ulong uuid)
    {
        return ReadNativeString(NativeCallbacks.Bindings.Scene_GetEntityNameByUUIDBuffer, uuid);
    }

    internal static bool Scene_LoadAdditive(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_LoadAdditive(ptr) != 0;
    }

    internal static bool Scene_Load(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_Load(ptr) != 0;
    }

    internal static void Scene_Unload(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.Scene_Unload(ptr);
    }

    internal static bool Scene_SetActive(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_SetActive(ptr) != 0;
    }

    internal static bool Scene_Reload(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_Reload(ptr) != 0;
    }

    internal static void Scene_SetGlobalSystemEnabled(string className, bool enabled)
    {
        byte[] buf = EncodeUtf8Name(className);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.Scene_SetGlobalSystemEnabled(ptr, enabled ? 1 : 0);
    }

    internal static bool Scene_SetSceneSystemEnabled(string sceneName, string className, bool enabled)
    {
        byte[] sceneBuffer = EncodeUtf8Z(sceneName);
        byte[] classBuffer = EncodeUtf8Name(className);
        fixed (byte* scenePtr = sceneBuffer)
        fixed (byte* classPtr = classBuffer)
        {
            return NativeCallbacks.Bindings.Scene_SetSceneSystemEnabled(scenePtr, classPtr, enabled ? 1 : 0) != 0;
        }
    }

    internal static bool Scene_IsSceneSystemEnabled(string sceneName, string className)
    {
        byte[] sceneBuffer = EncodeUtf8Z(sceneName);
        byte[] classBuffer = EncodeUtf8Name(className);
        fixed (byte* scenePtr = sceneBuffer)
        fixed (byte* classPtr = classBuffer)
        {
            return NativeCallbacks.Bindings.Scene_IsSceneSystemEnabled(scenePtr, classPtr) != 0;
        }
    }

    internal static bool Scene_DoesSceneExist(string sceneName)
    {
        byte[] buf = EncodeUtf8Z(sceneName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Scene_DoesSceneExist(ptr) != 0;
    }

    internal static int Scene_GetLoadedCount() => NativeCallbacks.Bindings.Scene_GetLoadedCount();

    internal static string Scene_GetLoadedSceneNameAt(int index)
    {
        return ReadNativeString(NativeCallbacks.Bindings.Scene_GetLoadedSceneNameAtBuffer, index);
    }

    // ── Entity ──────────────────────────────────────────────────────

    internal static bool Entity_IsValid(ulong entityID) => NativeCallbacks.Bindings.Entity_IsValid(entityID) != 0;

    internal static ulong Entity_FindByName(string name)
    {
        byte[] buf = EncodeUtf8Z(name);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_FindByName(ptr);
    }

    internal static void Entity_Destroy(ulong entityID) => NativeCallbacks.Bindings.Entity_Destroy(entityID);

    internal static void Entity_DestroyDelayed(ulong entityID, float delay)
        => NativeCallbacks.Bindings.Entity_DestroyDelayed(entityID, delay);

    internal static ulong Entity_Clone(ulong sourceEntityID)
        => NativeCallbacks.Bindings.Entity_Clone(sourceEntityID);

    internal static ulong Entity_InstantiatePrefab(ulong prefabGuid)
        => NativeCallbacks.Bindings.Entity_InstantiatePrefab(prefabGuid);

    internal static int Entity_GetOrigin(ulong entityID)
        => NativeCallbacks.Bindings.Entity_GetOrigin(entityID);

    internal static ulong Entity_GetRuntimeID(ulong entityID)
        => NativeCallbacks.Bindings.Entity_GetRuntimeID(entityID);

    internal static ulong Entity_GetSceneGUID(ulong entityID)
        => NativeCallbacks.Bindings.Entity_GetSceneGUID(entityID);

    internal static ulong Entity_GetPrefabGUID(ulong entityID)
        => NativeCallbacks.Bindings.Entity_GetPrefabGUID(entityID);

    internal static bool Entity_HasComponent(ulong entityID, string componentName)
    {
        byte[] buf = EncodeUtf8Name(componentName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_HasComponent(entityID, ptr) != 0;
    }

    internal static bool Entity_AddComponent(ulong entityID, string componentName)
    {
        byte[] buf = EncodeUtf8Name(componentName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_AddComponent(entityID, ptr) != 0;
    }

    internal static bool Entity_RemoveComponent(ulong entityID, string componentName)
    {
        byte[] buf = EncodeUtf8Name(componentName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_RemoveComponent(entityID, ptr) != 0;
    }

    // ── Script add/has/remove ─────────────────────────────────────────
    internal static bool Entity_AddScript(ulong entityID, string className)
    {
        byte[] buf = EncodeUtf8Name(className);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_AddScript(entityID, ptr) != 0;
    }

    internal static bool Entity_HasScript(ulong entityID, string className)
    {
        byte[] buf = EncodeUtf8Name(className);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_HasScript(entityID, ptr) != 0;
    }

    internal static bool Entity_RemoveScript(ulong entityID, string className)
    {
        byte[] buf = EncodeUtf8Name(className);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_RemoveScript(entityID, ptr) != 0;
    }

    // Layout-size guard in ScriptHostBridge (Entity_GetComponentSize at startup) prevents a stale C# mirror from corrupting memory.
    internal static void* Entity_GetComponentPtr(ulong entityID, string componentName)
    {
        byte[] buf = EncodeUtf8Name(componentName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_GetComponentPtr(entityID, ptr);
    }

    internal static void* Entity_GetComponentPtrReadOnly(ulong entityID, string componentName)
    {
        byte[] buf = EncodeUtf8Name(componentName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_GetComponentPtrReadOnly(entityID, ptr);
    }

    internal static int Entity_GetComponentSize(string componentName)
    {
        byte[] buf = EncodeUtf8Name(componentName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_GetComponentSize(ptr);
    }

    internal static int Scene_OpenQueryView(
        string? sceneName,
        string? writeNames,
        string? readonlyNames,
        string? mustHaveNames,
        string? withoutNames,
        int enableFilter,
        void** outPointers,
        int maxRows)
    {
        byte[] sceneBuf = EncodeUtf8Z(sceneName ?? "");
        byte[] writeBuf = EncodeUtf8Z(writeNames ?? "");
        byte[] roBuf    = EncodeUtf8Z(readonlyNames ?? "");
        byte[] mustBuf  = EncodeUtf8Z(mustHaveNames ?? "");
        byte[] withoutBuf = EncodeUtf8Z(withoutNames ?? "");
        fixed (byte* sc = sceneBuf)
        fixed (byte* w  = writeBuf)
        fixed (byte* ro = roBuf)
        fixed (byte* mh = mustBuf)
        fixed (byte* wo = withoutBuf)
        {
            return NativeCallbacks.Bindings.Scene_OpenQueryView(sc, w, ro, mh, wo, enableFilter, outPointers, maxRows);
        }
    }

    internal static int Scene_OpenQueryViewWithEntities(
        string? sceneName,
        string? writeNames,
        string? readonlyNames,
        string? mustHaveNames,
        string? withoutNames,
        int enableFilter,
        void** outPointers,
        ulong* outEntityIds,
        int maxRows)
    {
        byte[] sceneBuf = EncodeUtf8Z(sceneName ?? "");
        byte[] writeBuf = EncodeUtf8Z(writeNames ?? "");
        byte[] roBuf    = EncodeUtf8Z(readonlyNames ?? "");
        byte[] mustBuf  = EncodeUtf8Z(mustHaveNames ?? "");
        byte[] withoutBuf = EncodeUtf8Z(withoutNames ?? "");
        fixed (byte* sc = sceneBuf)
        fixed (byte* w  = writeBuf)
        fixed (byte* ro = roBuf)
        fixed (byte* mh = mustBuf)
        fixed (byte* wo = withoutBuf)
        {
            return NativeCallbacks.Bindings.Scene_OpenQueryViewWithEntities(
                sc, w, ro, mh, wo, enableFilter, outPointers, outEntityIds, maxRows);
        }
    }

    internal static string Entity_GetManagedComponentFields(ulong entityID, string componentName)
    {
        byte[] buf = EncodeUtf8Name(componentName);
        fixed (byte* ptr = buf)
        {
            int required = NativeCallbacks.Bindings.Entity_GetManagedComponentFieldsBuffer(entityID, ptr, null, 0);
            if (required <= 0) return "{}";

            byte[] output = new byte[required + 1];
            fixed (byte* outPtr = output)
            {
                int written = NativeCallbacks.Bindings.Entity_GetManagedComponentFieldsBuffer(entityID, ptr, outPtr, output.Length);
                return DecodeNativeString(output, written);
            }
        }
    }

    internal static bool Entity_GetIsStatic(ulong entityID) => NativeCallbacks.Bindings.Entity_GetIsStatic(entityID) != 0;
    internal static void Entity_SetIsStatic(ulong entityID, bool isStatic) => NativeCallbacks.Bindings.Entity_SetIsStatic(entityID, isStatic ? 1 : 0);
    internal static bool Entity_GetIsEnabled(ulong entityID) => NativeCallbacks.Bindings.Entity_GetIsEnabled(entityID) != 0;
    internal static bool Entity_GetIsEnabledInHierarchy(ulong entityID) => NativeCallbacks.Bindings.Entity_GetIsEnabledInHierarchy(entityID) != 0;
    internal static void Entity_SetIsEnabled(ulong entityID, bool isEnabled) => NativeCallbacks.Bindings.Entity_SetIsEnabled(entityID, isEnabled ? 1 : 0);

    internal static ulong Entity_Create(string? name)
    {
        byte[] buf = EncodeUtf8Z(name);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Entity_Create(ptr);
    }

    // ── EntityCommandBuffer ─────────────────────────────────────────
    internal static uint Component_GetTypeId(string componentName)
    {
        byte[] buf = EncodeUtf8Name(componentName);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Component_GetTypeId(ptr);
    }

    internal static int Ecb_Playback(byte* buffer, int length, ulong* outRuntimeIds, int maxOut)
    {
        return NativeCallbacks.Bindings.Ecb_Playback(buffer, length, outRuntimeIds, maxOut);
    }

    // ── NameComponent ───────────────────────────────────────────────

    internal static string NameComponent_GetName(ulong entityID)
    {
        return ReadNativeString(NativeCallbacks.Bindings.NameComponent_GetNameBuffer, entityID);
    }

    internal static void NameComponent_SetName(ulong entityID, string? name)
    {
        byte[] buf = EncodeUtf8Z(name);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.NameComponent_SetName(entityID, ptr);
    }

    // ── Transform2D ─────────────────────────────────────────────────

    internal static void Transform2D_GetPosition(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.Transform2D_GetPosition(id, &ox, &oy); x = ox; y = oy; }
    internal static void Transform2D_SetPosition(ulong id, float x, float y) => NativeCallbacks.Bindings.Transform2D_SetPosition(id, x, y);
    internal static float Transform2D_GetRotation(ulong id) => NativeCallbacks.Bindings.Transform2D_GetRotation(id);
    internal static void Transform2D_SetRotation(ulong id, float rotation) => NativeCallbacks.Bindings.Transform2D_SetRotation(id, rotation);
    internal static void Transform2D_GetScale(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.Transform2D_GetScale(id, &ox, &oy); x = ox; y = oy; }
    internal static void Transform2D_SetScale(ulong id, float x, float y) => NativeCallbacks.Bindings.Transform2D_SetScale(id, x, y);
    internal static ulong Transform2D_GetEntity(ulong id) => NativeCallbacks.Bindings.Transform2D_GetEntity(id);
    internal static void Transform2D_GetLocalPosition(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.Transform2D_GetLocalPosition(id, &ox, &oy); x = ox; y = oy; }
    internal static void Transform2D_SetLocalPosition(ulong id, float x, float y) => NativeCallbacks.Bindings.Transform2D_SetLocalPosition(id, x, y);
    internal static float Transform2D_GetLocalRotation(ulong id) => NativeCallbacks.Bindings.Transform2D_GetLocalRotation(id);
    internal static void Transform2D_SetLocalRotation(ulong id, float rotation) => NativeCallbacks.Bindings.Transform2D_SetLocalRotation(id, rotation);
    internal static void Transform2D_GetLocalScale(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.Transform2D_GetLocalScale(id, &ox, &oy); x = ox; y = oy; }
    internal static void Transform2D_SetLocalScale(ulong id, float x, float y) => NativeCallbacks.Bindings.Transform2D_SetLocalScale(id, x, y);
    internal static ulong Entity_GetParent(ulong id) => NativeCallbacks.Bindings.Entity_GetParent(id);
    internal static bool Entity_SetParent(ulong id, ulong parentId, bool worldPositionStays) => NativeCallbacks.Bindings.Entity_SetParent(id, parentId, worldPositionStays ? 1 : 0) != 0;
    internal static int Entity_GetChildCount(ulong id) => NativeCallbacks.Bindings.Entity_GetChildCount(id);
    internal static ulong Entity_GetChildAt(ulong id, int index) => NativeCallbacks.Bindings.Entity_GetChildAt(id, index);
    internal static int Entity_GetChildren(ulong id, Span<ulong> outIDs)
    {
        fixed (ulong* idPtr = outIDs)
        {
            return NativeCallbacks.Bindings.Entity_GetChildren(id, idPtr, outIDs.Length);
        }
    }

    // ── SpriteRenderer ──────────────────────────────────────────────

    internal static void SpriteRenderer_GetColor(ulong id, out float r, out float g, out float b, out float a) { float cr, cg, cb, ca; NativeCallbacks.Bindings.SpriteRenderer_GetColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void SpriteRenderer_SetColor(ulong id, float r, float g, float b, float a) => NativeCallbacks.Bindings.SpriteRenderer_SetColor(id, r, g, b, a);
    internal static ulong SpriteRenderer_GetTexture(ulong id) => NativeCallbacks.Bindings.SpriteRenderer_GetTexture(id);
    internal static void SpriteRenderer_SetTexture(ulong id, ulong assetId) => NativeCallbacks.Bindings.SpriteRenderer_SetTexture(id, assetId);
    internal static int SpriteRenderer_GetSortingOrder(ulong id) => NativeCallbacks.Bindings.SpriteRenderer_GetSortingOrder(id);
    internal static void SpriteRenderer_SetSortingOrder(ulong id, int order) => NativeCallbacks.Bindings.SpriteRenderer_SetSortingOrder(id, order);
    internal static int SpriteRenderer_GetSortingLayer(ulong id) => NativeCallbacks.Bindings.SpriteRenderer_GetSortingLayer(id);
    internal static void SpriteRenderer_SetSortingLayer(ulong id, int layer) => NativeCallbacks.Bindings.SpriteRenderer_SetSortingLayer(id, layer);
    internal static int SpriteRenderer_GetFilter(ulong id) => NativeCallbacks.Bindings.SpriteRenderer_GetFilter(id);
    internal static void SpriteRenderer_SetFilter(ulong id, int filter) => NativeCallbacks.Bindings.SpriteRenderer_SetFilter(id, filter);
    internal static ulong SpriteRenderer_GetMaterial(ulong id) => NativeCallbacks.Bindings.SpriteRenderer_GetMaterial(id);
    internal static void SpriteRenderer_SetMaterial(ulong id, ulong materialGuid) => NativeCallbacks.Bindings.SpriteRenderer_SetMaterial(id, materialGuid);

    internal static string SpriteRenderer_GetSpriteName(ulong id)
        => ReadNativeString(NativeCallbacks.Bindings.SpriteRenderer_GetSpriteNameBuffer, id);

    internal static void SpriteRenderer_SetSpriteName(ulong id, string? name)
    {
        name ??= "";
        int len = Encoding.UTF8.GetByteCount(name);
        Span<byte> buf = len <= 256 ? stackalloc byte[len + 1] : new byte[len + 1];
        Encoding.UTF8.GetBytes(name, buf);
        buf[len] = 0;
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.SpriteRenderer_SetSpriteName(id, ptr);
    }

    // ── TextRenderer ────────────────────────────────────────────────

    internal static string TextRenderer_GetText(ulong entityID)
    {
        return ReadNativeString(NativeCallbacks.Bindings.TextRenderer_GetTextBuffer, entityID);
    }

    internal static void TextRenderer_SetText(ulong entityID, string text)
    {
        byte[] buf = EncodeUtf8Z(text);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.TextRenderer_SetText(entityID, ptr);
    }

    internal static ulong TextRenderer_GetFont(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetFont(id);
    internal static void TextRenderer_SetFont(ulong id, ulong assetId) => NativeCallbacks.Bindings.TextRenderer_SetFont(id, assetId);
    internal static float TextRenderer_GetFontSize(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetFontSize(id);
    internal static void TextRenderer_SetFontSize(ulong id, float size) => NativeCallbacks.Bindings.TextRenderer_SetFontSize(id, size);
    internal static void TextRenderer_GetColor(ulong id, out float r, out float g, out float b, out float a) { float cr, cg, cb, ca; NativeCallbacks.Bindings.TextRenderer_GetColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void TextRenderer_SetColor(ulong id, float r, float g, float b, float a) => NativeCallbacks.Bindings.TextRenderer_SetColor(id, r, g, b, a);
    internal static float TextRenderer_GetLetterSpacing(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetLetterSpacing(id);
    internal static void TextRenderer_SetLetterSpacing(ulong id, float spacing) => NativeCallbacks.Bindings.TextRenderer_SetLetterSpacing(id, spacing);
    internal static float TextRenderer_GetLineSpacing(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetLineSpacing(id);
    internal static void TextRenderer_SetLineSpacing(ulong id, float spacing) => NativeCallbacks.Bindings.TextRenderer_SetLineSpacing(id, spacing);
    internal static int TextRenderer_GetHAlign(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetHAlign(id);
    internal static void TextRenderer_SetHAlign(ulong id, int alignment) => NativeCallbacks.Bindings.TextRenderer_SetHAlign(id, alignment);
    internal static int TextRenderer_GetVAlign(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetVAlign(id);
    internal static void TextRenderer_SetVAlign(ulong id, int alignment) => NativeCallbacks.Bindings.TextRenderer_SetVAlign(id, alignment);
    internal static int TextRenderer_GetWrapMode(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetWrapMode(id);
    internal static void TextRenderer_SetWrapMode(ulong id, int mode) => NativeCallbacks.Bindings.TextRenderer_SetWrapMode(id, mode);
    // WrapWidth helpers removed alongside the field.
    internal static int TextRenderer_GetSortingOrder(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetSortingOrder(id);
    internal static void TextRenderer_SetSortingOrder(ulong id, int order) => NativeCallbacks.Bindings.TextRenderer_SetSortingOrder(id, order);
    internal static int TextRenderer_GetSortingLayer(ulong id) => NativeCallbacks.Bindings.TextRenderer_GetSortingLayer(id);
    internal static void TextRenderer_SetSortingLayer(ulong id, int layer) => NativeCallbacks.Bindings.TextRenderer_SetSortingLayer(id, layer);

    // ── Camera2D ────────────────────────────────────────────────────

    internal static float Camera2D_GetOrthographicSize(ulong id) => NativeCallbacks.Bindings.Camera2D_GetOrthographicSize(id);
    internal static void Camera2D_SetOrthographicSize(ulong id, float size) => NativeCallbacks.Bindings.Camera2D_SetOrthographicSize(id, size);
    internal static float Camera2D_GetZoom(ulong id) => NativeCallbacks.Bindings.Camera2D_GetZoom(id);
    internal static void Camera2D_SetZoom(ulong id, float zoom) => NativeCallbacks.Bindings.Camera2D_SetZoom(id, zoom);
    internal static void Camera2D_GetClearColor(ulong id, out float r, out float g, out float b, out float a) { float cr, cg, cb, ca; NativeCallbacks.Bindings.Camera2D_GetClearColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Camera2D_SetClearColor(ulong id, float r, float g, float b, float a) => NativeCallbacks.Bindings.Camera2D_SetClearColor(id, r, g, b, a);
    internal static void Camera2D_ScreenToWorld(ulong id, float sx, float sy, out float wx, out float wy) { float ox, oy; NativeCallbacks.Bindings.Camera2D_ScreenToWorld(id, sx, sy, &ox, &oy); wx = ox; wy = oy; }
    internal static float Camera2D_GetViewportWidth(ulong id) => NativeCallbacks.Bindings.Camera2D_GetViewportWidth(id);
    internal static float Camera2D_GetViewportHeight(ulong id) => NativeCallbacks.Bindings.Camera2D_GetViewportHeight(id);
    internal static ulong Camera2D_GetMainEntity() => NativeCallbacks.Bindings.Camera2D_GetMainEntity();
    internal static void Camera2D_GetFrustum(ulong id, out float minX, out float minY, out float maxX, out float maxY) { float a, b, c, d; NativeCallbacks.Bindings.Camera2D_GetFrustum(id, &a, &b, &c, &d); minX = a; minY = b; maxX = c; maxY = d; }

    // ── Rigidbody2D ─────────────────────────────────────────────────

    internal static void Rigidbody2D_ApplyForce(ulong id, float fx, float fy, bool wake) => NativeCallbacks.Bindings.Rigidbody2D_ApplyForce(id, fx, fy, wake ? 1 : 0);
    internal static void Rigidbody2D_ApplyImpulse(ulong id, float ix, float iy, bool wake) => NativeCallbacks.Bindings.Rigidbody2D_ApplyImpulse(id, ix, iy, wake ? 1 : 0);
    internal static void Rigidbody2D_GetLinearVelocity(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.Rigidbody2D_GetLinearVelocity(id, &ox, &oy); x = ox; y = oy; }
    internal static void Rigidbody2D_SetLinearVelocity(ulong id, float x, float y) => NativeCallbacks.Bindings.Rigidbody2D_SetLinearVelocity(id, x, y);
    internal static float Rigidbody2D_GetAngularVelocity(ulong id) => NativeCallbacks.Bindings.Rigidbody2D_GetAngularVelocity(id);
    internal static void Rigidbody2D_SetAngularVelocity(ulong id, float v) => NativeCallbacks.Bindings.Rigidbody2D_SetAngularVelocity(id, v);
    internal static int Rigidbody2D_GetBodyType(ulong id) => NativeCallbacks.Bindings.Rigidbody2D_GetBodyType(id);
    internal static void Rigidbody2D_SetBodyType(ulong id, int type) => NativeCallbacks.Bindings.Rigidbody2D_SetBodyType(id, type);
    internal static float Rigidbody2D_GetGravityScale(ulong id) => NativeCallbacks.Bindings.Rigidbody2D_GetGravityScale(id);
    internal static void Rigidbody2D_SetGravityScale(ulong id, float scale) => NativeCallbacks.Bindings.Rigidbody2D_SetGravityScale(id, scale);
    internal static float Rigidbody2D_GetMass(ulong id) => NativeCallbacks.Bindings.Rigidbody2D_GetMass(id);
    internal static void Rigidbody2D_SetMass(ulong id, float mass) => NativeCallbacks.Bindings.Rigidbody2D_SetMass(id, mass);

    // Constraints. Bool ↔ int (0/1) at the FFI boundary so the C# side never
    // depends on platform-ABI bool width — same encoding the C++ binding uses.
    internal static bool Rigidbody2D_GetFreezePositionX(ulong id)
        => NativeCallbacks.Bindings.Rigidbody2D_GetFreezePositionX(id) != 0;
    internal static void Rigidbody2D_SetFreezePositionX(ulong id, bool freeze)
        => NativeCallbacks.Bindings.Rigidbody2D_SetFreezePositionX(id, freeze ? 1 : 0);
    internal static bool Rigidbody2D_GetFreezePositionY(ulong id)
        => NativeCallbacks.Bindings.Rigidbody2D_GetFreezePositionY(id) != 0;
    internal static void Rigidbody2D_SetFreezePositionY(ulong id, bool freeze)
        => NativeCallbacks.Bindings.Rigidbody2D_SetFreezePositionY(id, freeze ? 1 : 0);
    internal static bool Rigidbody2D_GetFreezeRotation(ulong id)
        => NativeCallbacks.Bindings.Rigidbody2D_GetFreezeRotation(id) != 0;
    internal static void Rigidbody2D_SetFreezeRotation(ulong id, bool freeze)
        => NativeCallbacks.Bindings.Rigidbody2D_SetFreezeRotation(id, freeze ? 1 : 0);

    // ── BoxCollider2D ───────────────────────────────────────────────

    internal static void BoxCollider2D_GetScale(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.BoxCollider2D_GetScale(id, &ox, &oy); x = ox; y = oy; }
    internal static void BoxCollider2D_GetCenter(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.BoxCollider2D_GetCenter(id, &ox, &oy); x = ox; y = oy; }
    internal static void BoxCollider2D_SetEnabled(ulong id, bool enabled) => NativeCallbacks.Bindings.BoxCollider2D_SetEnabled(id, enabled ? 1 : 0);

    // ── CircleCollider2D ────────────────────────────────────────────

    internal static float CircleCollider2D_GetRadius(ulong id) => NativeCallbacks.Bindings.CircleCollider2D_GetRadius(id);
    internal static void CircleCollider2D_SetRadius(ulong id, float radius) => NativeCallbacks.Bindings.CircleCollider2D_SetRadius(id, radius);
    internal static void CircleCollider2D_GetCenter(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.CircleCollider2D_GetCenter(id, &ox, &oy); x = ox; y = oy; }
    internal static void CircleCollider2D_SetCenter(ulong id, float x, float y) => NativeCallbacks.Bindings.CircleCollider2D_SetCenter(id, x, y);
    internal static void CircleCollider2D_SetEnabled(ulong id, bool enabled) => NativeCallbacks.Bindings.CircleCollider2D_SetEnabled(id, enabled ? 1 : 0);

    // ── PolygonCollider2D ───────────────────────────────────────────

    internal static int PolygonCollider2D_GetVertexCount(ulong id) => NativeCallbacks.Bindings.PolygonCollider2D_GetVertexCount(id);
    internal static int PolygonCollider2D_GetWorldPoints(ulong id, Span<float> outPoints)
    {
        fixed (float* p = outPoints)
        {
            return NativeCallbacks.Bindings.PolygonCollider2D_GetWorldPoints(id, p, outPoints.Length);
        }
    }
    internal static void PolygonCollider2D_SetPoints(ulong id, ReadOnlySpan<float> points, int pointCount)
    {
        fixed (float* p = points)
        {
            NativeCallbacks.Bindings.PolygonCollider2D_SetPoints(id, p, pointCount);
        }
    }
    internal static void PolygonCollider2D_SetSides(ulong id, int sides) => NativeCallbacks.Bindings.PolygonCollider2D_SetSides(id, sides);
    internal static void PolygonCollider2D_GetCenter(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.PolygonCollider2D_GetCenter(id, &ox, &oy); x = ox; y = oy; }
    internal static void PolygonCollider2D_SetCenter(ulong id, float x, float y) => NativeCallbacks.Bindings.PolygonCollider2D_SetCenter(id, x, y);
    internal static void PolygonCollider2D_GetSize(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.PolygonCollider2D_GetSize(id, &ox, &oy); x = ox; y = oy; }
    internal static void PolygonCollider2D_SetSize(ulong id, float x, float y) => NativeCallbacks.Bindings.PolygonCollider2D_SetSize(id, x, y);
    internal static void PolygonCollider2D_SetEnabled(ulong id, bool enabled) => NativeCallbacks.Bindings.PolygonCollider2D_SetEnabled(id, enabled ? 1 : 0);

    // ── AudioSource ─────────────────────────────────────────────────

    internal static void AudioSource_Play(ulong id) => NativeCallbacks.Bindings.AudioSource_Play(id);
    internal static void AudioSource_Pause(ulong id) => NativeCallbacks.Bindings.AudioSource_Pause(id);
    internal static void AudioSource_Stop(ulong id) => NativeCallbacks.Bindings.AudioSource_Stop(id);
    internal static void AudioSource_Resume(ulong id) => NativeCallbacks.Bindings.AudioSource_Resume(id);
    internal static float AudioSource_GetVolume(ulong id) => NativeCallbacks.Bindings.AudioSource_GetVolume(id);
    internal static void AudioSource_SetVolume(ulong id, float v) => NativeCallbacks.Bindings.AudioSource_SetVolume(id, v);
    internal static float AudioSource_GetPitch(ulong id) => NativeCallbacks.Bindings.AudioSource_GetPitch(id);
    internal static void AudioSource_SetPitch(ulong id, float p) => NativeCallbacks.Bindings.AudioSource_SetPitch(id, p);
    internal static bool AudioSource_GetLoop(ulong id) => NativeCallbacks.Bindings.AudioSource_GetLoop(id) != 0;
    internal static void AudioSource_SetLoop(ulong id, bool loop) => NativeCallbacks.Bindings.AudioSource_SetLoop(id, loop ? 1 : 0);
    internal static bool AudioSource_IsPlaying(ulong id) => NativeCallbacks.Bindings.AudioSource_IsPlaying(id) != 0;
    internal static bool AudioSource_IsPaused(ulong id) => NativeCallbacks.Bindings.AudioSource_IsPaused(id) != 0;
    internal static ulong AudioSource_GetAudio(ulong id) => NativeCallbacks.Bindings.AudioSource_GetAudio(id);
    internal static void AudioSource_SetAudio(ulong id, ulong assetId) => NativeCallbacks.Bindings.AudioSource_SetAudio(id, assetId);

    // ── Scene Query ─────────────────────────────────────────────

    internal static int Scene_QueryEntities(string componentNames, Span<ulong> outEntityIDs)
    {
        byte[] buf = EncodeUtf8Z(componentNames);
        fixed (byte* namePtr = buf)
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Scene_QueryEntities(namePtr, idPtr, outEntityIDs.Length);
        }
    }

    internal static int Scene_QueryEntitiesFiltered(
        string withComponents, string withoutComponents, string mustHaveComponents,
        int enableFilter, Span<ulong> outEntityIDs)
    {
        byte[] withBuf = EncodeUtf8Z(withComponents);
        byte[] withoutBuf = EncodeUtf8Z(withoutComponents);
        byte[] mustHaveBuf = EncodeUtf8Z(mustHaveComponents);

        fixed (byte* withPtr = withBuf)
        fixed (byte* withoutPtr = withoutBuf)
        fixed (byte* mustHavePtr = mustHaveBuf)
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Scene_QueryEntitiesFiltered(
                withPtr, withoutPtr, mustHavePtr, enableFilter, idPtr, outEntityIDs.Length);
        }
    }

    internal static int Scene_QueryEntities(string sceneName, string componentNames, Span<ulong> outEntityIDs)
    {
        byte[] sceneBuf = EncodeUtf8Z(sceneName);
        byte[] namesBuf = EncodeUtf8Z(componentNames);
        fixed (byte* scenePtr = sceneBuf)
        fixed (byte* namesPtr = namesBuf)
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Scene_QueryEntitiesInScene(
                scenePtr, namesPtr, idPtr, outEntityIDs.Length);
        }
    }

    internal static int Scene_QueryEntitiesFiltered(
        string sceneName, string withComponents, string withoutComponents,
        string mustHaveComponents, int enableFilter, Span<ulong> outEntityIDs)
    {
        byte[] sceneBuf = EncodeUtf8Z(sceneName);
        byte[] withBuf = EncodeUtf8Z(withComponents);
        byte[] withoutBuf = EncodeUtf8Z(withoutComponents);
        byte[] mustHaveBuf = EncodeUtf8Z(mustHaveComponents);

        fixed (byte* scenePtr = sceneBuf)
        fixed (byte* withPtr = withBuf)
        fixed (byte* withoutPtr = withoutBuf)
        fixed (byte* mustHavePtr = mustHaveBuf)
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Scene_QueryEntitiesFilteredInScene(
                scenePtr, withPtr, withoutPtr, mustHavePtr, enableFilter, idPtr, outEntityIDs.Length);
        }
    }

    // ── ParticleSystem2D ────────────────────────────────────────

    internal static bool Asset_IsValid(ulong assetId) => assetId != 0 && NativeCallbacks.Bindings.Asset_IsValid(assetId) != 0;

    internal static ulong Asset_GetOrCreateUUIDFromPath(string path)
    {
        if (string.IsNullOrEmpty(path))
            return 0;

        byte[] buf = EncodeUtf8Z(path);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Asset_GetOrCreateUUIDFromPath(ptr);
    }

    internal static string Asset_GetPath(ulong assetId)
    {
        return ReadNativeString(NativeCallbacks.Bindings.Asset_GetPathBuffer, assetId);
    }

    internal static string Asset_GetDisplayName(ulong assetId)
    {
        return ReadNativeString(NativeCallbacks.Bindings.Asset_GetDisplayNameBuffer, assetId);
    }

    internal static int Asset_GetKind(ulong assetId) => NativeCallbacks.Bindings.Asset_GetKind(assetId);

    internal static bool DataAsset_EnsureLoaded(ulong guid) => guid != 0 && NativeCallbacks.Bindings.DataAsset_EnsureLoaded(guid) != 0;

    internal static string Asset_FindAll(string pathPrefix, int kind)
    {
        byte[] buf = EncodeUtf8Z(pathPrefix);
        fixed (byte* ptr = buf)
        {
            int required = NativeCallbacks.Bindings.Asset_FindAllBuffer(ptr, kind, null, 0);
            if (required <= 0) return "[]";

            byte[] output = new byte[required + 1];
            fixed (byte* outPtr = output)
            {
                int written = NativeCallbacks.Bindings.Asset_FindAllBuffer(ptr, kind, outPtr, output.Length);
                return DecodeNativeString(output, written);
            }
        }
    }

    internal static bool Texture_LoadAsset(ulong assetId) => assetId != 0 && NativeCallbacks.Bindings.Texture_LoadAsset(assetId) != 0;
    internal static int Texture_GetWidth(ulong assetId) => NativeCallbacks.Bindings.Texture_GetWidth(assetId);
    internal static int Texture_GetHeight(ulong assetId) => NativeCallbacks.Bindings.Texture_GetHeight(assetId);
    internal static ulong Texture_GetDefaultAssetUUID(byte which) => NativeCallbacks.Bindings.Texture_GetDefaultAssetUUID(which);

    internal static unsafe int Image_Decode(byte[] data, int maxSize, byte[] outRgba, out int width, out int height)
    {
        int w = 0, h = 0;
        int result;
        fixed (byte* src = data)
        fixed (byte* dst = outRgba)
            result = NativeCallbacks.Bindings.Image_Decode(src, data.Length, maxSize, dst, outRgba.Length, &w, &h);
        width = w;
        height = h;
        return result;
    }

    internal static unsafe ulong Texture_CreateRuntime(int width, int height, byte[] rgba, int filter)
    {
        fixed (byte* ptr = rgba)
            return NativeCallbacks.Bindings.Texture_CreateRuntime(width, height, ptr, filter);
    }
    internal static unsafe bool Texture_UpdateRuntime(ulong uuid, byte[] rgba)
    {
        fixed (byte* ptr = rgba)
            return NativeCallbacks.Bindings.Texture_UpdateRuntime(uuid, ptr, rgba.Length) != 0;
    }
    internal static void Texture_DestroyRuntime(ulong uuid) => NativeCallbacks.Bindings.Texture_DestroyRuntime(uuid);
    internal static bool Texture_IsRuntime(ulong uuid) => NativeCallbacks.Bindings.Texture_IsRuntime(uuid) != 0;

    internal static bool Audio_LoadAsset(ulong assetId) => assetId != 0 && NativeCallbacks.Bindings.Audio_LoadAsset(assetId) != 0;
    internal static void Audio_PlayOneShotAsset(ulong assetId, float volume) => NativeCallbacks.Bindings.Audio_PlayOneShotAsset(assetId, volume);

    internal static bool Font_LoadAsset(ulong assetId) => assetId != 0 && NativeCallbacks.Bindings.Font_LoadAsset(assetId) != 0;

    internal static void ParticleSystem2D_Play(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_Play(id);
    internal static void ParticleSystem2D_Pause(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_Pause(id);
    internal static void ParticleSystem2D_Stop(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_Stop(id);
    internal static bool ParticleSystem2D_IsPlaying(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_IsPlaying(id) != 0;
    internal static bool ParticleSystem2D_GetPlayOnAwake(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_GetPlayOnAwake(id) != 0;
    internal static void ParticleSystem2D_SetPlayOnAwake(ulong id, bool enabled) => NativeCallbacks.Bindings.ParticleSystem2D_SetPlayOnAwake(id, enabled ? 1 : 0);
    internal static void ParticleSystem2D_GetColor(ulong id, out float r, out float g, out float b, out float a) { float cr, cg, cb, ca; NativeCallbacks.Bindings.ParticleSystem2D_GetColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void ParticleSystem2D_SetColor(ulong id, float r, float g, float b, float a) => NativeCallbacks.Bindings.ParticleSystem2D_SetColor(id, r, g, b, a);
    internal static ulong ParticleSystem2D_GetTexture(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_GetTexture(id);
    internal static void ParticleSystem2D_SetTexture(ulong id, ulong assetId) => NativeCallbacks.Bindings.ParticleSystem2D_SetTexture(id, assetId);
    internal static float ParticleSystem2D_GetLifeTime(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_GetLifeTime(id);
    internal static void ParticleSystem2D_SetLifeTime(ulong id, float v) => NativeCallbacks.Bindings.ParticleSystem2D_SetLifeTime(id, v);
    internal static float ParticleSystem2D_GetSpeed(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_GetSpeed(id);
    internal static void ParticleSystem2D_SetSpeed(ulong id, float v) => NativeCallbacks.Bindings.ParticleSystem2D_SetSpeed(id, v);
    internal static void ParticleSystem2D_GetScale(ulong id, out float x, out float y) { float ox, oy; NativeCallbacks.Bindings.ParticleSystem2D_GetScale(id, &ox, &oy); x = ox; y = oy; }
    internal static void ParticleSystem2D_SetScale(ulong id, float x, float y) => NativeCallbacks.Bindings.ParticleSystem2D_SetScale(id, x, y);
    internal static int ParticleSystem2D_GetEmitOverTime(ulong id) => NativeCallbacks.Bindings.ParticleSystem2D_GetEmitOverTime(id);
    internal static void ParticleSystem2D_SetEmitOverTime(ulong id, int v) => NativeCallbacks.Bindings.ParticleSystem2D_SetEmitOverTime(id, v);
    internal static void ParticleSystem2D_Emit(ulong id, int count) => NativeCallbacks.Bindings.ParticleSystem2D_Emit(id, count);

    // ── Gizmos ──────────────────────────────────────────────────────

    internal static void Gizmo_DrawLine(float x1, float y1, float x2, float y2) => NativeCallbacks.Bindings.Gizmo_DrawLine(x1, y1, x2, y2);
    internal static void Gizmo_DrawWireSquare(float cx, float cy, float sx, float sy, float deg) => NativeCallbacks.Bindings.Gizmo_DrawWireSquare(cx, cy, sx, sy, deg);
    internal static void Gizmo_DrawWireCircle(float cx, float cy, float r, int seg) => NativeCallbacks.Bindings.Gizmo_DrawWireCircle(cx, cy, r, seg);
    internal static void Gizmo_DrawSquare(float cx, float cy, float sx, float sy, float deg) => NativeCallbacks.Bindings.Gizmo_DrawSquare(cx, cy, sx, sy, deg);
    internal static void Gizmo_DrawCircle(float cx, float cy, float r, int seg) => NativeCallbacks.Bindings.Gizmo_DrawCircle(cx, cy, r, seg);
    internal static void Gizmo_DrawText(string text, float x, float y, float rotation, float size)
    {
        byte[] buf = EncodeUtf8Z(text);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.Gizmo_DrawText(ptr, x, y, rotation, size);
    }
    internal static void Gizmo_SetColor(float r, float g, float b, float a) => NativeCallbacks.Bindings.Gizmo_SetColor(r, g, b, a);
    internal static void Gizmo_GetColor(out float r, out float g, out float b, out float a) { float cr, cg, cb, ca; NativeCallbacks.Bindings.Gizmo_GetColor(&cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static float Gizmo_GetLineWidth() => NativeCallbacks.Bindings.Gizmo_GetLineWidth();
    internal static void Gizmo_SetLineWidth(float w) => NativeCallbacks.Bindings.Gizmo_SetLineWidth(w);
    internal static void Gizmo_SetMaxVertices(int maxVertices) => NativeCallbacks.Bindings.Gizmo_SetMaxVertices(maxVertices);
    internal static int Gizmo_GetMaxVertices() => NativeCallbacks.Bindings.Gizmo_GetMaxVertices();
    internal static int Gizmo_GetRegisteredVertices() => NativeCallbacks.Bindings.Gizmo_GetRegisteredVertices();
    internal static bool Gizmo_GetEnabled() => NativeCallbacks.Bindings.Gizmo_GetEnabled() != 0;
    internal static void Gizmo_SetEnabled(bool enabled) => NativeCallbacks.Bindings.Gizmo_SetEnabled(enabled ? 1 : 0);

    // ── Physics2D ───────────────────────────────────────────────────

    internal static bool Physics2D_Raycast(float originX, float originY, float dirX, float dirY, float distance,
        out ulong hitEntityID, out float hitX, out float hitY, out float hitNormalX, out float hitNormalY, out float hitDistance)
    {
        ulong eid; float hx, hy, hnx, hny, hd;
        int result = NativeCallbacks.Bindings.Physics2D_Raycast(originX, originY, dirX, dirY, distance, &eid, &hx, &hy, &hnx, &hny, &hd);
        hitEntityID = eid; hitX = hx; hitY = hy; hitNormalX = hnx; hitNormalY = hny; hitDistance = hd;
        return result != 0;
    }

    internal static bool Physics2D_OverlapCircle(float originX, float originY, float radius, int mode, out ulong entityID)
    {
        ulong id;
        int result = NativeCallbacks.Bindings.Physics2D_OverlapCircle(originX, originY, radius, mode, &id);
        entityID = id;
        return result != 0 && id != 0;
    }

    internal static bool Physics2D_OverlapBox(float originX, float originY, float halfX, float halfY, float degrees, int mode, out ulong entityID)
    {
        ulong id;
        int result = NativeCallbacks.Bindings.Physics2D_OverlapBox(originX, originY, halfX, halfY, degrees, mode, &id);
        entityID = id;
        return result != 0 && id != 0;
    }

    internal static bool Physics2D_OverlapPolygon(float originX, float originY, float[] points, int mode, out ulong entityID)
    {
        ulong id;
        fixed (float* pointsPtr = points)
        {
            int result = NativeCallbacks.Bindings.Physics2D_OverlapPolygon(originX, originY, pointsPtr, points.Length / 2, mode, &id);
            entityID = id;
            return result != 0 && id != 0;
        }
    }

    internal static int Physics2D_OverlapCircleAll(float originX, float originY, float radius, Span<ulong> outEntityIDs)
    {
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Physics2D_OverlapCircleAll(originX, originY, radius, idPtr, outEntityIDs.Length);
        }
    }

    internal static int Physics2D_OverlapBoxAll(float originX, float originY, float halfX, float halfY, float degrees, Span<ulong> outEntityIDs)
    {
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Physics2D_OverlapBoxAll(originX, originY, halfX, halfY, degrees, idPtr, outEntityIDs.Length);
        }
    }

    internal static int Physics2D_OverlapPolygonAll(float originX, float originY, float[] points, Span<ulong> outEntityIDs)
    {
        fixed (float* pointsPtr = points)
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Physics2D_OverlapPolygonAll(originX, originY, pointsPtr, points.Length / 2, idPtr, outEntityIDs.Length);
        }
    }

    internal static bool Physics2D_ContainsPoint(float originX, float originY, int mode, out ulong entityID)
    {
        ulong id;
        int result = NativeCallbacks.Bindings.Physics2D_ContainsPoint(originX, originY, mode, &id);
        entityID = id;
        return result != 0 && id != 0;
    }

    internal static int Physics2D_ContainsPointAll(float originX, float originY, Span<ulong> outEntityIDs)
    {
        fixed (ulong* idPtr = outEntityIDs)
        {
            return NativeCallbacks.Bindings.Physics2D_ContainsPointAll(originX, originY, idPtr, outEntityIDs.Length);
        }
    }

    // ── EntityPicker ─────────────────────────────────────────────────

    internal static bool EntityPicker_TryPickEntity(float worldX, float worldY, out ulong entityID)
    {
        ulong id;
        int result = NativeCallbacks.Bindings.EntityPicker_TryPickEntity(worldX, worldY, &id);
        entityID = id;
        return result != 0 && id != 0;
    }

    internal static bool UI_GetHoveredEntity(out ulong entityID)
    {
        ulong id;
        int result = NativeCallbacks.Bindings.UI_GetHoveredEntity(&id);
        entityID = id;
        return result != 0 && id != 0;
    }

    // ── UI: RectTransform2D ──────────────────────────────────────────

    internal static void RectTransform_GetAnchorMin(ulong id, out float x, out float y)
    { float ox, oy; NativeCallbacks.Bindings.RectTransform_GetAnchorMin(id, &ox, &oy); x = ox; y = oy; }
    internal static void RectTransform_SetAnchorMin(ulong id, float x, float y)
        => NativeCallbacks.Bindings.RectTransform_SetAnchorMin(id, x, y);
    internal static void RectTransform_GetAnchorMax(ulong id, out float x, out float y)
    { float ox, oy; NativeCallbacks.Bindings.RectTransform_GetAnchorMax(id, &ox, &oy); x = ox; y = oy; }
    internal static void RectTransform_SetAnchorMax(ulong id, float x, float y)
        => NativeCallbacks.Bindings.RectTransform_SetAnchorMax(id, x, y);
    internal static void RectTransform_GetPivot(ulong id, out float x, out float y)
    { float ox, oy; NativeCallbacks.Bindings.RectTransform_GetPivot(id, &ox, &oy); x = ox; y = oy; }
    internal static void RectTransform_SetPivot(ulong id, float x, float y)
        => NativeCallbacks.Bindings.RectTransform_SetPivot(id, x, y);
    internal static void RectTransform_GetAnchoredPosition(ulong id, out float x, out float y)
    { float ox, oy; NativeCallbacks.Bindings.RectTransform_GetAnchoredPosition(id, &ox, &oy); x = ox; y = oy; }
    internal static void RectTransform_SetAnchoredPosition(ulong id, float x, float y)
        => NativeCallbacks.Bindings.RectTransform_SetAnchoredPosition(id, x, y);
    internal static void RectTransform_GetSizeDelta(ulong id, out float x, out float y)
    { float ox, oy; NativeCallbacks.Bindings.RectTransform_GetSizeDelta(id, &ox, &oy); x = ox; y = oy; }
    internal static void RectTransform_SetSizeDelta(ulong id, float x, float y)
        => NativeCallbacks.Bindings.RectTransform_SetSizeDelta(id, x, y);
    internal static float RectTransform_GetRotation(ulong id) => NativeCallbacks.Bindings.RectTransform_GetRotation(id);
    internal static void RectTransform_SetRotation(ulong id, float r) => NativeCallbacks.Bindings.RectTransform_SetRotation(id, r);
    internal static void RectTransform_GetScale(ulong id, out float x, out float y)
    { float ox, oy; NativeCallbacks.Bindings.RectTransform_GetScale(id, &ox, &oy); x = ox; y = oy; }
    internal static void RectTransform_SetScale(ulong id, float x, float y)
        => NativeCallbacks.Bindings.RectTransform_SetScale(id, x, y);
    internal static float RectTransform_GetLocalRotation(ulong id) => NativeCallbacks.Bindings.RectTransform_GetLocalRotation(id);
    internal static void RectTransform_SetLocalRotation(ulong id, float r) => NativeCallbacks.Bindings.RectTransform_SetLocalRotation(id, r);
    internal static void RectTransform_GetLocalScale(ulong id, out float x, out float y)
    { float ox, oy; NativeCallbacks.Bindings.RectTransform_GetLocalScale(id, &ox, &oy); x = ox; y = oy; }
    internal static void RectTransform_SetLocalScale(ulong id, float x, float y)
        => NativeCallbacks.Bindings.RectTransform_SetLocalScale(id, x, y);
    internal static void RectTransform_GetResolvedSize(ulong id, out float w, out float h)
    { float ow, oh; NativeCallbacks.Bindings.RectTransform_GetResolvedSize(id, &ow, &oh); w = ow; h = oh; }
    internal static void RectTransform_GetScreenPosition(ulong id, out float x, out float y)
    { float ox, oy; NativeCallbacks.Bindings.RectTransform_GetScreenPosition(id, &ox, &oy); x = ox; y = oy; }
    internal static void RectTransform_SetScreenPosition(ulong id, float x, float y)
        => NativeCallbacks.Bindings.RectTransform_SetScreenPosition(id, x, y);

    // ── UI: Image ────────────────────────────────────────────────────

    internal static void Image_GetColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Image_GetColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Image_SetColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Image_SetColor(id, r, g, b, a);
    internal static ulong Image_GetTexture(ulong id) => NativeCallbacks.Bindings.Image_GetTexture(id);
    internal static void Image_SetTexture(ulong id, ulong assetId) => NativeCallbacks.Bindings.Image_SetTexture(id, assetId);
    internal static string Image_GetSpriteName(ulong id)
        => ReadNativeString(NativeCallbacks.Bindings.Image_GetSpriteNameBuffer, id);
    internal static void Image_SetSpriteName(ulong id, string? name)
    {
        byte[] buf = EncodeUtf8Z(name ?? string.Empty);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.Image_SetSpriteName(id, ptr);
    }
    internal static int Image_GetSortingOrder(ulong id) => NativeCallbacks.Bindings.Image_GetSortingOrder(id);
    internal static void Image_SetSortingOrder(ulong id, int order) => NativeCallbacks.Bindings.Image_SetSortingOrder(id, order);
    internal static int Image_GetSortingLayer(ulong id) => NativeCallbacks.Bindings.Image_GetSortingLayer(id);
    internal static void Image_SetSortingLayer(ulong id, int layer) => NativeCallbacks.Bindings.Image_SetSortingLayer(id, layer);
    internal static ulong Image_GetMaterial(ulong id) => NativeCallbacks.Bindings.Image_GetMaterial(id);
    internal static void Image_SetMaterial(ulong id, ulong materialGuid) => NativeCallbacks.Bindings.Image_SetMaterial(id, materialGuid);
    internal const int ImageSlicingValueCount = 6;
    internal static void Image_GetSlicing(ulong id, Span<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.Image_GetSlicing(id, p, values.Length);
    }
    internal static void Image_SetSlicing(ulong id, ReadOnlySpan<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.Image_SetSlicing(id, p, values.Length);
    }

    // ── UI: Interactable ─────────────────────────────────────────────

    internal static bool Interactable_GetInteractable(ulong id) => NativeCallbacks.Bindings.Interactable_GetInteractable(id) != 0;
    internal static void Interactable_SetInteractable(ulong id, bool v) => NativeCallbacks.Bindings.Interactable_SetInteractable(id, v ? 1 : 0);
    internal static bool Interactable_GetIsHovered(ulong id) => NativeCallbacks.Bindings.Interactable_GetIsHovered(id) != 0;
    internal static bool Interactable_GetIsClicked(ulong id) => NativeCallbacks.Bindings.Interactable_GetIsClicked(id) != 0;
    internal static bool Interactable_GetIsPressed(ulong id) => NativeCallbacks.Bindings.Interactable_GetIsPressed(id) != 0;
    internal static bool Interactable_GetIsMouseDown(ulong id) => NativeCallbacks.Bindings.Interactable_GetIsMouseDown(id) != 0;
    internal static bool Interactable_GetIsMouseUp(ulong id) => NativeCallbacks.Bindings.Interactable_GetIsMouseUp(id) != 0;
    internal static bool Interactable_GetFocusable(ulong id) => NativeCallbacks.Bindings.Interactable_GetFocusable(id) != 0;
    internal static void Interactable_SetFocusable(ulong id, bool v) => NativeCallbacks.Bindings.Interactable_SetFocusable(id, v ? 1 : 0);
    internal static bool Interactable_GetIsFocused(ulong id) => NativeCallbacks.Bindings.Interactable_GetIsFocused(id) != 0;
    internal static void Interactable_SetIsFocused(ulong id, bool v) => NativeCallbacks.Bindings.Interactable_SetIsFocused(id, v ? 1 : 0);

    // ── UI: Button ───────────────────────────────────────────────────

    internal static void Button_GetNormalColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Button_GetNormalColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Button_SetNormalColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Button_SetNormalColor(id, r, g, b, a);
    internal static void Button_GetHoveredColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Button_GetHoveredColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Button_SetHoveredColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Button_SetHoveredColor(id, r, g, b, a);
    internal static void Button_GetPressedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Button_GetPressedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Button_SetPressedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Button_SetPressedColor(id, r, g, b, a);
    internal static void Button_GetDisabledColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Button_GetDisabledColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Button_SetDisabledColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Button_SetDisabledColor(id, r, g, b, a);
    internal static void Button_GetFocusedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Button_GetFocusedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Button_SetFocusedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Button_SetFocusedColor(id, r, g, b, a);
    internal static int Button_GetTransitionMode(ulong id) => NativeCallbacks.Bindings.Button_GetTransitionMode(id);
    internal static void Button_SetTransitionMode(ulong id, int mode) => NativeCallbacks.Bindings.Button_SetTransitionMode(id, mode);
    internal static ulong Button_GetNormalSprite(ulong id) => NativeCallbacks.Bindings.Button_GetNormalSprite(id);
    internal static void Button_SetNormalSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Button_SetNormalSprite(id, uuid);
    internal static ulong Button_GetHoveredSprite(ulong id) => NativeCallbacks.Bindings.Button_GetHoveredSprite(id);
    internal static void Button_SetHoveredSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Button_SetHoveredSprite(id, uuid);
    internal static ulong Button_GetPressedSprite(ulong id) => NativeCallbacks.Bindings.Button_GetPressedSprite(id);
    internal static void Button_SetPressedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Button_SetPressedSprite(id, uuid);
    internal static ulong Button_GetDisabledSprite(ulong id) => NativeCallbacks.Bindings.Button_GetDisabledSprite(id);
    internal static void Button_SetDisabledSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Button_SetDisabledSprite(id, uuid);
    internal static ulong Button_GetFocusedSprite(ulong id) => NativeCallbacks.Bindings.Button_GetFocusedSprite(id);
    internal static void Button_SetFocusedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Button_SetFocusedSprite(id, uuid);

    // ── UI: Slider ───────────────────────────────────────────────────

    internal static float Slider_GetValue(ulong id) => NativeCallbacks.Bindings.Slider_GetValue(id);
    internal static void Slider_SetValue(ulong id, float v) => NativeCallbacks.Bindings.Slider_SetValue(id, v);
    internal static float Slider_GetMinValue(ulong id) => NativeCallbacks.Bindings.Slider_GetMinValue(id);
    internal static void Slider_SetMinValue(ulong id, float v) => NativeCallbacks.Bindings.Slider_SetMinValue(id, v);
    internal static float Slider_GetMaxValue(ulong id) => NativeCallbacks.Bindings.Slider_GetMaxValue(id);
    internal static void Slider_SetMaxValue(ulong id, float v) => NativeCallbacks.Bindings.Slider_SetMaxValue(id, v);
    internal static bool Slider_GetWholeNumbers(ulong id) => NativeCallbacks.Bindings.Slider_GetWholeNumbers(id) != 0;
    internal static void Slider_SetWholeNumbers(ulong id, bool v) => NativeCallbacks.Bindings.Slider_SetWholeNumbers(id, v ? 1 : 0);
    internal static bool Slider_GetValueChangedThisFrame(ulong id) => NativeCallbacks.Bindings.Slider_GetValueChangedThisFrame(id) != 0;
    internal static void Slider_MarkValueObserved(ulong id) => NativeCallbacks.Bindings.Slider_MarkValueObserved(id);
    internal static void Slider_GetNormalColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Slider_GetNormalColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Slider_SetNormalColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Slider_SetNormalColor(id, r, g, b, a);
    internal static void Slider_GetHoveredColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Slider_GetHoveredColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Slider_SetHoveredColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Slider_SetHoveredColor(id, r, g, b, a);
    internal static void Slider_GetPressedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Slider_GetPressedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Slider_SetPressedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Slider_SetPressedColor(id, r, g, b, a);
    internal static void Slider_GetDisabledColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Slider_GetDisabledColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Slider_SetDisabledColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Slider_SetDisabledColor(id, r, g, b, a);
    internal static void Slider_GetFocusedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Slider_GetFocusedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Slider_SetFocusedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Slider_SetFocusedColor(id, r, g, b, a);
    internal static int Slider_GetTransitionMode(ulong id) => NativeCallbacks.Bindings.Slider_GetTransitionMode(id);
    internal static void Slider_SetTransitionMode(ulong id, int mode) => NativeCallbacks.Bindings.Slider_SetTransitionMode(id, mode);
    internal static ulong Slider_GetNormalSprite(ulong id) => NativeCallbacks.Bindings.Slider_GetNormalSprite(id);
    internal static void Slider_SetNormalSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Slider_SetNormalSprite(id, uuid);
    internal static ulong Slider_GetHoveredSprite(ulong id) => NativeCallbacks.Bindings.Slider_GetHoveredSprite(id);
    internal static void Slider_SetHoveredSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Slider_SetHoveredSprite(id, uuid);
    internal static ulong Slider_GetPressedSprite(ulong id) => NativeCallbacks.Bindings.Slider_GetPressedSprite(id);
    internal static void Slider_SetPressedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Slider_SetPressedSprite(id, uuid);
    internal static ulong Slider_GetDisabledSprite(ulong id) => NativeCallbacks.Bindings.Slider_GetDisabledSprite(id);
    internal static void Slider_SetDisabledSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Slider_SetDisabledSprite(id, uuid);
    internal static ulong Slider_GetFocusedSprite(ulong id) => NativeCallbacks.Bindings.Slider_GetFocusedSprite(id);
    internal static void Slider_SetFocusedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Slider_SetFocusedSprite(id, uuid);

    // ── UI: Toggle ───────────────────────────────────────────────────

    internal static bool Toggle_GetIsOn(ulong id) => NativeCallbacks.Bindings.Toggle_GetIsOn(id) != 0;
    internal static void Toggle_SetIsOn(ulong id, bool v) => NativeCallbacks.Bindings.Toggle_SetIsOn(id, v ? 1 : 0);
    internal static bool Toggle_GetValueChangedThisFrame(ulong id) => NativeCallbacks.Bindings.Toggle_GetValueChangedThisFrame(id) != 0;
    internal static void Toggle_MarkIsOnObserved(ulong id) => NativeCallbacks.Bindings.Toggle_MarkIsOnObserved(id);
    internal static void Toggle_GetNormalColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Toggle_GetNormalColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Toggle_SetNormalColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Toggle_SetNormalColor(id, r, g, b, a);
    internal static void Toggle_GetHoveredColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Toggle_GetHoveredColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Toggle_SetHoveredColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Toggle_SetHoveredColor(id, r, g, b, a);
    internal static void Toggle_GetPressedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Toggle_GetPressedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Toggle_SetPressedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Toggle_SetPressedColor(id, r, g, b, a);
    internal static void Toggle_GetDisabledColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Toggle_GetDisabledColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Toggle_SetDisabledColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Toggle_SetDisabledColor(id, r, g, b, a);
    internal static void Toggle_GetFocusedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Toggle_GetFocusedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Toggle_SetFocusedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Toggle_SetFocusedColor(id, r, g, b, a);
    internal static int Toggle_GetTransitionMode(ulong id) => NativeCallbacks.Bindings.Toggle_GetTransitionMode(id);
    internal static void Toggle_SetTransitionMode(ulong id, int mode) => NativeCallbacks.Bindings.Toggle_SetTransitionMode(id, mode);
    internal static ulong Toggle_GetNormalSprite(ulong id) => NativeCallbacks.Bindings.Toggle_GetNormalSprite(id);
    internal static void Toggle_SetNormalSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Toggle_SetNormalSprite(id, uuid);
    internal static ulong Toggle_GetHoveredSprite(ulong id) => NativeCallbacks.Bindings.Toggle_GetHoveredSprite(id);
    internal static void Toggle_SetHoveredSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Toggle_SetHoveredSprite(id, uuid);
    internal static ulong Toggle_GetPressedSprite(ulong id) => NativeCallbacks.Bindings.Toggle_GetPressedSprite(id);
    internal static void Toggle_SetPressedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Toggle_SetPressedSprite(id, uuid);
    internal static ulong Toggle_GetDisabledSprite(ulong id) => NativeCallbacks.Bindings.Toggle_GetDisabledSprite(id);
    internal static void Toggle_SetDisabledSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Toggle_SetDisabledSprite(id, uuid);
    internal static ulong Toggle_GetFocusedSprite(ulong id) => NativeCallbacks.Bindings.Toggle_GetFocusedSprite(id);
    internal static void Toggle_SetFocusedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Toggle_SetFocusedSprite(id, uuid);

    // ── UI: InputField ───────────────────────────────────────────────

    internal static string InputField_GetText(ulong id)
        => ReadNativeString(NativeCallbacks.Bindings.InputField_GetTextBuffer, id);
    internal static void InputField_SetText(ulong id, string text)
    {
        byte[] buf = EncodeUtf8Z(text);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.InputField_SetText(id, ptr);
    }
    internal static string InputField_GetPlaceholderText(ulong id)
        => ReadNativeString(NativeCallbacks.Bindings.InputField_GetPlaceholderTextBuffer, id);
    internal static void InputField_SetPlaceholderText(ulong id, string text)
    {
        byte[] buf = EncodeUtf8Z(text);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.InputField_SetPlaceholderText(id, ptr);
    }
    internal static bool InputField_GetIsFocused(ulong id) => NativeCallbacks.Bindings.InputField_GetIsFocused(id) != 0;
    internal static void InputField_SetIsFocused(ulong id, bool v) => NativeCallbacks.Bindings.InputField_SetIsFocused(id, v ? 1 : 0);
    internal static bool InputField_GetSubmittedThisFrame(ulong id) => NativeCallbacks.Bindings.InputField_GetSubmittedThisFrame(id) != 0;
    internal static int InputField_GetCharacterLimit(ulong id) => NativeCallbacks.Bindings.InputField_GetCharacterLimit(id);
    internal static void InputField_SetCharacterLimit(ulong id, int v) => NativeCallbacks.Bindings.InputField_SetCharacterLimit(id, v);
    internal static bool InputField_GetMultiline(ulong id) => NativeCallbacks.Bindings.InputField_GetMultiline(id) != 0;
    internal static void InputField_SetMultiline(ulong id, bool v) => NativeCallbacks.Bindings.InputField_SetMultiline(id, v ? 1 : 0);
    internal static ulong InputField_GetVerticalScrollbarEntity(ulong id) => NativeCallbacks.Bindings.InputField_GetVerticalScrollbarEntity(id);
    internal static void InputField_SetVerticalScrollbarEntity(ulong id, ulong refUuid) => NativeCallbacks.Bindings.InputField_SetVerticalScrollbarEntity(id, refUuid);
    internal static void InputField_GetNormalColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.InputField_GetNormalColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void InputField_SetNormalColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.InputField_SetNormalColor(id, r, g, b, a);
    internal static void InputField_GetHoveredColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.InputField_GetHoveredColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void InputField_SetHoveredColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.InputField_SetHoveredColor(id, r, g, b, a);
    internal static void InputField_GetPressedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.InputField_GetPressedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void InputField_SetPressedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.InputField_SetPressedColor(id, r, g, b, a);
    internal static void InputField_GetDisabledColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.InputField_GetDisabledColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void InputField_SetDisabledColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.InputField_SetDisabledColor(id, r, g, b, a);
    internal static void InputField_GetFocusedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.InputField_GetFocusedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void InputField_SetFocusedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.InputField_SetFocusedColor(id, r, g, b, a);
    internal static int InputField_GetTransitionMode(ulong id) => NativeCallbacks.Bindings.InputField_GetTransitionMode(id);
    internal static void InputField_SetTransitionMode(ulong id, int mode) => NativeCallbacks.Bindings.InputField_SetTransitionMode(id, mode);
    internal static ulong InputField_GetNormalSprite(ulong id) => NativeCallbacks.Bindings.InputField_GetNormalSprite(id);
    internal static void InputField_SetNormalSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.InputField_SetNormalSprite(id, uuid);
    internal static ulong InputField_GetHoveredSprite(ulong id) => NativeCallbacks.Bindings.InputField_GetHoveredSprite(id);
    internal static void InputField_SetHoveredSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.InputField_SetHoveredSprite(id, uuid);
    internal static ulong InputField_GetPressedSprite(ulong id) => NativeCallbacks.Bindings.InputField_GetPressedSprite(id);
    internal static void InputField_SetPressedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.InputField_SetPressedSprite(id, uuid);
    internal static ulong InputField_GetDisabledSprite(ulong id) => NativeCallbacks.Bindings.InputField_GetDisabledSprite(id);
    internal static void InputField_SetDisabledSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.InputField_SetDisabledSprite(id, uuid);
    internal static ulong InputField_GetFocusedSprite(ulong id) => NativeCallbacks.Bindings.InputField_GetFocusedSprite(id);
    internal static void InputField_SetFocusedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.InputField_SetFocusedSprite(id, uuid);

    // ── UI: Dropdown ─────────────────────────────────────────────────

    internal static int Dropdown_GetSelectedIndex(ulong id) => NativeCallbacks.Bindings.Dropdown_GetSelectedIndex(id);
    internal static void Dropdown_SetSelectedIndex(ulong id, int v) => NativeCallbacks.Bindings.Dropdown_SetSelectedIndex(id, v);
    internal static bool Dropdown_GetIsOpen(ulong id) => NativeCallbacks.Bindings.Dropdown_GetIsOpen(id) != 0;
    internal static void Dropdown_SetIsOpen(ulong id, bool v) => NativeCallbacks.Bindings.Dropdown_SetIsOpen(id, v ? 1 : 0);
    internal static bool Dropdown_GetSelectionChangedThisFrame(ulong id) => NativeCallbacks.Bindings.Dropdown_GetSelectionChangedThisFrame(id) != 0;
    internal static void Dropdown_MarkSelectedIndexObserved(ulong id) => NativeCallbacks.Bindings.Dropdown_MarkSelectedIndexObserved(id);
    internal static int Dropdown_GetOptionCount(ulong id) => NativeCallbacks.Bindings.Dropdown_GetOptionCount(id);
    internal static string Dropdown_GetOption(ulong id, int index)
        => ReadNativeStringIndexed(NativeCallbacks.Bindings.Dropdown_GetOptionBuffer, id, index);
    internal static void Dropdown_SetOption(ulong id, int index, string text)
    {
        byte[] buf = EncodeUtf8Z(text);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.Dropdown_SetOption(id, index, ptr);
    }
    internal static void Dropdown_AddOption(ulong id, string text)
    {
        byte[] buf = EncodeUtf8Z(text);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.Dropdown_AddOption(id, ptr);
    }
    internal static void Dropdown_RemoveOption(ulong id, int index) => NativeCallbacks.Bindings.Dropdown_RemoveOption(id, index);
    internal static void Dropdown_ClearOptions(ulong id) => NativeCallbacks.Bindings.Dropdown_ClearOptions(id);
    internal static void Dropdown_GetNormalColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Dropdown_GetNormalColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Dropdown_SetNormalColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Dropdown_SetNormalColor(id, r, g, b, a);
    internal static void Dropdown_GetHoveredColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Dropdown_GetHoveredColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Dropdown_SetHoveredColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Dropdown_SetHoveredColor(id, r, g, b, a);
    internal static void Dropdown_GetPressedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Dropdown_GetPressedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Dropdown_SetPressedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Dropdown_SetPressedColor(id, r, g, b, a);
    internal static void Dropdown_GetDisabledColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Dropdown_GetDisabledColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Dropdown_SetDisabledColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Dropdown_SetDisabledColor(id, r, g, b, a);
    internal static void Dropdown_GetFocusedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Dropdown_GetFocusedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Dropdown_SetFocusedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Dropdown_SetFocusedColor(id, r, g, b, a);
    internal static int Dropdown_GetTransitionMode(ulong id) => NativeCallbacks.Bindings.Dropdown_GetTransitionMode(id);
    internal static void Dropdown_SetTransitionMode(ulong id, int mode) => NativeCallbacks.Bindings.Dropdown_SetTransitionMode(id, mode);
    internal static ulong Dropdown_GetNormalSprite(ulong id) => NativeCallbacks.Bindings.Dropdown_GetNormalSprite(id);
    internal static void Dropdown_SetNormalSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Dropdown_SetNormalSprite(id, uuid);
    internal static ulong Dropdown_GetHoveredSprite(ulong id) => NativeCallbacks.Bindings.Dropdown_GetHoveredSprite(id);
    internal static void Dropdown_SetHoveredSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Dropdown_SetHoveredSprite(id, uuid);
    internal static ulong Dropdown_GetPressedSprite(ulong id) => NativeCallbacks.Bindings.Dropdown_GetPressedSprite(id);
    internal static void Dropdown_SetPressedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Dropdown_SetPressedSprite(id, uuid);
    internal static ulong Dropdown_GetDisabledSprite(ulong id) => NativeCallbacks.Bindings.Dropdown_GetDisabledSprite(id);
    internal static void Dropdown_SetDisabledSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Dropdown_SetDisabledSprite(id, uuid);
    internal static ulong Dropdown_GetFocusedSprite(ulong id) => NativeCallbacks.Bindings.Dropdown_GetFocusedSprite(id);
    internal static void Dropdown_SetFocusedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Dropdown_SetFocusedSprite(id, uuid);

    // ── UI: IsReadOnly + entity-ref + popup-option colors ───────────
    internal static bool Toggle_GetIsReadOnly(ulong id) => NativeCallbacks.Bindings.Toggle_GetIsReadOnly(id) != 0;
    internal static void Toggle_SetIsReadOnly(ulong id, bool v) => NativeCallbacks.Bindings.Toggle_SetIsReadOnly(id, v ? 1 : 0);
    internal static bool Slider_GetIsReadOnly(ulong id) => NativeCallbacks.Bindings.Slider_GetIsReadOnly(id) != 0;
    internal static void Slider_SetIsReadOnly(ulong id, bool v) => NativeCallbacks.Bindings.Slider_SetIsReadOnly(id, v ? 1 : 0);
    internal static bool Dropdown_GetIsReadOnly(ulong id) => NativeCallbacks.Bindings.Dropdown_GetIsReadOnly(id) != 0;
    internal static void Dropdown_SetIsReadOnly(ulong id, bool v) => NativeCallbacks.Bindings.Dropdown_SetIsReadOnly(id, v ? 1 : 0);

    internal static ulong Button_GetTargetGraphic(ulong id) => NativeCallbacks.Bindings.Button_GetTargetGraphic(id);
    internal static void Button_SetTargetGraphic(ulong id, ulong refUuid) => NativeCallbacks.Bindings.Button_SetTargetGraphic(id, refUuid);
    internal static ulong Slider_GetFillEntity(ulong id) => NativeCallbacks.Bindings.Slider_GetFillEntity(id);
    internal static void Slider_SetFillEntity(ulong id, ulong refUuid) => NativeCallbacks.Bindings.Slider_SetFillEntity(id, refUuid);
    internal static ulong Slider_GetHandleEntity(ulong id) => NativeCallbacks.Bindings.Slider_GetHandleEntity(id);
    internal static void Slider_SetHandleEntity(ulong id, ulong refUuid) => NativeCallbacks.Bindings.Slider_SetHandleEntity(id, refUuid);
    internal static ulong Slider_GetBackgroundEntity(ulong id) => NativeCallbacks.Bindings.Slider_GetBackgroundEntity(id);
    internal static void Slider_SetBackgroundEntity(ulong id, ulong refUuid) => NativeCallbacks.Bindings.Slider_SetBackgroundEntity(id, refUuid);
    internal static ulong Toggle_GetCheckmarkEntity(ulong id) => NativeCallbacks.Bindings.Toggle_GetCheckmarkEntity(id);
    internal static void Toggle_SetCheckmarkEntity(ulong id, ulong refUuid) => NativeCallbacks.Bindings.Toggle_SetCheckmarkEntity(id, refUuid);
    internal static ulong InputField_GetTextEntity(ulong id) => NativeCallbacks.Bindings.InputField_GetTextEntity(id);
    internal static void InputField_SetTextEntity(ulong id, ulong refUuid) => NativeCallbacks.Bindings.InputField_SetTextEntity(id, refUuid);
    internal static ulong Dropdown_GetLabelEntity(ulong id) => NativeCallbacks.Bindings.Dropdown_GetLabelEntity(id);
    internal static void Dropdown_SetLabelEntity(ulong id, ulong refUuid) => NativeCallbacks.Bindings.Dropdown_SetLabelEntity(id, refUuid);

    internal static void Dropdown_GetOptionNormalColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Dropdown_GetOptionNormalColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Dropdown_SetOptionNormalColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Dropdown_SetOptionNormalColor(id, r, g, b, a);
    internal static void Dropdown_GetOptionHoverColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Dropdown_GetOptionHoverColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Dropdown_SetOptionHoverColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Dropdown_SetOptionHoverColor(id, r, g, b, a);
    internal static void Dropdown_GetOptionPressedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Dropdown_GetOptionPressedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Dropdown_SetOptionPressedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Dropdown_SetOptionPressedColor(id, r, g, b, a);
    internal static void Dropdown_GetOptionSelectedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Dropdown_GetOptionSelectedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Dropdown_SetOptionSelectedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Dropdown_SetOptionSelectedColor(id, r, g, b, a);
    internal static void Dropdown_GetPopupBackgroundColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Dropdown_GetPopupBackgroundColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Dropdown_SetPopupBackgroundColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Dropdown_SetPopupBackgroundColor(id, r, g, b, a);
    internal static void Dropdown_GetOptionTextColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Dropdown_GetOptionTextColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Dropdown_SetOptionTextColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Dropdown_SetOptionTextColor(id, r, g, b, a);

    // ── UI: Scrollbar ───────────────────────────────────────────────
    internal static float Scrollbar_GetValue(ulong id) => NativeCallbacks.Bindings.Scrollbar_GetValue(id);
    internal static void Scrollbar_SetValue(ulong id, float v) => NativeCallbacks.Bindings.Scrollbar_SetValue(id, v);
    internal static float Scrollbar_GetSize(ulong id) => NativeCallbacks.Bindings.Scrollbar_GetSize(id);
    internal static void Scrollbar_SetSize(ulong id, float v) => NativeCallbacks.Bindings.Scrollbar_SetSize(id, v);
    internal static int Scrollbar_GetNumberOfSteps(ulong id) => NativeCallbacks.Bindings.Scrollbar_GetNumberOfSteps(id);
    internal static void Scrollbar_SetNumberOfSteps(ulong id, int v) => NativeCallbacks.Bindings.Scrollbar_SetNumberOfSteps(id, v);
    internal static int Scrollbar_GetDirection(ulong id) => NativeCallbacks.Bindings.Scrollbar_GetDirection(id);
    internal static void Scrollbar_SetDirection(ulong id, int v) => NativeCallbacks.Bindings.Scrollbar_SetDirection(id, v);
    internal static bool Scrollbar_GetIsReadOnly(ulong id) => NativeCallbacks.Bindings.Scrollbar_GetIsReadOnly(id) != 0;
    internal static void Scrollbar_SetIsReadOnly(ulong id, bool v) => NativeCallbacks.Bindings.Scrollbar_SetIsReadOnly(id, v ? 1 : 0);
    internal static ulong Scrollbar_GetHandleEntity(ulong id) => NativeCallbacks.Bindings.Scrollbar_GetHandleEntity(id);
    internal static void Scrollbar_SetHandleEntity(ulong id, ulong refUuid) => NativeCallbacks.Bindings.Scrollbar_SetHandleEntity(id, refUuid);
    internal static bool Scrollbar_GetValueChangedThisFrame(ulong id) => NativeCallbacks.Bindings.Scrollbar_GetValueChangedThisFrame(id) != 0;
    internal static void Scrollbar_MarkValueObserved(ulong id) => NativeCallbacks.Bindings.Scrollbar_MarkValueObserved(id);
    internal static void Scrollbar_GetNormalColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Scrollbar_GetNormalColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Scrollbar_SetNormalColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Scrollbar_SetNormalColor(id, r, g, b, a);
    internal static void Scrollbar_GetHoveredColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Scrollbar_GetHoveredColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Scrollbar_SetHoveredColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Scrollbar_SetHoveredColor(id, r, g, b, a);
    internal static void Scrollbar_GetPressedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Scrollbar_GetPressedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Scrollbar_SetPressedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Scrollbar_SetPressedColor(id, r, g, b, a);
    internal static void Scrollbar_GetDisabledColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Scrollbar_GetDisabledColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Scrollbar_SetDisabledColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Scrollbar_SetDisabledColor(id, r, g, b, a);
    internal static void Scrollbar_GetFocusedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.Scrollbar_GetFocusedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void Scrollbar_SetFocusedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.Scrollbar_SetFocusedColor(id, r, g, b, a);
    internal static int Scrollbar_GetTransitionMode(ulong id) => NativeCallbacks.Bindings.Scrollbar_GetTransitionMode(id);
    internal static void Scrollbar_SetTransitionMode(ulong id, int mode) => NativeCallbacks.Bindings.Scrollbar_SetTransitionMode(id, mode);
    internal static ulong Scrollbar_GetNormalSprite(ulong id) => NativeCallbacks.Bindings.Scrollbar_GetNormalSprite(id);
    internal static void Scrollbar_SetNormalSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Scrollbar_SetNormalSprite(id, uuid);
    internal static ulong Scrollbar_GetHoveredSprite(ulong id) => NativeCallbacks.Bindings.Scrollbar_GetHoveredSprite(id);
    internal static void Scrollbar_SetHoveredSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Scrollbar_SetHoveredSprite(id, uuid);
    internal static ulong Scrollbar_GetPressedSprite(ulong id) => NativeCallbacks.Bindings.Scrollbar_GetPressedSprite(id);
    internal static void Scrollbar_SetPressedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Scrollbar_SetPressedSprite(id, uuid);
    internal static ulong Scrollbar_GetDisabledSprite(ulong id) => NativeCallbacks.Bindings.Scrollbar_GetDisabledSprite(id);
    internal static void Scrollbar_SetDisabledSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Scrollbar_SetDisabledSprite(id, uuid);
    internal static ulong Scrollbar_GetFocusedSprite(ulong id) => NativeCallbacks.Bindings.Scrollbar_GetFocusedSprite(id);
    internal static void Scrollbar_SetFocusedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.Scrollbar_SetFocusedSprite(id, uuid);

    // ── UI: ScrollRect ──────────────────────────────────────────────
    internal static ulong ScrollRect_GetContent(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetContent(id);
    internal static void ScrollRect_SetContent(ulong id, ulong refUuid) => NativeCallbacks.Bindings.ScrollRect_SetContent(id, refUuid);
    internal static ulong ScrollRect_GetViewport(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetViewport(id);
    internal static void ScrollRect_SetViewport(ulong id, ulong refUuid) => NativeCallbacks.Bindings.ScrollRect_SetViewport(id, refUuid);
    internal static bool ScrollRect_GetHorizontal(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetHorizontal(id) != 0;
    internal static void ScrollRect_SetHorizontal(ulong id, bool v) => NativeCallbacks.Bindings.ScrollRect_SetHorizontal(id, v ? 1 : 0);
    internal static bool ScrollRect_GetVertical(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetVertical(id) != 0;
    internal static void ScrollRect_SetVertical(ulong id, bool v) => NativeCallbacks.Bindings.ScrollRect_SetVertical(id, v ? 1 : 0);
    internal static int ScrollRect_GetMovementType(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetMovementType(id);
    internal static void ScrollRect_SetMovementType(ulong id, int v) => NativeCallbacks.Bindings.ScrollRect_SetMovementType(id, v);
    internal static float ScrollRect_GetElasticity(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetElasticity(id);
    internal static void ScrollRect_SetElasticity(ulong id, float v) => NativeCallbacks.Bindings.ScrollRect_SetElasticity(id, v);
    internal static bool ScrollRect_GetInertia(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetInertia(id) != 0;
    internal static void ScrollRect_SetInertia(ulong id, bool v) => NativeCallbacks.Bindings.ScrollRect_SetInertia(id, v ? 1 : 0);
    internal static float ScrollRect_GetDecelerationRate(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetDecelerationRate(id);
    internal static void ScrollRect_SetDecelerationRate(ulong id, float v) => NativeCallbacks.Bindings.ScrollRect_SetDecelerationRate(id, v);
    internal static float ScrollRect_GetScrollSensitivity(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetScrollSensitivity(id);
    internal static void ScrollRect_SetScrollSensitivity(ulong id, float v) => NativeCallbacks.Bindings.ScrollRect_SetScrollSensitivity(id, v);
    internal static ulong ScrollRect_GetHorizontalScrollbar(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetHorizontalScrollbar(id);
    internal static void ScrollRect_SetHorizontalScrollbar(ulong id, ulong refUuid) => NativeCallbacks.Bindings.ScrollRect_SetHorizontalScrollbar(id, refUuid);
    internal static ulong ScrollRect_GetVerticalScrollbar(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetVerticalScrollbar(id);
    internal static void ScrollRect_SetVerticalScrollbar(ulong id, ulong refUuid) => NativeCallbacks.Bindings.ScrollRect_SetVerticalScrollbar(id, refUuid);
    internal static int ScrollRect_GetHorizontalScrollbarVisibility(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetHorizontalScrollbarVisibility(id);
    internal static void ScrollRect_SetHorizontalScrollbarVisibility(ulong id, int v) => NativeCallbacks.Bindings.ScrollRect_SetHorizontalScrollbarVisibility(id, v);
    internal static int ScrollRect_GetVerticalScrollbarVisibility(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetVerticalScrollbarVisibility(id);
    internal static void ScrollRect_SetVerticalScrollbarVisibility(ulong id, int v) => NativeCallbacks.Bindings.ScrollRect_SetVerticalScrollbarVisibility(id, v);
    internal static float ScrollRect_GetHorizontalScrollbarSpacing(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetHorizontalScrollbarSpacing(id);
    internal static void ScrollRect_SetHorizontalScrollbarSpacing(ulong id, float v) => NativeCallbacks.Bindings.ScrollRect_SetHorizontalScrollbarSpacing(id, v);
    internal static float ScrollRect_GetVerticalScrollbarSpacing(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetVerticalScrollbarSpacing(id);
    internal static void ScrollRect_SetVerticalScrollbarSpacing(ulong id, float v) => NativeCallbacks.Bindings.ScrollRect_SetVerticalScrollbarSpacing(id, v);
    internal static void ScrollRect_GetNormalizedPosition(ulong id, out float x, out float y)
    { float cx, cy; NativeCallbacks.Bindings.ScrollRect_GetNormalizedPosition(id, &cx, &cy); x = cx; y = cy; }
    internal static void ScrollRect_SetNormalizedPosition(ulong id, float x, float y)
        => NativeCallbacks.Bindings.ScrollRect_SetNormalizedPosition(id, x, y);
    internal static bool ScrollRect_GetValueChangedThisFrame(ulong id) => NativeCallbacks.Bindings.ScrollRect_GetValueChangedThisFrame(id) != 0;
    internal static void ScrollRect_MarkValueObserved(ulong id) => NativeCallbacks.Bindings.ScrollRect_MarkValueObserved(id);

    // ── UI: Mask ────────────────────────────────────────────────────
    internal static bool Mask_GetShowMaskGraphic(ulong id) => NativeCallbacks.Bindings.Mask_GetShowMaskGraphic(id) != 0;
    internal static void Mask_SetShowMaskGraphic(ulong id, bool v) => NativeCallbacks.Bindings.Mask_SetShowMaskGraphic(id, v ? 1 : 0);

    // ── UI: CircularSlider ──────────────────────────────────────────
    internal static float CircularSlider_GetValue(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetValue(id);
    internal static void CircularSlider_SetValue(ulong id, float v) => NativeCallbacks.Bindings.CircularSlider_SetValue(id, v);
    internal static float CircularSlider_GetMinValue(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetMinValue(id);
    internal static void CircularSlider_SetMinValue(ulong id, float v) => NativeCallbacks.Bindings.CircularSlider_SetMinValue(id, v);
    internal static float CircularSlider_GetMaxValue(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetMaxValue(id);
    internal static void CircularSlider_SetMaxValue(ulong id, float v) => NativeCallbacks.Bindings.CircularSlider_SetMaxValue(id, v);
    internal static bool CircularSlider_GetWholeNumbers(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetWholeNumbers(id) != 0;
    internal static void CircularSlider_SetWholeNumbers(ulong id, bool v) => NativeCallbacks.Bindings.CircularSlider_SetWholeNumbers(id, v ? 1 : 0);
    internal static bool CircularSlider_GetIsReadOnly(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetIsReadOnly(id) != 0;
    internal static void CircularSlider_SetIsReadOnly(ulong id, bool v) => NativeCallbacks.Bindings.CircularSlider_SetIsReadOnly(id, v ? 1 : 0);
    internal static float CircularSlider_GetStartAngleDegrees(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetStartAngleDegrees(id);
    internal static void CircularSlider_SetStartAngleDegrees(ulong id, float v) => NativeCallbacks.Bindings.CircularSlider_SetStartAngleDegrees(id, v);
    internal static float CircularSlider_GetSweepDegrees(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetSweepDegrees(id);
    internal static void CircularSlider_SetSweepDegrees(ulong id, float v) => NativeCallbacks.Bindings.CircularSlider_SetSweepDegrees(id, v);
    internal static bool CircularSlider_GetClockwise(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetClockwise(id) != 0;
    internal static void CircularSlider_SetClockwise(ulong id, bool v) => NativeCallbacks.Bindings.CircularSlider_SetClockwise(id, v ? 1 : 0);
    internal static float CircularSlider_GetRingThickness(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetRingThickness(id);
    internal static void CircularSlider_SetRingThickness(ulong id, float v) => NativeCallbacks.Bindings.CircularSlider_SetRingThickness(id, v);
    internal static int CircularSlider_GetRingSegments(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetRingSegments(id);
    internal static void CircularSlider_SetRingSegments(ulong id, int v) => NativeCallbacks.Bindings.CircularSlider_SetRingSegments(id, v);
    internal static void CircularSlider_GetBackgroundColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.CircularSlider_GetBackgroundColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void CircularSlider_SetBackgroundColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.CircularSlider_SetBackgroundColor(id, r, g, b, a);
    internal static void CircularSlider_GetFillColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.CircularSlider_GetFillColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void CircularSlider_SetFillColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.CircularSlider_SetFillColor(id, r, g, b, a);
    internal static ulong CircularSlider_GetHandleEntity(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetHandleEntity(id);
    internal static void CircularSlider_SetHandleEntity(ulong id, ulong refUuid) => NativeCallbacks.Bindings.CircularSlider_SetHandleEntity(id, refUuid);
    internal static bool CircularSlider_GetValueChangedThisFrame(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetValueChangedThisFrame(id) != 0;
    internal static void CircularSlider_MarkValueObserved(ulong id) => NativeCallbacks.Bindings.CircularSlider_MarkValueObserved(id);
    internal static void CircularSlider_GetNormalColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.CircularSlider_GetNormalColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void CircularSlider_SetNormalColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.CircularSlider_SetNormalColor(id, r, g, b, a);
    internal static void CircularSlider_GetHoveredColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.CircularSlider_GetHoveredColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void CircularSlider_SetHoveredColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.CircularSlider_SetHoveredColor(id, r, g, b, a);
    internal static void CircularSlider_GetPressedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.CircularSlider_GetPressedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void CircularSlider_SetPressedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.CircularSlider_SetPressedColor(id, r, g, b, a);
    internal static void CircularSlider_GetDisabledColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.CircularSlider_GetDisabledColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void CircularSlider_SetDisabledColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.CircularSlider_SetDisabledColor(id, r, g, b, a);
    internal static void CircularSlider_GetFocusedColor(ulong id, out float r, out float g, out float b, out float a)
    { float cr, cg, cb, ca; NativeCallbacks.Bindings.CircularSlider_GetFocusedColor(id, &cr, &cg, &cb, &ca); r = cr; g = cg; b = cb; a = ca; }
    internal static void CircularSlider_SetFocusedColor(ulong id, float r, float g, float b, float a)
        => NativeCallbacks.Bindings.CircularSlider_SetFocusedColor(id, r, g, b, a);
    internal static int CircularSlider_GetTransitionMode(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetTransitionMode(id);
    internal static void CircularSlider_SetTransitionMode(ulong id, int mode) => NativeCallbacks.Bindings.CircularSlider_SetTransitionMode(id, mode);
    internal static ulong CircularSlider_GetNormalSprite(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetNormalSprite(id);
    internal static void CircularSlider_SetNormalSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.CircularSlider_SetNormalSprite(id, uuid);
    internal static ulong CircularSlider_GetHoveredSprite(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetHoveredSprite(id);
    internal static void CircularSlider_SetHoveredSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.CircularSlider_SetHoveredSprite(id, uuid);
    internal static ulong CircularSlider_GetPressedSprite(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetPressedSprite(id);
    internal static void CircularSlider_SetPressedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.CircularSlider_SetPressedSprite(id, uuid);
    internal static ulong CircularSlider_GetDisabledSprite(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetDisabledSprite(id);
    internal static void CircularSlider_SetDisabledSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.CircularSlider_SetDisabledSprite(id, uuid);
    internal static ulong CircularSlider_GetFocusedSprite(ulong id) => NativeCallbacks.Bindings.CircularSlider_GetFocusedSprite(id);
    internal static void CircularSlider_SetFocusedSprite(ulong id, ulong uuid) => NativeCallbacks.Bindings.CircularSlider_SetFocusedSprite(id, uuid);

    // ── UI: HorizontalLayoutGroup ───────────────────────────────────
    internal static float HorizontalLayoutGroup_GetPaddingLeft(ulong id) => NativeCallbacks.Bindings.HorizontalLayoutGroup_GetPaddingLeft(id);
    internal static void HorizontalLayoutGroup_SetPaddingLeft(ulong id, float v) => NativeCallbacks.Bindings.HorizontalLayoutGroup_SetPaddingLeft(id, v);
    internal static float HorizontalLayoutGroup_GetPaddingRight(ulong id) => NativeCallbacks.Bindings.HorizontalLayoutGroup_GetPaddingRight(id);
    internal static void HorizontalLayoutGroup_SetPaddingRight(ulong id, float v) => NativeCallbacks.Bindings.HorizontalLayoutGroup_SetPaddingRight(id, v);
    internal static float HorizontalLayoutGroup_GetPaddingTop(ulong id) => NativeCallbacks.Bindings.HorizontalLayoutGroup_GetPaddingTop(id);
    internal static void HorizontalLayoutGroup_SetPaddingTop(ulong id, float v) => NativeCallbacks.Bindings.HorizontalLayoutGroup_SetPaddingTop(id, v);
    internal static float HorizontalLayoutGroup_GetPaddingBottom(ulong id) => NativeCallbacks.Bindings.HorizontalLayoutGroup_GetPaddingBottom(id);
    internal static void HorizontalLayoutGroup_SetPaddingBottom(ulong id, float v) => NativeCallbacks.Bindings.HorizontalLayoutGroup_SetPaddingBottom(id, v);
    internal static float HorizontalLayoutGroup_GetSpacing(ulong id) => NativeCallbacks.Bindings.HorizontalLayoutGroup_GetSpacing(id);
    internal static void HorizontalLayoutGroup_SetSpacing(ulong id, float v) => NativeCallbacks.Bindings.HorizontalLayoutGroup_SetSpacing(id, v);
    internal static int HorizontalLayoutGroup_GetChildAlignment(ulong id) => NativeCallbacks.Bindings.HorizontalLayoutGroup_GetChildAlignment(id);
    internal static void HorizontalLayoutGroup_SetChildAlignment(ulong id, int v) => NativeCallbacks.Bindings.HorizontalLayoutGroup_SetChildAlignment(id, v);
    internal static bool HorizontalLayoutGroup_GetReverseArrangement(ulong id) => NativeCallbacks.Bindings.HorizontalLayoutGroup_GetReverseArrangement(id) != 0;
    internal static void HorizontalLayoutGroup_SetReverseArrangement(ulong id, bool v) => NativeCallbacks.Bindings.HorizontalLayoutGroup_SetReverseArrangement(id, v ? 1 : 0);
    internal static bool HorizontalLayoutGroup_GetControlChildWidth(ulong id) => NativeCallbacks.Bindings.HorizontalLayoutGroup_GetControlChildWidth(id) != 0;
    internal static void HorizontalLayoutGroup_SetControlChildWidth(ulong id, bool v) => NativeCallbacks.Bindings.HorizontalLayoutGroup_SetControlChildWidth(id, v ? 1 : 0);
    internal static bool HorizontalLayoutGroup_GetControlChildHeight(ulong id) => NativeCallbacks.Bindings.HorizontalLayoutGroup_GetControlChildHeight(id) != 0;
    internal static void HorizontalLayoutGroup_SetControlChildHeight(ulong id, bool v) => NativeCallbacks.Bindings.HorizontalLayoutGroup_SetControlChildHeight(id, v ? 1 : 0);
    internal static bool HorizontalLayoutGroup_GetUseChildScaleWidth(ulong id) => NativeCallbacks.Bindings.HorizontalLayoutGroup_GetUseChildScaleWidth(id) != 0;
    internal static void HorizontalLayoutGroup_SetUseChildScaleWidth(ulong id, bool v) => NativeCallbacks.Bindings.HorizontalLayoutGroup_SetUseChildScaleWidth(id, v ? 1 : 0);
    internal static bool HorizontalLayoutGroup_GetUseChildScaleHeight(ulong id) => NativeCallbacks.Bindings.HorizontalLayoutGroup_GetUseChildScaleHeight(id) != 0;
    internal static void HorizontalLayoutGroup_SetUseChildScaleHeight(ulong id, bool v) => NativeCallbacks.Bindings.HorizontalLayoutGroup_SetUseChildScaleHeight(id, v ? 1 : 0);
    internal static bool HorizontalLayoutGroup_GetChildForceExpandWidth(ulong id) => NativeCallbacks.Bindings.HorizontalLayoutGroup_GetChildForceExpandWidth(id) != 0;
    internal static void HorizontalLayoutGroup_SetChildForceExpandWidth(ulong id, bool v) => NativeCallbacks.Bindings.HorizontalLayoutGroup_SetChildForceExpandWidth(id, v ? 1 : 0);
    internal static bool HorizontalLayoutGroup_GetChildForceExpandHeight(ulong id) => NativeCallbacks.Bindings.HorizontalLayoutGroup_GetChildForceExpandHeight(id) != 0;
    internal static void HorizontalLayoutGroup_SetChildForceExpandHeight(ulong id, bool v) => NativeCallbacks.Bindings.HorizontalLayoutGroup_SetChildForceExpandHeight(id, v ? 1 : 0);

    // ── UI: VerticalLayoutGroup ─────────────────────────────────────
    internal static float VerticalLayoutGroup_GetPaddingLeft(ulong id) => NativeCallbacks.Bindings.VerticalLayoutGroup_GetPaddingLeft(id);
    internal static void VerticalLayoutGroup_SetPaddingLeft(ulong id, float v) => NativeCallbacks.Bindings.VerticalLayoutGroup_SetPaddingLeft(id, v);
    internal static float VerticalLayoutGroup_GetPaddingRight(ulong id) => NativeCallbacks.Bindings.VerticalLayoutGroup_GetPaddingRight(id);
    internal static void VerticalLayoutGroup_SetPaddingRight(ulong id, float v) => NativeCallbacks.Bindings.VerticalLayoutGroup_SetPaddingRight(id, v);
    internal static float VerticalLayoutGroup_GetPaddingTop(ulong id) => NativeCallbacks.Bindings.VerticalLayoutGroup_GetPaddingTop(id);
    internal static void VerticalLayoutGroup_SetPaddingTop(ulong id, float v) => NativeCallbacks.Bindings.VerticalLayoutGroup_SetPaddingTop(id, v);
    internal static float VerticalLayoutGroup_GetPaddingBottom(ulong id) => NativeCallbacks.Bindings.VerticalLayoutGroup_GetPaddingBottom(id);
    internal static void VerticalLayoutGroup_SetPaddingBottom(ulong id, float v) => NativeCallbacks.Bindings.VerticalLayoutGroup_SetPaddingBottom(id, v);
    internal static float VerticalLayoutGroup_GetSpacing(ulong id) => NativeCallbacks.Bindings.VerticalLayoutGroup_GetSpacing(id);
    internal static void VerticalLayoutGroup_SetSpacing(ulong id, float v) => NativeCallbacks.Bindings.VerticalLayoutGroup_SetSpacing(id, v);
    internal static int VerticalLayoutGroup_GetChildAlignment(ulong id) => NativeCallbacks.Bindings.VerticalLayoutGroup_GetChildAlignment(id);
    internal static void VerticalLayoutGroup_SetChildAlignment(ulong id, int v) => NativeCallbacks.Bindings.VerticalLayoutGroup_SetChildAlignment(id, v);
    internal static bool VerticalLayoutGroup_GetReverseArrangement(ulong id) => NativeCallbacks.Bindings.VerticalLayoutGroup_GetReverseArrangement(id) != 0;
    internal static void VerticalLayoutGroup_SetReverseArrangement(ulong id, bool v) => NativeCallbacks.Bindings.VerticalLayoutGroup_SetReverseArrangement(id, v ? 1 : 0);
    internal static bool VerticalLayoutGroup_GetControlChildWidth(ulong id) => NativeCallbacks.Bindings.VerticalLayoutGroup_GetControlChildWidth(id) != 0;
    internal static void VerticalLayoutGroup_SetControlChildWidth(ulong id, bool v) => NativeCallbacks.Bindings.VerticalLayoutGroup_SetControlChildWidth(id, v ? 1 : 0);
    internal static bool VerticalLayoutGroup_GetControlChildHeight(ulong id) => NativeCallbacks.Bindings.VerticalLayoutGroup_GetControlChildHeight(id) != 0;
    internal static void VerticalLayoutGroup_SetControlChildHeight(ulong id, bool v) => NativeCallbacks.Bindings.VerticalLayoutGroup_SetControlChildHeight(id, v ? 1 : 0);
    internal static bool VerticalLayoutGroup_GetUseChildScaleWidth(ulong id) => NativeCallbacks.Bindings.VerticalLayoutGroup_GetUseChildScaleWidth(id) != 0;
    internal static void VerticalLayoutGroup_SetUseChildScaleWidth(ulong id, bool v) => NativeCallbacks.Bindings.VerticalLayoutGroup_SetUseChildScaleWidth(id, v ? 1 : 0);
    internal static bool VerticalLayoutGroup_GetUseChildScaleHeight(ulong id) => NativeCallbacks.Bindings.VerticalLayoutGroup_GetUseChildScaleHeight(id) != 0;
    internal static void VerticalLayoutGroup_SetUseChildScaleHeight(ulong id, bool v) => NativeCallbacks.Bindings.VerticalLayoutGroup_SetUseChildScaleHeight(id, v ? 1 : 0);
    internal static bool VerticalLayoutGroup_GetChildForceExpandWidth(ulong id) => NativeCallbacks.Bindings.VerticalLayoutGroup_GetChildForceExpandWidth(id) != 0;
    internal static void VerticalLayoutGroup_SetChildForceExpandWidth(ulong id, bool v) => NativeCallbacks.Bindings.VerticalLayoutGroup_SetChildForceExpandWidth(id, v ? 1 : 0);
    internal static bool VerticalLayoutGroup_GetChildForceExpandHeight(ulong id) => NativeCallbacks.Bindings.VerticalLayoutGroup_GetChildForceExpandHeight(id) != 0;
    internal static void VerticalLayoutGroup_SetChildForceExpandHeight(ulong id, bool v) => NativeCallbacks.Bindings.VerticalLayoutGroup_SetChildForceExpandHeight(id, v ? 1 : 0);

    // ── UI: GridLayoutGroup ─────────────────────────────────────────
    internal static float GridLayoutGroup_GetPaddingLeft(ulong id) => NativeCallbacks.Bindings.GridLayoutGroup_GetPaddingLeft(id);
    internal static void GridLayoutGroup_SetPaddingLeft(ulong id, float v) => NativeCallbacks.Bindings.GridLayoutGroup_SetPaddingLeft(id, v);
    internal static float GridLayoutGroup_GetPaddingRight(ulong id) => NativeCallbacks.Bindings.GridLayoutGroup_GetPaddingRight(id);
    internal static void GridLayoutGroup_SetPaddingRight(ulong id, float v) => NativeCallbacks.Bindings.GridLayoutGroup_SetPaddingRight(id, v);
    internal static float GridLayoutGroup_GetPaddingTop(ulong id) => NativeCallbacks.Bindings.GridLayoutGroup_GetPaddingTop(id);
    internal static void GridLayoutGroup_SetPaddingTop(ulong id, float v) => NativeCallbacks.Bindings.GridLayoutGroup_SetPaddingTop(id, v);
    internal static float GridLayoutGroup_GetPaddingBottom(ulong id) => NativeCallbacks.Bindings.GridLayoutGroup_GetPaddingBottom(id);
    internal static void GridLayoutGroup_SetPaddingBottom(ulong id, float v) => NativeCallbacks.Bindings.GridLayoutGroup_SetPaddingBottom(id, v);
    internal static void GridLayoutGroup_GetCellSize(ulong id, out float x, out float y)
    { float cx, cy; NativeCallbacks.Bindings.GridLayoutGroup_GetCellSize(id, &cx, &cy); x = cx; y = cy; }
    internal static void GridLayoutGroup_SetCellSize(ulong id, float x, float y)
        => NativeCallbacks.Bindings.GridLayoutGroup_SetCellSize(id, x, y);
    internal static void GridLayoutGroup_GetSpacing(ulong id, out float x, out float y)
    { float cx, cy; NativeCallbacks.Bindings.GridLayoutGroup_GetSpacing(id, &cx, &cy); x = cx; y = cy; }
    internal static void GridLayoutGroup_SetSpacing(ulong id, float x, float y)
        => NativeCallbacks.Bindings.GridLayoutGroup_SetSpacing(id, x, y);
    internal static int GridLayoutGroup_GetStartCorner(ulong id) => NativeCallbacks.Bindings.GridLayoutGroup_GetStartCorner(id);
    internal static void GridLayoutGroup_SetStartCorner(ulong id, int v) => NativeCallbacks.Bindings.GridLayoutGroup_SetStartCorner(id, v);
    internal static int GridLayoutGroup_GetStartAxis(ulong id) => NativeCallbacks.Bindings.GridLayoutGroup_GetStartAxis(id);
    internal static void GridLayoutGroup_SetStartAxis(ulong id, int v) => NativeCallbacks.Bindings.GridLayoutGroup_SetStartAxis(id, v);
    internal static int GridLayoutGroup_GetChildAlignment(ulong id) => NativeCallbacks.Bindings.GridLayoutGroup_GetChildAlignment(id);
    internal static void GridLayoutGroup_SetChildAlignment(ulong id, int v) => NativeCallbacks.Bindings.GridLayoutGroup_SetChildAlignment(id, v);
    internal static int GridLayoutGroup_GetConstraint(ulong id) => NativeCallbacks.Bindings.GridLayoutGroup_GetConstraint(id);
    internal static void GridLayoutGroup_SetConstraint(ulong id, int v) => NativeCallbacks.Bindings.GridLayoutGroup_SetConstraint(id, v);
    internal static int GridLayoutGroup_GetConstraintCount(ulong id) => NativeCallbacks.Bindings.GridLayoutGroup_GetConstraintCount(id);
    internal static void GridLayoutGroup_SetConstraintCount(ulong id, int v) => NativeCallbacks.Bindings.GridLayoutGroup_SetConstraintCount(id, v);
    internal static bool GridLayoutGroup_GetReverse(ulong id) => NativeCallbacks.Bindings.GridLayoutGroup_GetReverse(id) != 0;
    internal static void GridLayoutGroup_SetReverse(ulong id, bool v) => NativeCallbacks.Bindings.GridLayoutGroup_SetReverse(id, v ? 1 : 0);

    // ── UI: ContentSizeFitter ───────────────────────────────────────
    internal static bool ContentSizeFitter_GetHorizontalFit(ulong id) => NativeCallbacks.Bindings.ContentSizeFitter_GetHorizontalFit(id) != 0;
    internal static void ContentSizeFitter_SetHorizontalFit(ulong id, bool v) => NativeCallbacks.Bindings.ContentSizeFitter_SetHorizontalFit(id, v ? 1 : 0);
    internal static bool ContentSizeFitter_GetVerticalFit(ulong id) => NativeCallbacks.Bindings.ContentSizeFitter_GetVerticalFit(id) != 0;
    internal static void ContentSizeFitter_SetVerticalFit(ulong id, bool v) => NativeCallbacks.Bindings.ContentSizeFitter_SetVerticalFit(id, v ? 1 : 0);
    internal static float ContentSizeFitter_GetPaddingLeft(ulong id) => NativeCallbacks.Bindings.ContentSizeFitter_GetPaddingLeft(id);
    internal static void ContentSizeFitter_SetPaddingLeft(ulong id, float v) => NativeCallbacks.Bindings.ContentSizeFitter_SetPaddingLeft(id, v);
    internal static float ContentSizeFitter_GetPaddingRight(ulong id) => NativeCallbacks.Bindings.ContentSizeFitter_GetPaddingRight(id);
    internal static void ContentSizeFitter_SetPaddingRight(ulong id, float v) => NativeCallbacks.Bindings.ContentSizeFitter_SetPaddingRight(id, v);
    internal static float ContentSizeFitter_GetPaddingTop(ulong id) => NativeCallbacks.Bindings.ContentSizeFitter_GetPaddingTop(id);
    internal static void ContentSizeFitter_SetPaddingTop(ulong id, float v) => NativeCallbacks.Bindings.ContentSizeFitter_SetPaddingTop(id, v);
    internal static float ContentSizeFitter_GetPaddingBottom(ulong id) => NativeCallbacks.Bindings.ContentSizeFitter_GetPaddingBottom(id);
    internal static void ContentSizeFitter_SetPaddingBottom(ulong id, float v) => NativeCallbacks.Bindings.ContentSizeFitter_SetPaddingBottom(id, v);

    // ── UI: WidthConstraint ─────────────────────────────────────────
    internal static float WidthConstraint_GetMinWidth(ulong id) => NativeCallbacks.Bindings.WidthConstraint_GetMinWidth(id);
    internal static void WidthConstraint_SetMinWidth(ulong id, float v) => NativeCallbacks.Bindings.WidthConstraint_SetMinWidth(id, v);
    internal static float WidthConstraint_GetMaxWidth(ulong id) => NativeCallbacks.Bindings.WidthConstraint_GetMaxWidth(id);
    internal static void WidthConstraint_SetMaxWidth(ulong id, float v) => NativeCallbacks.Bindings.WidthConstraint_SetMaxWidth(id, v);

    // ── Localization ────────────────────────────────────────────────
    internal static string Localization_Get(string key)
    {
        byte[] keyBytes = EncodeUtf8Z(key);
        fixed (byte* k = keyBytes)
        {
            int required = NativeCallbacks.Bindings.Localization_GetBuffer(k, null, 0);
            if (required <= 0) return "";
            byte[] buffer = new byte[required + 1];
            fixed (byte* p = buffer)
            {
                int written = NativeCallbacks.Bindings.Localization_GetBuffer(k, p, buffer.Length);
                return DecodeNativeString(buffer, written);
            }
        }
    }
    internal static bool Localization_HasKey(string key)
    {
        byte[] buf = EncodeUtf8Z(key);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Localization_HasKey(ptr) != 0;
    }
    // which: 0 = current, 1 = fallback, 2 = OS UI language.
    internal static string Localization_GetLanguage(int which)
        => ReadNativeString(NativeCallbacks.Bindings.Localization_GetLanguageBuffer, which);
    internal static bool Localization_SetLanguage(string code)
    {
        byte[] buf = EncodeUtf8Z(code);
        fixed (byte* ptr = buf) return NativeCallbacks.Bindings.Localization_SetLanguage(ptr) != 0;
    }
    internal static string Localization_GetLanguagesJson()
        => ReadNativeString(NativeCallbacks.Bindings.Localization_GetLanguagesBuffer);
    internal static uint Localization_GetRevision() => NativeCallbacks.Bindings.Localization_GetRevision();

    // ── UI: LocalizedText ───────────────────────────────────────────
    internal static string LocalizedText_GetKey(ulong id)
        => ReadNativeString(NativeCallbacks.Bindings.LocalizedText_GetKeyBuffer, id);
    internal static void LocalizedText_SetKey(ulong id, string key)
    {
        byte[] buf = EncodeUtf8Z(key);
        fixed (byte* ptr = buf) NativeCallbacks.Bindings.LocalizedText_SetKey(id, ptr);
    }

    // ── Dynamic component registration ───────────────────────────────
    // Called from DynamicComponentRegistrar after the user assembly loads: every IComponent struct registers here at
    // runtime, no engine rebuild required.
    internal static uint Component_RegisterDynamic(
        string displayName,
        string serializedName,
        string subcategory,
        uint category,
        uint size,
        uint alignment)
    {
        byte[] dispBuf  = EncodeUtf8Z(displayName);
        byte[] serBuf   = EncodeUtf8Z(serializedName);
        byte[] subBuf   = EncodeUtf8Z(subcategory);
        fixed (byte* d = dispBuf)
        fixed (byte* s = serBuf)
        fixed (byte* c = subBuf)
        {
            return NativeCallbacks.Bindings.Component_RegisterDynamic(d, s, c, category, size, alignment);
        }
    }

    internal static void Component_UnregisterAllDynamic()
    {
        NativeCallbacks.Bindings.Component_UnregisterAllDynamic();
    }

    // ── Scene load-by-GUID ───────────────────────────────────────────
    // Wrap the native Scene_*ByGuid entry points (no string buffer
    // marshalling — GUIDs are blittable ulongs, so these are the leanest
    // path between managed code and the native scene loader).
    internal static bool Scene_LoadByGuid(ulong sceneGuid)
        => NativeCallbacks.Bindings.Scene_LoadByGuid(sceneGuid) != 0;

    internal static bool Scene_LoadAdditiveByGuid(ulong sceneGuid)
        => NativeCallbacks.Bindings.Scene_LoadAdditiveByGuid(sceneGuid) != 0;

    internal static void Scene_UnloadByGuid(ulong sceneGuid)
        => NativeCallbacks.Bindings.Scene_UnloadByGuid(sceneGuid);

    internal static bool Scene_SetActiveByGuid(ulong sceneGuid)
        => NativeCallbacks.Bindings.Scene_SetActiveByGuid(sceneGuid) != 0;

    internal static bool Scene_ReloadByGuid(ulong sceneGuid)
        => NativeCallbacks.Bindings.Scene_ReloadByGuid(sceneGuid) != 0;

    internal static bool Scene_DoesSceneExistByGuid(ulong sceneGuid)
        => NativeCallbacks.Bindings.Scene_DoesSceneExistByGuid(sceneGuid) != 0;

    internal static ulong Scene_GetActiveSceneGuid()
        => NativeCallbacks.Bindings.Scene_GetActiveSceneGuid();

    // ── 3D components ───────────────────────────────────────────────

    private static Vector3 ReadVector3(ulong id, delegate* unmanaged<ulong, float*, float*, float*, void> getter)
    {
        float x, y, z;
        getter(id, &x, &y, &z);
        return new Vector3(x, y, z);
    }

    private static Color ReadColor(ulong id, delegate* unmanaged<ulong, float*, float*, float*, float*, void> getter)
    {
        float r, g, b, a;
        getter(id, &r, &g, &b, &a);
        return new Color(r, g, b, a);
    }

    internal static Vector3 Transform3D_GetPosition(ulong id) => ReadVector3(id, NativeCallbacks.Bindings.Transform3D_GetPosition);
    internal static void Transform3D_SetPosition(ulong id, Vector3 v) => NativeCallbacks.Bindings.Transform3D_SetPosition(id, v.X, v.Y, v.Z);
    internal static Vector3 Transform3D_GetRotation(ulong id) => ReadVector3(id, NativeCallbacks.Bindings.Transform3D_GetRotation);
    internal static void Transform3D_SetRotation(ulong id, Vector3 v) => NativeCallbacks.Bindings.Transform3D_SetRotation(id, v.X, v.Y, v.Z);
    internal static Vector3 Transform3D_GetScale(ulong id) => ReadVector3(id, NativeCallbacks.Bindings.Transform3D_GetScale);
    internal static void Transform3D_SetScale(ulong id, Vector3 v) => NativeCallbacks.Bindings.Transform3D_SetScale(id, v.X, v.Y, v.Z);
    internal static Vector3 Transform3D_GetWorldPosition(ulong id) => ReadVector3(id, NativeCallbacks.Bindings.Transform3D_GetWorldPosition);
    internal static Vector3 Transform3D_GetForward(ulong id) => ReadVector3(id, NativeCallbacks.Bindings.Transform3D_GetForward);

    internal static int Camera3D_GetProjection(ulong id) => NativeCallbacks.Bindings.Camera3D_GetProjection(id);
    internal static void Camera3D_SetProjection(ulong id, int projection) => NativeCallbacks.Bindings.Camera3D_SetProjection(id, projection);
    internal static float Camera3D_GetFieldOfView(ulong id) => NativeCallbacks.Bindings.Camera3D_GetFieldOfView(id);
    internal static void Camera3D_SetFieldOfView(ulong id, float degrees) => NativeCallbacks.Bindings.Camera3D_SetFieldOfView(id, degrees);
    internal static float Camera3D_GetOrthographicSize(ulong id) => NativeCallbacks.Bindings.Camera3D_GetOrthographicSize(id);
    internal static void Camera3D_SetOrthographicSize(ulong id, float size) => NativeCallbacks.Bindings.Camera3D_SetOrthographicSize(id, size);
    internal static float Camera3D_GetNearClip(ulong id) => NativeCallbacks.Bindings.Camera3D_GetNearClip(id);
    internal static void Camera3D_SetNearClip(ulong id, float value) => NativeCallbacks.Bindings.Camera3D_SetNearClip(id, value);
    internal static float Camera3D_GetFarClip(ulong id) => NativeCallbacks.Bindings.Camera3D_GetFarClip(id);
    internal static void Camera3D_SetFarClip(ulong id, float value) => NativeCallbacks.Bindings.Camera3D_SetFarClip(id, value);
    internal static Color Camera3D_GetClearColor(ulong id) => ReadColor(id, NativeCallbacks.Bindings.Camera3D_GetClearColor);
    internal static void Camera3D_SetClearColor(ulong id, Color c) => NativeCallbacks.Bindings.Camera3D_SetClearColor(id, c.R, c.G, c.B, c.A);
    internal static ulong Camera3D_GetMainEntity() => NativeCallbacks.Bindings.Camera3D_GetMainEntity();

    internal static int MeshRenderer_GetMesh(ulong id) => NativeCallbacks.Bindings.MeshRenderer_GetMesh(id);
    internal static void MeshRenderer_SetMesh(ulong id, int mesh) => NativeCallbacks.Bindings.MeshRenderer_SetMesh(id, mesh);
    internal static Color MeshRenderer_GetColor(ulong id) => ReadColor(id, NativeCallbacks.Bindings.MeshRenderer_GetColor);
    internal static void MeshRenderer_SetColor(ulong id, Color c) => NativeCallbacks.Bindings.MeshRenderer_SetColor(id, c.R, c.G, c.B, c.A);
    internal static bool MeshRenderer_GetLit(ulong id) => NativeCallbacks.Bindings.MeshRenderer_GetLit(id) != 0;
    internal static void MeshRenderer_SetLit(ulong id, bool lit) => NativeCallbacks.Bindings.MeshRenderer_SetLit(id, lit ? 1 : 0);

    internal static Color DirectionalLight_GetColor(ulong id) => ReadColor(id, NativeCallbacks.Bindings.DirectionalLight_GetColor);
    internal static void DirectionalLight_SetColor(ulong id, Color c) => NativeCallbacks.Bindings.DirectionalLight_SetColor(id, c.R, c.G, c.B, c.A);
    internal static float DirectionalLight_GetIntensity(ulong id) => NativeCallbacks.Bindings.DirectionalLight_GetIntensity(id);
    internal static void DirectionalLight_SetIntensity(ulong id, float intensity) => NativeCallbacks.Bindings.DirectionalLight_SetIntensity(id, intensity);
    internal static Color DirectionalLight_GetAmbient(ulong id) => ReadColor(id, NativeCallbacks.Bindings.DirectionalLight_GetAmbient);
    internal static void DirectionalLight_SetAmbient(ulong id, Color c) => NativeCallbacks.Bindings.DirectionalLight_SetAmbient(id, c.R, c.G, c.B, c.A);
    internal static bool DirectionalLight_GetCastShadows(ulong id) => NativeCallbacks.Bindings.DirectionalLight_GetCastShadows(id) != 0;
    internal static void DirectionalLight_SetCastShadows(ulong id, bool value) => NativeCallbacks.Bindings.DirectionalLight_SetCastShadows(id, value ? 1 : 0);

    internal static ulong MeshRenderer_GetMaterial(ulong id) => NativeCallbacks.Bindings.MeshRenderer_GetMaterial(id);
    internal static void MeshRenderer_SetMaterial(ulong id, ulong materialGuid) => NativeCallbacks.Bindings.MeshRenderer_SetMaterial(id, materialGuid);
    internal static bool MeshRenderer_GetCastShadows(ulong id) => NativeCallbacks.Bindings.MeshRenderer_GetCastShadows(id) != 0;
    internal static void MeshRenderer_SetCastShadows(ulong id, bool value) => NativeCallbacks.Bindings.MeshRenderer_SetCastShadows(id, value ? 1 : 0);
    internal static ulong MeshRenderer_GetCustomMesh(ulong id) => NativeCallbacks.Bindings.MeshRenderer_GetCustomMesh(id);
    internal static void MeshRenderer_SetCustomMesh(ulong id, ulong meshGuid) => NativeCallbacks.Bindings.MeshRenderer_SetCustomMesh(id, meshGuid);
    internal static ulong MeshCollider3D_GetCustomMesh(ulong id) => NativeCallbacks.Bindings.MeshCollider3D_GetCustomMesh(id);
    internal static void MeshCollider3D_SetCustomMesh(ulong id, ulong meshGuid) => NativeCallbacks.Bindings.MeshCollider3D_SetCustomMesh(id, meshGuid);

    // Packed layouts mirror ScriptBindings3D.cpp.
    internal const int MaterialValueCount = 16;
    internal const int MeshInfoValueCount = 8;
    internal const int PointLightValueCount = 6;
    internal const int SpotLightValueCount = 8;
    internal const int CameraSkyValueCount = 23;
    internal const int ParticleSystem3DValueCount = 21;
    internal const int SkinnedMeshRendererValueCount = 11;
    internal const int SkinnedMeshBoneValueCount = 9;

    internal static void SkinnedMeshRenderer_Get(ulong id, Span<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.SkinnedMeshRenderer_Get(id, p, values.Length);
    }
    internal static void SkinnedMeshRenderer_Set(ulong id, ReadOnlySpan<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.SkinnedMeshRenderer_Set(id, p, values.Length);
    }
    internal static ulong SkinnedMeshRenderer_GetAsset(ulong id, int which) => NativeCallbacks.Bindings.SkinnedMeshRenderer_GetAsset(id, which);
    internal static void SkinnedMeshRenderer_SetAsset(ulong id, int which, ulong guid) => NativeCallbacks.Bindings.SkinnedMeshRenderer_SetAsset(id, which, guid);
    internal static void SkinnedMeshRenderer_Play(ulong id, string? clip, float crossFade)
    {
        byte[] buf = EncodeUtf8Name(clip);
        fixed (byte* p = buf) NativeCallbacks.Bindings.SkinnedMeshRenderer_Play(id, p, crossFade);
    }
    internal static void SkinnedMeshRenderer_Control(ulong id, int op) => NativeCallbacks.Bindings.SkinnedMeshRenderer_Control(id, op);
    internal static string SkinnedMeshRenderer_GetClip(ulong id) => ReadNativeString(NativeCallbacks.Bindings.SkinnedMeshRenderer_GetClip, id);
    internal static int SkinnedMeshRenderer_GetClipCount(ulong id) => NativeCallbacks.Bindings.SkinnedMeshRenderer_GetClipCount(id);
    internal static string SkinnedMeshRenderer_GetClipName(ulong id, int index)
    {
        int required = NativeCallbacks.Bindings.SkinnedMeshRenderer_GetClipName(id, index, null, 0);
        if (required <= 0) return "";
        byte[] buffer = new byte[required + 1];
        fixed (byte* p = buffer)
        {
            int written = NativeCallbacks.Bindings.SkinnedMeshRenderer_GetClipName(id, index, p, buffer.Length);
            return DecodeNativeString(buffer, written);
        }
    }
    internal static float SkinnedMeshRenderer_GetClipDuration(ulong id, int index) => NativeCallbacks.Bindings.SkinnedMeshRenderer_GetClipDuration(id, index);
    internal static bool SkinnedMeshRenderer_GetBone(ulong id, string bone, Span<float> values)
    {
        byte[] buf = EncodeUtf8Name(bone);
        fixed (byte* b = buf)
        fixed (float* p = values)
            return NativeCallbacks.Bindings.SkinnedMeshRenderer_GetBone(id, b, p, values.Length) != 0;
    }

    internal static void ParticleSystem3D_Get(ulong id, Span<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.ParticleSystem3D_Get(id, p, values.Length);
    }
    internal static void ParticleSystem3D_Set(ulong id, ReadOnlySpan<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.ParticleSystem3D_Set(id, p, values.Length);
    }
    internal static void ParticleSystem3D_Control(ulong id, int op) => NativeCallbacks.Bindings.ParticleSystem3D_Control(id, op);
    internal static void ParticleSystem3D_Emit(ulong id, int count) => NativeCallbacks.Bindings.ParticleSystem3D_Emit(id, count);
    internal static int ParticleSystem3D_GetState(ulong id, out int particleCount)
    {
        int count = 0;
        int state = NativeCallbacks.Bindings.ParticleSystem3D_GetState(id, &count);
        particleCount = count;
        return state;
    }
    internal static ulong ParticleSystem3D_GetTexture(ulong id) => NativeCallbacks.Bindings.ParticleSystem3D_GetTexture(id);
    internal static void ParticleSystem3D_SetTexture(ulong id, ulong textureGuid) => NativeCallbacks.Bindings.ParticleSystem3D_SetTexture(id, textureGuid);

    internal static bool Material_Get(ulong guid, Span<float> values)
    {
        fixed (float* p = values) return NativeCallbacks.Bindings.Material_Get(guid, p, values.Length) != 0;
    }
    internal static void Material_Set(ulong guid, ReadOnlySpan<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.Material_Set(guid, p, values.Length);
    }
    internal static bool Mesh_GetInfo(ulong guid, Span<float> values)
    {
        fixed (float* p = values) return NativeCallbacks.Bindings.Mesh_GetInfo(guid, p, values.Length) != 0;
    }
    internal static ulong Mesh_CreateRuntime() => NativeCallbacks.Bindings.Mesh_CreateRuntime();
    internal static bool Mesh_SetData(ulong guid, ReadOnlySpan<float> positions, ReadOnlySpan<float> normals, ReadOnlySpan<float> uvs,
        int vertexCount, ReadOnlySpan<uint> indices)
    {
        fixed (float* pp = positions)
        fixed (float* pn = normals)
        fixed (float* pu = uvs)
        fixed (uint* pi = indices)
        {
            // An empty span pins to null, which tells native to recalculate normals / zero the UVs.
            return NativeCallbacks.Bindings.Mesh_SetData(guid, pp, normals.IsEmpty ? null : pn, uvs.IsEmpty ? null : pu,
                vertexCount, pi, indices.Length) != 0;
        }
    }
    internal static bool Mesh_DestroyRuntime(ulong guid) => NativeCallbacks.Bindings.Mesh_DestroyRuntime(guid) != 0;
    internal static bool Mesh_IsRuntime(ulong guid) => NativeCallbacks.Bindings.Mesh_IsRuntime(guid) != 0;
    internal static ulong Material_GetTexture(ulong guid) => NativeCallbacks.Bindings.Material_GetTexture(guid);
    internal static void Material_SetTexture(ulong guid, ulong textureGuid) => NativeCallbacks.Bindings.Material_SetTexture(guid, textureGuid);
    internal static ulong Material_GetShader(ulong guid) => NativeCallbacks.Bindings.Material_GetShader(guid);
    internal static void Material_SetShader(ulong guid, ulong shaderGuid) => NativeCallbacks.Bindings.Material_SetShader(guid, shaderGuid);

    // Returns the native ShaderPropertyType + 1, or 0 when the property is unknown.
    internal static int Material_GetProperty(ulong guid, string name, Span<float> values, out ulong texture)
    {
        byte[] buf = EncodeUtf8Name(name);
        ulong textureGuid = 0;
        int result;
        fixed (byte* pName = buf)
        fixed (float* pValues = values)
        {
            result = NativeCallbacks.Bindings.Material_GetProperty(guid, pName, pValues, &textureGuid);
        }
        texture = textureGuid;
        return result;
    }

    internal static bool Material_SetProperty(ulong guid, string name, int type, ReadOnlySpan<float> values, ulong texture)
    {
        byte[] buf = EncodeUtf8Name(name);
        fixed (byte* pName = buf)
        fixed (float* pValues = values)
        {
            return NativeCallbacks.Bindings.Material_SetProperty(guid, pName, type, pValues, texture) != 0;
        }
    }

    internal static void PointLight_Get(ulong id, Span<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.PointLight_Get(id, p, values.Length);
    }
    internal static void PointLight_Set(ulong id, ReadOnlySpan<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.PointLight_Set(id, p, values.Length);
    }
    internal static void SpotLight_Get(ulong id, Span<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.SpotLight_Get(id, p, values.Length);
    }
    internal static void SpotLight_Set(ulong id, ReadOnlySpan<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.SpotLight_Set(id, p, values.Length);
    }
    internal static void Camera3D_GetSky(ulong id, Span<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.Camera3D_GetSky(id, p, values.Length);
    }
    internal static void Camera3D_SetSky(ulong id, ReadOnlySpan<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.Camera3D_SetSky(id, p, values.Length);
    }
    internal static ulong Camera3D_GetSkyboxMaterial(ulong id) => NativeCallbacks.Bindings.Camera3D_GetSkyboxMaterial(id);
    internal static void Camera3D_SetSkyboxMaterial(ulong id, ulong materialGuid) => NativeCallbacks.Bindings.Camera3D_SetSkyboxMaterial(id, materialGuid);

    // ── Animator ───────────────────────────────────────────────

    internal static bool Animator_SetParameter(ulong id, string name, int op, float value)
    {
        byte[] buf = EncodeUtf8Name(name);
        fixed (byte* p = buf) return NativeCallbacks.Bindings.Animator_SetParameter(id, p, op, value) != 0;
    }

    internal static float Animator_GetParameter(ulong id, string name, int op)
    {
        byte[] buf = EncodeUtf8Name(name);
        fixed (byte* p = buf) return NativeCallbacks.Bindings.Animator_GetParameter(id, p, op);
    }

    internal static bool Animator_HasParameter(ulong id, string name)
    {
        byte[] buf = EncodeUtf8Name(name);
        fixed (byte* p = buf) return NativeCallbacks.Bindings.Animator_HasParameter(id, p) != 0;
    }

    internal static bool Animator_Play(ulong id, string state, float normalizedTime, float crossFade)
    {
        byte[] buf = EncodeUtf8Name(state);
        fixed (byte* p = buf) return NativeCallbacks.Bindings.Animator_Play(id, p, normalizedTime, crossFade) != 0;
    }

    internal static string Animator_GetState(ulong id, int which)
    {
        int required = NativeCallbacks.Bindings.Animator_GetStateBuffer(id, which, null, 0);
        if (required <= 0) return "";
        byte[] buffer = new byte[required + 1];
        fixed (byte* p = buffer)
        {
            int written = NativeCallbacks.Bindings.Animator_GetStateBuffer(id, which, p, buffer.Length);
            return DecodeNativeString(buffer, written);
        }
    }

    internal static void Animator_GetPlayback(ulong id, Span<float> values)
    {
        fixed (float* p = values) NativeCallbacks.Bindings.Animator_GetPlayback(id, p, values.Length);
    }

    internal static ulong Animator_GetAsset(ulong id, int which) => NativeCallbacks.Bindings.Animator_GetAsset(id, which);
    internal static void Animator_SetAsset(ulong id, int which, ulong guid) => NativeCallbacks.Bindings.Animator_SetAsset(id, which, guid);
    internal static float Animator_GetSpeed(ulong id) => NativeCallbacks.Bindings.Animator_GetSpeed(id);
    internal static void Animator_SetSpeed(ulong id, float speed) => NativeCallbacks.Bindings.Animator_SetSpeed(id, speed);

    internal static bool Camera3D_ScreenPointToRay(ulong id, Vector2 screenPoint, out Ray ray)
    {
        float* v = stackalloc float[6];
        if (NativeCallbacks.Bindings.Camera3D_ScreenPointToRay(id, screenPoint.X, screenPoint.Y, v, 6) == 0)
        {
            ray = new Ray(new Vector3(0f), new Vector3(0f, 0f, -1f));
            return false;
        }
        ray = new Ray(new Vector3(v[0], v[1], v[2]), new Vector3(v[3], v[4], v[5]));
        return true;
    }

    internal static bool Camera3D_WorldToScreenPoint(ulong id, Vector3 world, out Vector3 screen)
    {
        float* v = stackalloc float[3];
        int inFront = NativeCallbacks.Bindings.Camera3D_WorldToScreenPoint(id, world.X, world.Y, world.Z, v, 3);
        screen = new Vector3(v[0], v[1], v[2]);
        return inFront != 0;
    }
}
