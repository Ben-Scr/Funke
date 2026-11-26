using System;

namespace Funke;

/// <summary>Inclusive axis-aligned bounds in the coordinate system of the supplied endpoints.</summary>
public readonly struct Bounds3D
{
    public Vector3 Min { get; }
    public Vector3 Max { get; }

    public Bounds3D(Vector3 min, Vector3 max)
    {
        Vector3.ValidateFinite(min, nameof(min));
        Vector3.ValidateFinite(max, nameof(max));
        if (min.X > max.X || min.Y > max.Y || min.Z > max.Z)
            throw new ArgumentException("Minimum exceeds maximum.", nameof(min));
        Min = min;
        Max = max;
    }

    public bool Contains(Vector3 p) => p.X >= Min.X && p.X <= Max.X && p.Y >= Min.Y && p.Y <= Max.Y && p.Z >= Min.Z && p.Z <= Max.Z;

    public bool Intersects(Bounds3D other) => Min.X <= other.Max.X && Max.X >= other.Min.X &&
        Min.Y <= other.Max.Y && Max.Y >= other.Min.Y && Min.Z <= other.Max.Z && Max.Z >= other.Min.Z;

    public Vector3 ClosestPoint(Vector3 point) => new(Math.Clamp(point.X, Min.X, Max.X),
        Math.Clamp(point.Y, Min.Y, Max.Y), Math.Clamp(point.Z, Min.Z, Max.Z));

    public Bounds3D Encapsulate(Vector3 p) => new(
        new(MathF.Min(Min.X, p.X), MathF.Min(Min.Y, p.Y), MathF.Min(Min.Z, p.Z)),
        new(MathF.Max(Max.X, p.X), MathF.Max(Max.Y, p.Y), MathF.Max(Max.Z, p.Z)));

    /// <summary>Grows each face outward by the specified distance.</summary>
    public Bounds3D Expand(float distance)
    {
        if (!float.IsFinite(distance) || distance < 0) throw new ArgumentOutOfRangeException(nameof(distance));
        Vector3 extent = new(distance);
        return new(Min - extent, Max + extent);
    }

    public bool TryRaycast(Ray ray, out float distance)
    {
        distance = 0;
        if (!Vector3.IsFinite(ray.Origin) || !Vector3.TryNormalize(ray.Direction, out Vector3 direction)) return false;
        double near = 0, far = double.PositiveInfinity;
        if (!Clip(ray.Origin.X, direction.X, Min.X, Max.X, ref near, ref far) ||
            !Clip(ray.Origin.Y, direction.Y, Min.Y, Max.Y, ref near, ref far) ||
            !Clip(ray.Origin.Z, direction.Z, Min.Z, Max.Z, ref near, ref far) || near > float.MaxValue) return false;
        distance = (float)near;
        return true;
    }

    private static bool Clip(double origin, double direction, double min, double max, ref double near, ref double far)
    {
        if (direction == 0) return origin >= min && origin <= max;
        double t1 = (min - origin) / direction, t2 = (max - origin) / direction;
        near = Math.Max(near, Math.Min(t1, t2));
        far = Math.Min(far, Math.Max(t1, t2));
        return near <= far;
    }
}
