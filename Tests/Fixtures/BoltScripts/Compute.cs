using Funke;
using Funke.Components;

public sealed class BenchmarkCompute : EntityScript
{
    private Transform2D _transform = null!;
    private ulong _state;

    public override void OnStart() => _transform = Transform2D!;

    public override void OnUpdate()
    {
        ulong state = _state;
        for (int i = 0; i < 256; i++)
            state = (state * 25173ul + 13849ul) % 65521ul;
        _state = state;
        var position = _transform.Position;
        position.X += 1ul + state % 8ul;
        _transform.Position = position;
    }
}
