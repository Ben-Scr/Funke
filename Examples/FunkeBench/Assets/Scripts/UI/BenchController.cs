using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using Funke;
using Funke.UI;

public class BenchController : EntityScript
{
    public const int MaximumEntities = BenchmarkSettings.MaximumEntities;
    public static BenchController? Instance { get; private set; }
    public BenchmarkSession? Session { get; private set; }
    public InputField? CountInput, IterationsInput, DurationInput;
    public Toggle? JobsToggle, AnimateToggle, VsyncToggle;
    public Entity? Camera2DEntity, Camera3DEntity, EmptyState;
    public List<Entity> ScenarioMarkers = new();
    public TextRenderer? ScenarioTitle, PreviewNote, StatusLabel;

    private BenchmarkKind m_Selected;
    private float m_NextRefresh, m_NoticeUntil;
    private string m_Notice = "";
    private bool m_Locked;
    private bool m_HasOriginalSettings, m_OriginalVsync;
    private float m_OriginalFrameRate;
    private EntityCommandBufferPlaybackMode m_PlaybackMode;

    public override void OnStart()
    {
        Instance = this;
        Session = new BenchmarkSession(SetView);
        m_OriginalVsync = Application.VsyncEnabled;
        m_OriginalFrameRate = Application.TargetFrameRate;
        m_HasOriginalSettings = true;
        Application.VsyncEnabled = false;
        Application.TargetFrameRate = 0;
        SelectScenario(0);
        Log.Info("[FunkeBench] Ready");
    }

    public override void OnUpdate()
    {
        if (Session == null) return;
        try { Session.Tick(); }
        catch (Exception ex) { Session.Cancel(); Notice("Benchmark failed: " + ex.Message); Log.Error(ex.ToString()); }
        if (Session.IsBusy != m_Locked) LockInputs(Session.IsBusy);
        if (Time.RealtimeSinceStartup < m_NextRefresh) return;
        m_NextRefresh = Time.RealtimeSinceStartup + 0.25f;
        if (!Session.IsBusy && VsyncToggle != null && Application.VsyncEnabled != VsyncToggle.IsOn)
            Application.VsyncEnabled = VsyncToggle.IsOn;
        if (EmptyState != null) EmptyState.IsEnabled = Session.LiveEntityCount == 0 || Session.WorkloadKind < BenchmarkKind.Render2D;
        SetText(StatusLabel, Time.RealtimeSinceStartup < m_NoticeUntil ? m_Notice : Session.Status);
    }

    public void SelectScenario(int index)
    {
        if (Session?.IsBusy == true) { Notice("Stop the active benchmark before selecting another one."); return; }
        m_Selected = (BenchmarkKind)Math.Clamp(index, 0, 4);
        for (int i = 0; i < ScenarioMarkers.Count; i++) ScenarioMarkers[i].IsEnabled = i == (int)m_Selected;
        SetText(ScenarioTitle, DisplayName(m_Selected));
        SetText(PreviewNote, m_Selected >= BenchmarkKind.Render2D
            ? "Full application frames, including UI. Setup and warmup excluded."
            : m_Selected == BenchmarkKind.Move
                ? "Native transform writes: serial loop or joined Funke Jobs."
                : "Operation timing. Create and destroy use one command-buffer playback.");
    }

    public void SetCount(int count)
    {
        if (Session?.IsBusy == true) return;
        ValidateCount(count);
        if (CountInput != null) CountInput.Text = count.ToString(CultureInfo.InvariantCulture);
    }

    public void Run() => StartFromControls(false);
    public void RunOnce() => StartFromControls(true);

    private void StartFromControls(bool once)
    {
        if (Session?.IsBusy == true) { Notice("A benchmark is already running."); return; }
        if (!ReadNumber(CountInput, 1, MaximumEntities, "Entity count", out int count)) return;
        int iterations = 1, seconds = 1;
        bool rendering = m_Selected == BenchmarkKind.Render2D || m_Selected == BenchmarkKind.Render3D;
        if (!once && (rendering
            ? !ReadNumber(DurationInput, 1, 120, "Render seconds", out seconds)
            : !ReadNumber(IterationsInput, 1, BenchmarkSettings.MaximumIterations, "Operation samples", out iterations))) return;
        RunCommand(m_Selected, count, JobsToggle?.IsOn ?? false, seconds, iterations, once);
    }

    public void RunCommand(BenchmarkKind kind, int count, bool jobs, float seconds, int samples, bool once = false)
    {
        if (Session == null) throw new InvalidOperationException("FunkeBench is not ready.");
        if (Session.IsBusy) throw new InvalidOperationException("A benchmark is already running.");
        ValidateCount(count);
        if (!Enum.IsDefined(kind)) throw new ArgumentOutOfRangeException(nameof(kind));
        SelectScenario((int)kind);
        SetCount(count);
        if (JobsToggle != null) JobsToggle.IsOn = jobs;
        Application.VsyncEnabled = VsyncToggle?.IsOn ?? false;
        m_NoticeUntil = 0;
        Session.Start(kind, new BenchmarkSettings { Count = count, UseJobs = jobs, Iterations = once ? 1 : samples,
            WarmupIterations = once ? 0 : 2, WarmupSeconds = once ? 0 : 1, DurationSeconds = once ? 1 : seconds,
            Animate = AnimateToggle?.IsOn ?? false, PlaybackMode = m_PlaybackMode });
        LockInputs(Session.IsBusy);
    }

    public void SetPlaybackMode(EntityCommandBufferPlaybackMode mode)
    {
        if (Session?.IsBusy == true) throw new InvalidOperationException("Stop the active benchmark before changing playback.");
        if (!Enum.IsDefined(mode)) throw new ArgumentOutOfRangeException(nameof(mode));
        m_PlaybackMode = mode;
        Notice("Requested native ECB playback: " + mode + ". Jobs controls recording separately.");
    }

    public void SetAnimate(bool enabled)
    {
        if (Session?.IsBusy == true) throw new InvalidOperationException("Stop the active benchmark before changing animation.");
        if (AnimateToggle != null) AnimateToggle.IsOn = enabled;
    }

    public void SetVsync(bool enabled)
    {
        if (Session?.IsBusy == true) throw new InvalidOperationException("Stop the active benchmark before changing VSync.");
        if (VsyncToggle != null) VsyncToggle.IsOn = enabled;
        Application.VsyncEnabled = enabled;
    }

    private static void ValidateCount(int count)
    {
        if (count < 1 || count > MaximumEntities)
            throw new ArgumentOutOfRangeException(nameof(count), "Entity count must be between 1 and 100,000.");
    }

    public void Stop() { Session?.Cancel(); Notice("Measurement stopped. Completed results are retained."); }
    public void Clear() { Session?.Clear(); Notice("Workload cleared. Completed results are retained."); }
    public void Export()
    {
        try { Notice("CSV saved: " + ExportResults()); }
        catch (Exception ex) { Notice("Export failed: " + ex.Message); Log.Error(ex.ToString()); }
    }

    public string ExportResults()
    {
        if (Session == null) throw new InvalidOperationException("FunkeBench is not ready.");
        string path = Session.ExportCsv(Path.Combine(Application.PersistentDataPath, "Results"));
        Log.Info("[FunkeBench] CSV " + path);
        return path;
    }

    private void SetView(BenchmarkKind kind)
    {
        if (Camera2DEntity != null) Camera2DEntity.IsEnabled = kind != BenchmarkKind.Render3D;
        if (Camera3DEntity != null) Camera3DEntity.IsEnabled = kind == BenchmarkKind.Render3D;
    }

    private void LockInputs(bool locked)
    {
        m_Locked = locked;
        LockInput(CountInput, locked);
        LockInput(IterationsInput, locked);
        LockInput(DurationInput, locked);
        if (JobsToggle != null) JobsToggle.IsReadOnly = locked;
        if (AnimateToggle != null) AnimateToggle.IsReadOnly = locked;
        if (VsyncToggle != null) VsyncToggle.IsReadOnly = locked;
    }

    private static void LockInput(InputField? field, bool locked)
    {
        if (field == null) return;
        if (locked) field.IsFocused = false;
        var interactable = field.Entity.GetComponent<Interactable>();
        if (interactable != null) interactable.IsInteractable = !locked;
    }

    private bool ReadNumber(InputField? field, int min, int max, string name, out int value)
    {
        if (int.TryParse(field?.Text, NumberStyles.Integer, CultureInfo.InvariantCulture, out value) && value >= min && value <= max) return true;
        Notice($"{name} must be between {min:N0} and {max:N0}.");
        return false;
    }

    private void Notice(string text) { m_Notice = text; m_NoticeUntil = Time.RealtimeSinceStartup + 8; SetText(StatusLabel, text); }
    public static void SetText(TextRenderer? label, string value) { if (label != null && label.Text != value) label.Text = value; }
    public static string DisplayName(BenchmarkKind kind) => kind switch {
        BenchmarkKind.Create => "Create entities", BenchmarkKind.Destroy => "Destroy entities",
        BenchmarkKind.Move => "Move entities", BenchmarkKind.Render2D => "Render 2D sprites", _ => "Render 3D cubes" };

    public override void OnDestroy()
    {
        try { Session?.Dispose(); }
        finally
        {
            Session = null;
            if (m_HasOriginalSettings)
            {
                Application.VsyncEnabled = m_OriginalVsync;
                Application.TargetFrameRate = m_OriginalFrameRate;
                m_HasOriginalSettings = false;
            }
            if (ReferenceEquals(Instance, this)) Instance = null;
        }
    }
}
