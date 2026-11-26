using System;
using System.Globalization;
using BoltScriptBench;
using Funke;
using Funke.UI;

public class BenchPanel : EntityScript
{
    private const int Samples = 7;
    private const int TargetNsPerEntity = 10;
    private static readonly CultureInfo German = CultureInfo.GetCultureInfo("de-DE");
    private static readonly string[] Operations = { "Create", "Move", "Read", "Destroy" };
    private readonly LifecycleTimes[] boltSamples = new LifecycleTimes[Samples];
    private readonly LifecycleTimes[] managedSamples = new LifecycleTimes[Samples];
    private BenchState state = null!;
    private InputField countInput = null!;
    private TextRenderer status = null!;
    private int round;
    private bool secondLanguage;
    private bool running;
    private bool waitingForBolt;
    private float requestedAt;

    private static T Control<T>(string name) where T : Component, new()
        => Entity.FindByName(name)!.GetComponent<T>()!;

    public override void OnStart()
    {
        state = GetComponent<BenchState>()!;
        countInput = Control<InputField>("Count");
        status = Control<TextRenderer>("Status");
        state.PointIds = true;
        ShowMode();
    }

    private void ShowMode()
    {
        Control<TextRenderer>("Mode Label").Text = state.PointIds ? "Modus: Punkt-IDs" : "Modus: Entity + Transform";
        Control<TextRenderer>("Create Detail").Text = state.PointIds ? "ID + XY im ECS" : "Entity.Create";
        Control<TextRenderer>("Destroy Detail").Text = state.PointIds ? "IDs freigeben" : "Entity.Destroy";
    }

    public void ToggleMode()
    {
        if (running) return;
        state.PointIds = !state.PointIds;
        ShowMode();
        ClearResults();
        status.Text = "Bereit";
    }

    public void Preset(int count)
    {
        if (!running) countInput.Text = count.ToString(CultureInfo.InvariantCulture);
    }

    public void Run()
    {
        if (running) return;
        if (!state.Ready) { status.Text = "Bolt ist noch nicht bereit. Bitte das Runtime-Log prüfen."; return; }
        if (!int.TryParse(countInput.Text, out int count) || count < 1 || count > 100000)
        {
            status.Text = "Bitte eine ganze Zahl zwischen 1 und 100.000 eingeben.";
            return;
        }
        state.Count = count;
        countInput.Entity.GetComponent<Interactable>()!.IsInteractable = false;
        round = 0;
        secondLanguage = false;
        running = true;
        ClearResults();
    }

    private void ClearResults()
    {
        Control<TextRenderer>("Warmup note").Text = "Ziel: 0,10 ms / 10.000";
        foreach (string op in Operations)
            foreach (string unit in new[] { "Batch", "Cost", "Rate" })
            {
                Control<TextRenderer>(op + unit).Text = "–";
                Control<TextRenderer>("Cs" + op + unit).Text = "–";
            }
    }

    public override void OnUpdate()
    {
        if (!running) return;
        // Round zero warms both paths. Alternate the first language in each pair.
        bool bolt = (round % 2 == 0) != secondLanguage;
        if (!waitingForBolt)
        {
            string phase = round == 0 ? "Aufwärmrunde" : $"Messung {round} von {Samples}";
            status.Text = $"{phase}  ·  {(bolt ? "Bolt" : "C#")}";
        }

        LifecycleTimes times;
        if (bolt)
        {
            if (!waitingForBolt)
            {
                requestedAt = Time.RealtimeSinceStartup;
                waitingForBolt = true;
                state.Valid = false;
                state.Requested = true;
                return;
            }
            if (state.Requested)
            {
                if (Time.RealtimeSinceStartup - requestedAt > 15f)
                    Finish("Bolt hat keine Messung geliefert. Bitte das Runtime-Log prüfen.");
                return;
            }
            waitingForBolt = false;
            if (!state.Valid)
            {
                Finish("Bolt-Messung ungültig. Entity-Lebenszyklus oder Positionssumme fehlerhaft.");
                return;
            }
            times = new(state.CreateTicks, state.MoveTicks, state.ReadTicks, state.DestroyTicks);
        }
        else
        {
            try { times = state.PointIds ? ManagedLifecycle.MeasurePoints(state.Count) : ManagedLifecycle.Measure(state.Count); }
            catch (Exception ex)
            {
                Finish("C#-Messung fehlgeschlagen. " + ex.Message);
                Log.Error(ex.ToString());
                return;
            }
        }

        if (round > 0) (bolt ? boltSamples : managedSamples)[round - 1] = times;
        if (secondLanguage) round++;
        secondLanguage = !secondLanguage;
        if (round <= Samples) return;

        int boltGoals = ShowResults(boltSamples, "", "Bolt");
        int managedGoals = ShowResults(managedSamples, "Cs", "C#");
        Control<TextRenderer>("Warmup note").Text = $"≤ {TargetNsPerEntity} ns: Bolt {boltGoals}/4 · C# {managedGoals}/4";
        Finish($"Abgeschlossen  ·  {state.Count.ToString("N0", German)} Entities  ·  je {Samples} Messungen");
    }

    private int ShowResults(LifecycleTimes[] samples, string prefix, string language)
    {
        int goals = 0;
        var ticks = new long[Samples];
        for (int op = 0; op < Operations.Length; op++)
        {
            for (int i = 0; i < Samples; i++)
                ticks[i] = op switch
                {
                    0 => samples[i].Create, 1 => samples[i].Move, 2 => samples[i].Read, _ => samples[i].Destroy
                };
            Array.Sort(ticks);
            long median = ticks[Samples / 2];
            if (median * 100 <= (long)TargetNsPerEntity * state.Count) goals++;
            string cell = prefix + Operations[op];
            Control<TextRenderer>(cell + "Batch").Text = (median / 10000.0).ToString("N4", German);
            Control<TextRenderer>(cell + "Cost").Text = (median * 100.0 / state.Count).ToString("N1", German);
            Control<TextRenderer>(cell + "Rate").Text = median > 0
                ? (state.Count * 10000000.0 / median).ToString("N0", German) : "–";
            Log.Info($"[BoltScriptBench] {language} {Operations[op]} count={state.Count} medianTicks={median} samples={Samples} mode={(state.PointIds ? "point-ids" : "scene-objects")}");
        }
        return goals;
    }

    private void Finish(string message)
    {
        waitingForBolt = false;
        state.Requested = false;
        running = false;
        countInput.Entity.GetComponent<Interactable>()!.IsInteractable = true;
        status.Text = message;
    }
}
