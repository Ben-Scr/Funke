using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using Funke;
using Funke.Coroutines;

namespace Acceptance.Enemies { public class Controller : EntityScript { public int Value = 3; } }
namespace Acceptance.UI { public class Controller : EntityScript { public int Value = 7; } }

namespace Acceptance
{
    public class PlainData : Component { public int Value = 9; }
    public class Empty : EntityScript { }
    public class InspectorLimits : EntityScript
    {
        public float Scalar = 1;
        public List<float> Values = new() { 2, 3 };
        public List<List<int>> Nested = new();
        public Dictionary<string, int> Dictionary = new();
        [HideFromEditor] public Dictionary<string, int> Hidden = new();
    }

    public class SpawnObserver : EntityScript
    {
        public static ulong ExpectedParent;
        public static int Visits;
        public override void OnAwake()
        {
            if (ExpectedParent == 0) return;
            Driver.Check(Entity.Parent?.ID == ExpectedParent, "awake parent");
            if (Transform3D is { } three) Driver.Near(three.LocalPosition.Z, 7, "awake Z");
            else Driver.Near(Transform2D!.LocalPosition.Y, 3, "awake Y");
            ++Visits;
        }
    }

    public class Driver : EntityScript, IHotReloadState
    {
        public int Health = 100;
        public double Timer;
        public int Starts;
        public int Restores;
        public static int Resets;
        public static int Registered = 88;
        public static int Unregistered;
        public static Driver? Active;
        private readonly List<Entity> m_Created = new();
        private Entity? m_Two, m_Three;
        private int m_Updates;
        private Task m_Wait = Task.CompletedTask;

        [ResetOnPlay] private static void Reset() { ++Resets; Registered = 0; }
        public static void Check(bool value, string message) { if (!value) throw new InvalidOperationException("ACCEPT-FAIL: " + message); }
        public static void Near(float value, float expected, string message) => Check(MathF.Abs(value - expected) < .002f, message + ": " + value);
        private Entity Track(Entity value) { m_Created.Add(value); return value; }
        public override void OnStart()
        {
            Active = this; ++Starts; Health = 100; Timer = 0; ++Unregistered;
            RunCoroutine(() => m_Wait = Wait());
            var enemy = Entity.AddComponent<Enemies.Controller>();
            var ui = Entity.AddComponent<UI.Controller>();
            Check(enemy != null && ui != null && enemy.Value == 3 && ui.Value == 7, "namespaced Add/Get");
            Check(Entity.HasComponent<Enemies.Controller>() && Entity.GetComponent<Enemies.Controller>() == enemy, "namespaced Has/Get identity");
            Check(Entity.RemoveComponent<Enemies.Controller>() && !Entity.HasComponent<Enemies.Controller>(), "namespaced Remove");
            Check(Entity.GetComponent<UI.Controller>() == ui, "same short name preserved");
            var data = Entity.AddComponent<PlainData>();
            Check(data != null && Entity.HasComponent<PlainData>() && Entity.GetComponent<PlainData>() == data, "managed component identity");
            Check(Entity.RemoveComponent<PlainData>(), "managed remove");

            var p2 = Track(Create2D("AcceptParent2", new(10, 20), 90)); p2.Transform2D!.Scale = new(2, 3);
            var p3 = Track(Create3D("AcceptParent3", new(10, 20, 30), new(0, 0, 90))); p3.Transform3D!.Scale = new(2, 3, 4);
            var source2 = Track(Create2D("AcceptSource2")); source2.AddComponent<SpawnObserver>();
            var source3 = Track(Create3D("AcceptSource3")); source3.AddComponent<SpawnObserver>();
            SpawnObserver.ExpectedParent = p2.ID;
            m_Two = Track(Instantiate2D(source2, new(2, 3), 90, p2));
            SpawnObserver.ExpectedParent = p3.ID;
            m_Three = Track(Instantiate3D(source3, new(2, 3, 7), new(0, 90, 0), p3));
            SpawnObserver.ExpectedParent = 0;
            Check(SpawnObserver.Visits >= 2, "spawn observer callbacks");
            Check(m_Three.Transform2D == null && m_Three.Transform3D != null, "pure 3D nullable transform");
            Near(m_Three.Transform3D!.LocalPosition.Z, 7, "local Z");
            Near(m_Two.Transform2D!.LocalPosition.X, 2, "local 2D X");
            try { Instantiate3D(source2); throw new Exception("ACCEPT-FAIL: wrong dimension accepted"); }
            catch (InvalidOperationException) { }
            Print("ACCEPT-START: reset=" + Resets + " registered=" + Registered + " unregistered=" + Unregistered);
        }
        private async Task Wait() => await new WaitForFrames(100000);
        public override void OnUpdate()
        {
            if (++m_Updates != 3) return;
            Near(m_Three!.Transform3D!.WorldPosition.X, 1, "world 3D X");
            Near(m_Three.Transform3D.WorldPosition.Y, 24, "world 3D Y");
            Near(m_Three.Transform3D.WorldPosition.Z, 58, "world 3D Z");
            Near(m_Two!.Transform2D!.WorldPosition.X, 1, "world 2D X");
            Near(m_Two.Transform2D.WorldPosition.Y, 24, "world 2D Y");
            m_Two.Transform2D.WorldPosition = new(8, 24);
            Near(m_Two.Transform2D.LocalPosition.X, 2, "world write local X");
            Near(m_Two.Transform2D.LocalPosition.Y, 2f / 3, "world write local Y");
            Print("ACCEPT-SPAWN-PASS");
        }
        public override void OnDestroy()
        {
            Print("ACCEPT-DESTROY: cancelled=" + DestroyToken.IsCancellationRequested);
            foreach (var entity in m_Created) entity.Destroy();
            m_Created.Clear();
        }
        public void CaptureReloadState(ReloadState state) { state.Set("health", Health); state.Set("timer", Timer); }
        public void RestoreReloadState(ReloadState state)
        {
            Check(Starts == 1 && m_Updates == 0, "restore after Start before Update");
            if (state.TryGet<int>("health", out var health)) Health = health;
            if (state.TryGet<double>("timer", out var timer)) Timer = timer;
            ++Restores;
            Print("ACCEPT-RESTORE: " + Health + ":" + Timer);
        }
        [DevCommand("accept.set")] public static string Set() { Active!.Health = 37; Active.Timer = 12.5; Registered = 99; return "set"; }
        [DevCommand("accept.state")] public static string State() => FormattableString.Invariant($"health={Active?.Health};timer={Active?.Timer};starts={Active?.Starts};restores={Active?.Restores};resets={Resets};registered={Registered};unregistered={Unregistered}");
    }
}
