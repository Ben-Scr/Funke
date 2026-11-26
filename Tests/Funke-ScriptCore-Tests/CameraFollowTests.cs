using System;
using Funke;
using Xunit;

namespace FunkeScriptCoreTests;

public class CameraFollowTests
{
    [Theory]
    [InlineData(30)]
    [InlineData(60)]
    [InlineData(144)]
    public void DampingMatchesElapsedTime(int frames)
    {
        var follow = new CameraFollow3D { Offset = Vector3.Zero, Damping = 3 };
        follow.Step(Vector3.Zero, 0);
        for (int i = 0; i < frames; i++) follow.Step(new Vector3(10, 0, 0), 1f / frames);
        Assert.InRange(MathF.Abs(follow.Position.X - 10 * (1 - MathF.Exp(-3))), 0, 0.00002f);
    }

    [Fact]
    public void PauseResetAndSnapHaveExplicitSemantics()
    {
        var follow = new CameraFollow2D { Offset = new Vector2(2, 3), Damping = 0 };
        Assert.Equal(new Vector2(2, 3), follow.Step(Vector2.Zero, 0));
        Assert.Equal(new Vector2(2, 3), follow.Step(new Vector2(100, 100), 0));
        Assert.Equal(new Vector2(102, 103), follow.Step(new Vector2(100, 100), 0.1f));
        follow.Reset();
        Assert.False(follow.IsInitialized);
        Assert.Equal(new Vector2(202, 203), follow.Step(new Vector2(200, 200), 0));
    }

    [Theory]
    [InlineData(0, 0)]
    [InlineData(90, 45)]
    [InlineData(360, 90)]
    [InlineData(-720, -90)]
    public void OrbitKeepsRadiusAndLooksAtTarget(float yaw, float pitch)
    {
        var follow = new CameraFollow3D();
        Vector3 target = new(3, 4, 5);
        Vector3 position = follow.StepOrbit(target, yaw, pitch, 10, 0.016f);
        Assert.InRange(Vector3.Distance(position, target), 9.999f, 10.001f);
        Assert.InRange(Vector3.Dot(follow.LookAt(target) * Vector3.Forward, (target-position).Normalized()), 0.9999f, 1.0001f);
    }

    [Fact]
    public void InvalidInputCannotPoisonHistory()
    {
        var follow = new CameraFollow3D();
        Vector3 initial = follow.Step(Vector3.Zero, 0);
        Assert.Throws<ArgumentOutOfRangeException>(() => follow.Step(new Vector3(float.NaN), 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => follow.Step(Vector3.Zero, -1));
        Assert.Throws<ArgumentOutOfRangeException>(() => follow.StepOrbit(Vector3.Zero, 0, 0, 0, 1));
        Assert.Equal(initial, follow.Position);
        follow.Damping = float.PositiveInfinity;
        Assert.Throws<ArgumentOutOfRangeException>(() => follow.Step(Vector3.Zero, 1));
    }

    [Fact]
    public void OppositeFiniteCoordinatesDoNotOverflowDuringDamping()
    {
        var follow = new CameraFollow3D { Offset = Vector3.Zero, Damping = 1 };
        follow.Step(new Vector3(float.MaxValue, 0, 0), 0);
        Assert.True(float.IsFinite(follow.Step(new Vector3(-float.MaxValue, 0, 0), 1).X));
    }
}
