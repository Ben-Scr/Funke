namespace Funke;

/// <summary>A half-line from Origin along Direction (normalized), e.g. from <c>Camera3D.ScreenPointToRay</c>.</summary>
public readonly struct Ray
{
    public Ray(Vector3 origin, Vector3 direction)
    {
        Origin = origin;
        Direction = direction.LengthSquared() > 0f ? direction.Normalized() : new Vector3(0f, 0f, -1f);
    }

    public Vector3 Origin { get; }
    public Vector3 Direction { get; }

    /// <summary>The point `distance` units along the ray.</summary>
    public Vector3 GetPoint(float distance) => Origin + Direction * distance;

    public override string ToString() => $"Ray(origin {Origin}, direction {Direction})";
}
