using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using Funke;
using Funke.Coroutines;
using Funke.Interop;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public sealed class HotReloadStateTests : IDisposable
{
    private readonly NativeBindingsStruct m_Previous = NativeCallbacks.Bindings;
    private readonly List<int> m_Handles = new();
    private static ulong s_Scene;

    public class Participant : EntityScript, IHotReloadState
    {
        public int Health = 100;
        public double Timer;
        public int Starts, Restores, Updates;
        public Task Pending = Task.CompletedTask;
        public override void OnStart() { ++Starts; Health = 100; Timer = 0; }
        public override void OnUpdate() { ++Updates; }
        public void Begin() => RunCoroutine(() => Pending = Wait());
        private async Task Wait() => await new WaitForFrames(1000);
        public void CaptureReloadState(ReloadState state) { state.Set("health", Health); state.Set("timer", Timer); }
        public void RestoreReloadState(ReloadState state)
        {
            Assert.Equal(1, Starts);
            Assert.Equal(0, Updates);
            if (state.TryGet<int>("health", out var health)) Health = health;
            if (state.TryGet<double>("timer", out var timer)) Timer = timer;
            ++Restores;
        }
    }

    public unsafe HotReloadStateTests()
    {
        ScriptInstanceManager.SetCoreAssembly(typeof(Participant).Assembly);
        NativeCallbacks.Bindings.Entity_GetOwningSceneId = &Scene;
        s_Scene = 77;
        CoroutineScheduler.Reset();
        ScriptInstanceManager.ClearReloadState();
    }

    [UnmanagedCallersOnly] private static ulong Scene(ulong _) => s_Scene;
    private unsafe int Create(ulong entity = 123)
    {
        byte[] name = Encoding.UTF8.GetBytes("type:" + typeof(Participant).FullName + "\0");
        delegate* unmanaged<byte*, ulong, int> create = &ScriptInstanceManager.CreateScriptInstance;
        fixed (byte* pointer = name) {
            int handle = create(pointer, entity);
            Assert.NotEqual(0, handle);
            m_Handles.Add(handle);
            return handle;
        }
    }
    private static Participant Instance(ulong entity = 123) => (Participant)ScriptInstanceManager.GetScriptInstance(entity, typeof(Participant))!;
    private static unsafe void Start(int handle)
    {
        delegate* unmanaged<int, void> awake = &ScriptInstanceManager.InvokeAwake;
        delegate* unmanaged<int, void> enable = &ScriptInstanceManager.InvokeOnEnable;
        delegate* unmanaged<int, void> start = &ScriptInstanceManager.InvokeStart;
        awake(handle); enable(handle); start(handle);
    }
    private unsafe void Destroy(int handle)
    {
        delegate* unmanaged<int, void> invoke = &ScriptInstanceManager.InvokeOnDestroy;
        delegate* unmanaged<int, void> destroy = &ScriptInstanceManager.DestroyScriptInstance;
        invoke(handle); destroy(handle); m_Handles.Remove(handle);
    }
    public void Dispose()
    {
        foreach (int handle in m_Handles.ToArray()) Destroy(handle);
        ScriptInstanceManager.ClearReloadState();
        ScriptInstanceManager.SetCoreAssembly(typeof(EntityScript).Assembly);
        NativeCallbacks.Bindings = m_Previous;
        CoroutineScheduler.Reset();
    }

    [Fact]
    public unsafe void SelectedStateRestoresAfterDelayedStartExactlyOnceAndOldCoroutineIsCancelled()
    {
        int old = Create(); Start(old);
        var original = Instance(); original.Health = 34; original.Timer = 12.5; original.Begin();
        ScriptInstanceManager.CaptureReloadState();
        Assert.False(original.Pending.IsCompleted);
        Destroy(old);
        CoroutineScheduler.PumpUpdate(0);
        Assert.True(original.Pending.IsCanceled);
        int replacement = Create();
        Assert.Equal(100, Instance().Health);
        Assert.Equal(0, Instance().Restores);
        Start(replacement); Start(replacement);
        Assert.Equal(34, Instance().Health);
        Assert.Equal(12.5, Instance().Timer);
        Assert.Equal(1, Instance().Restores);
        Assert.Equal(1, Instance().Starts);
        delegate* unmanaged<int, void> update = &ScriptInstanceManager.InvokeUpdate;
        update(replacement);
        Assert.Equal(1, Instance().Updates);
        Destroy(replacement);
        int nextSession = Create(); Start(nextSession);
        Assert.Equal(100, Instance().Health);
        Assert.Equal(0, Instance().Restores);
    }

    [Fact]
    public void SceneIdentityAndAbortPreventStateFromLeakingIntoUnrelatedInstances()
    {
        int old = Create(); Start(old); Instance().Health = 2;
        ScriptInstanceManager.CaptureReloadState(); Destroy(old);
        s_Scene = 78;
        int other = Create(); Start(other);
        Assert.Equal(100, Instance().Health);
        Assert.Equal(0, Instance().Restores);
        Destroy(other);
        ScriptInstanceManager.ClearReloadState();
        s_Scene = 77;
        int fresh = Create(); Start(fresh);
        Assert.Equal(100, Instance().Health);
    }

    [Fact]
    public void StateIsNeutralTypedAndReportsRemovedOrIncompatibleValues()
    {
        var capture = new ReloadState();
        capture.Set("health", 10); capture.Set("removed", "old"); capture.Set("entity", ulong.MaxValue);
        Assert.Throws<NotSupportedException>(() => capture.Set("object", new Participant()));
        var restored = new ReloadState(capture.Serialize());
        Assert.False(restored.TryGet<float>("health", out _));
        Assert.True(restored.TryGet<ulong>("entity", out var id)); Assert.Equal(ulong.MaxValue, id);
        Assert.Equal("health, removed", restored.UnreadKeys);
        Assert.Throws<InvalidOperationException>(() => restored.Set("health", 7));
    }
}
