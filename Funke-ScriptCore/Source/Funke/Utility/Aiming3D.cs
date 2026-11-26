using System;
using Funke.Physics;

namespace Funke;

public static class Aiming3D
{
    public static bool InViewCone(Vector3 origin, Vector3 forward, Vector3 point, float halfAngleDegrees, float range)
    {
        if (!float.IsFinite(range) || range < 0) throw new ArgumentOutOfRangeException(nameof(range));
        if (!float.IsFinite(halfAngleDegrees) || halfAngleDegrees < 0 || halfAngleDegrees > 180)
            throw new ArgumentOutOfRangeException(nameof(halfAngleDegrees));
        Vector3.ValidateFinite(origin, nameof(origin));
        Vector3.ValidateFinite(point, nameof(point));
        if (!Vector3.TryNormalize(forward, out forward)) return false;
        Vector3 offset = point - origin;
        double squared = (double)offset.X * offset.X + (double)offset.Y * offset.Y + (double)offset.Z * offset.Z;
        return squared <= (double)range * range && Vector3.Angle(forward, offset) <= halfAngleDegrees;
    }

    /// <summary>World velocity that reaches target after flightTime seconds under constant acceleration.</summary>
    public static Vector3 LaunchVelocity(Vector3 origin, Vector3 target, Vector3 acceleration, float flightTime)
    {
        if (!float.IsFinite(flightTime) || flightTime <= 0) throw new ArgumentOutOfRangeException(nameof(flightTime));
        float Axis(float start, float end, float a) => (float)(((double)end - start) / flightTime - a * (0.5d * flightTime));
        Vector3 velocity = new(Axis(origin.X, target.X, acceleration.X), Axis(origin.Y, target.Y, acceleration.Y),
            Axis(origin.Z, target.Z, acceleration.Z));
        Vector3.ValidateFinite(velocity, nameof(velocity));
        return velocity;
    }

    /// <summary>Earliest straight-line intercept by a constant-speed projectile in world space.</summary>
    public static bool TryIntercept(Vector3 origin, float speed, Vector3 target, Vector3 targetVelocity,
        out float time, out Vector3 point)
    {
        time = 0;
        point = default;
        if (!float.IsFinite(speed) || speed <= 0 || !Vector3.IsFinite(origin) ||
            !Vector3.IsFinite(target) || !Vector3.IsFinite(targetVelocity)) return false;
        double x = (double)target.X - origin.X, y = (double)target.Y - origin.Y, z = (double)target.Z - origin.Z;
        double vx = targetVelocity.X, vy = targetVelocity.Y, vz = targetVelocity.Z;
        double a = vx * vx + vy * vy + vz * vz - (double)speed * speed;
        double b = 2 * (x * vx + y * vy + z * vz);
        double c = x * x + y * y + z * z;
        if (c == 0) { point = target; return true; }
        double t;
        if (a == 0)
        {
            if (b >= 0) return false;
            t = -c / b;
        }
        else
        {
            double discriminant = b * b - 4 * a * c;
            if (discriminant < 0) return false;
            double q = -0.5 * (b + Math.CopySign(Math.Sqrt(discriminant), b));
            double first = q / a, second = c / q;
            t = Math.Min(first >= 0 ? first : double.PositiveInfinity, second >= 0 ? second : double.PositiveInfinity);
        }
        if (!double.IsFinite(t) || t > float.MaxValue) return false;
        Vector3 hit = new((float)(target.X + vx * t), (float)(target.Y + vy * t), (float)(target.Z + vz * t));
        if (!Vector3.IsFinite(hit)) return false;
        time = (float)t;
        point = hit;
        return true;
    }

    /// <summary>In a live 3D physics scene, query only layers that should obstruct sight.</summary>
    public static bool HasClearLine(Vector3 origin, Vector3 target, PhysicsLayer obstacles, bool includeTriggers = false)
    {
        Vector3.ValidateFinite(origin, nameof(origin));
        Vector3.ValidateFinite(target, nameof(target));
        Vector3 direction = target - origin;
        float distance = direction.Length();
        if (!float.IsFinite(distance)) throw new ArgumentOutOfRangeException(nameof(target));
        return distance == 0 || !Physics3D.Raycast(origin, direction / distance, out _, obstacles, distance, includeTriggers);
    }
}
