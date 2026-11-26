using Funke;
using Funke.Components;

public sealed class BenchmarkMover : EntityScript
{
    private Transform2D _transform = null!;

    public override void OnStart() => _transform = Transform2D!;

    public override void OnUpdate()
    {
        var position = _transform.Position;
        position.X += 1.0f;
        _transform.Position = position;
    }
}
