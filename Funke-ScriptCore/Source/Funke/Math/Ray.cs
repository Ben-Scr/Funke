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

    public bool TryIntersectSphere(Vector3 center, float radius, out float distance)
    {
        distance = 0;
        if (!float.IsFinite(radius) || radius < 0) throw new ArgumentOutOfRangeException(nameof(radius));
        Vector3.ValidateFinite(center, nameof(center));
        if (!Vector3.IsFinite(Origin) || !Vector3.TryNormalize(Direction, out Vector3 unit)) return false;
        double x = (double)center.X - Origin.X, y = (double)center.Y - Origin.Y, z = (double)center.Z - Origin.Z;
        double radiusSquared = (double)radius * radius;
        if (x * x + y * y + z * z <= radiusSquared) return true;
        double along = x * unit.X + y * unit.Y + z * unit.Z;
        if (along < 0) return false;
        double dx = x - along * unit.X, dy = y - along * unit.Y, dz = z - along * unit.Z;
        double chordSquared = radiusSquared - (dx * dx + dy * dy + dz * dz);
        if (chordSquared < 0) return false;
        double hit = Math.Max(0, along - Math.Sqrt(chordSquared));
        if (hit > float.MaxValue) return false;
        distance = (float)hit;
        return true;
    }

    public override string ToString() => $"Ray(origin {Origin}, direction {Direction})";
}

/// <summary>World-space plane with unit Normal and equation Dot(Normal, point) + Distance = 0; default is invalid.</summary>
public readonly struct Plane
{
    public Plane(Vector3 normal, Vector3 point)
    {
        Vector3.ValidateFinite(normal, nameof(normal));
        Vector3.ValidateFinite(point, nameof(point));
        double length = Math.Sqrt((double)normal.X * normal.X + (double)normal.Y * normal.Y + (double)normal.Z * normal.Z);
        if (length == 0) throw new ArgumentOutOfRangeException(nameof(normal));
        Normal = new((float)(normal.X / length), (float)(normal.Y / length), (float)(normal.Z / length));
        Distance = (float)(-((double)Normal.X * point.X + (double)Normal.Y * point.Y + (double)Normal.Z * point.Z));
        if (!float.IsFinite(Distance)) throw new ArgumentOutOfRangeException(nameof(point));
    }

    public Vector3 Normal { get; }
    public float Distance { get; }

    /// <summary>False for invalid/parallel rays or intersections behind the origin; a hit at distance 0 is valid.</summary>
    public bool TryRaycast(Ray ray, out float distance)
    {
        distance = 0;
        if (Normal.LengthSquared() == 0 || !Vector3.IsFinite(ray.Origin) || !Vector3.IsFinite(ray.Direction)) return false;
        double denominator = (double)Normal.X * ray.Direction.X + (double)Normal.Y * ray.Direction.Y + (double)Normal.Z * ray.Direction.Z;
        if (Math.Abs(denominator) <= Mathf.Epsilon) return false;
        double value = -((double)Normal.X * ray.Origin.X + (double)Normal.Y * ray.Origin.Y + (double)Normal.Z * ray.Origin.Z + Distance) / denominator;
        if (!double.IsFinite(value) || value < 0 || value > float.MaxValue) return false;
        distance = (float)value;
        return true;
    }
}
