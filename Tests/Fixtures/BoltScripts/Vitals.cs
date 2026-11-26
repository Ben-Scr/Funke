using Funke;

namespace MixedGameplay;

public struct ManagedMeter : Funke.Components.IComponent
{
    public byte Channel;
    public int Value;
}

public class Vitals : Component
{
    public int Points = 21;
    public string Label { get; set; } = "ready";
    public Vitals? Related;
    public string ProbeId { get; } = "bolt-api-" + System.Guid.NewGuid().ToString("N");
    private readonly System.Threading.Tasks.TaskCompletionSource<int> source = new();
    public System.Threading.Tasks.Task<int> Pending => source.Task;
    private readonly System.Threading.CancellationTokenSource cancellation = new();
    public System.Threading.CancellationToken Stop => cancellation.Token;
    public void Resolve()
    {
        if (source.Task.IsCompleted) return;
        bool received = PlayerPrefs.GetLong(ProbeId) == 9007199254740993L
            && SaveData.Load(ProbeId, -1) == 73;
        PlayerPrefs.SetString(ProbeId, "Grüße");
        SaveData.SaveVersioned(ProbeId, 29, 2);
        source.TrySetResult(received ? 19 : -1);
        cancellation.Cancel();
    }
    public int DoublePoints => 2 * Points;
}

public class VitalsReader : EntityScript
{
    public override void OnUpdate()
    {
        Vitals? vitals = GetComponent<Vitals>();
        if (vitals is not null && vitals.Label == "Bolt=ok")
        {
            Transform2D!.Position = new Vector2(vitals.DoublePoints, 0);
            vitals.Resolve();
        }
    }
}
