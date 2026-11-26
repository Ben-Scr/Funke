using System;
using System.Runtime.InteropServices;

namespace Funke;
/// <summary>
/// 3D float vector. Used for compatibility and future 3D features.
/// Layout matches glm::vec3 for direct interop.
/// </summary>
[StructLayout(LayoutKind.Explicit, Size = 12)]
public struct Vector3 : IEquatable<Vector3>, ISpanFormattable
{
    [FieldOffset(0)] public float X;
    [FieldOffset(4)] public float Y;
    [FieldOffset(8)] public float Z;

    public static readonly Vector3 Zero = new(0.0f, 0.0f, 0.0f);
    public static readonly Vector3 One = new(1.0f, 1.0f, 1.0f);
    public static readonly Vector3 Right = new(1.0f, 0.0f, 0.0f);
    public static readonly Vector3 Left = new(-1.0f, 0.0f, 0.0f);
    public static readonly Vector3 Up = new(0.0f, 1.0f, 0.0f);
    public static readonly Vector3 Down = new(0.0f, -1.0f, 0.0f);
    public static readonly Vector3 Forward = new(0.0f, 0.0f, -1.0f);
    public static readonly Vector3 Back = new(0.0f, 0.0f, 1.0f);

    public Vector3(float scalar) { X = scalar; Y = scalar; Z = scalar; }
    public Vector3(float x, float y, float z) { X = x; Y = y; Z = z; }
    public Vector3(Vector2 v, float z = 0.0f) { X = v.X; Y = v.Y; Z = z; }

    public float Length() => Mathf.Sqrt(X * X + Y * Y + Z * Z);
    public float LengthSquared() => X * X + Y * Y + Z * Z;

    public Vector3 Normalized()
    {
        float len = Length();
        return len > Mathf.Epsilon ? new Vector3(X / len, Y / len, Z / len) : Zero;
    }

    public void Normalize()
    {
        float len = Length();
        if (len > Mathf.Epsilon) { X /= len; Y /= len; Z /= len; }
    }

    public static float Dot(Vector3 a, Vector3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;

    public static Vector3 Cross(Vector3 a, Vector3 b) => new(
        a.Y * b.Z - a.Z * b.Y,
        a.Z * b.X - a.X * b.Z,
        a.X * b.Y - a.Y * b.X
    );

    public static float Distance(Vector3 a, Vector3 b) => (a - b).Length();

    public static float DistanceSquared(Vector3 a, Vector3 b) => (a - b).LengthSquared();
    public static Vector3 ClosestPointOnSegment(Vector3 point, Vector3 start, Vector3 end)
    {
        ValidateFinite(point, nameof(point));
        ValidateFinite(start, nameof(start));
        ValidateFinite(end, nameof(end));
        double x = (double)end.X - start.X, y = (double)end.Y - start.Y, z = (double)end.Z - start.Z;
        double lengthSquared = x * x + y * y + z * z;
        if (lengthSquared == 0) return start;
        double dot = ((double)point.X - start.X) * x + ((double)point.Y - start.Y) * y + ((double)point.Z - start.Z) * z;
        double t = Math.Clamp(dot / lengthSquared, 0, 1);
        return new((float)(start.X + t * x), (float)(start.Y + t * y), (float)(start.Z + t * z));
    }
    public static Vector3 LerpUnclamped(Vector3 a, Vector3 b, float t) =>
        new(float.Lerp(a.X, b.X, t), float.Lerp(a.Y, b.Y, t), float.Lerp(a.Z, b.Z, t));

    public static bool TryNormalize(Vector3 input, out Vector3 result)
    {
        double length = Math.Sqrt((double)input.X * input.X + (double)input.Y * input.Y + (double)input.Z * input.Z);
        if (length > 0 && double.IsFinite(length))
        {
            result = new((float)(input.X / length), (float)(input.Y / length), (float)(input.Z / length));
            return true;
        }
        result = Zero;
        return false;
    }

    public static Vector3 Project(Vector3 vector, Vector3 normal)
    {
        ValidateFinite(vector, nameof(vector));
        ValidateFinite(normal, nameof(normal));
        if (!TryNormalize(normal, out normal)) return Zero;
        double dot = (double)vector.X * normal.X + (double)vector.Y * normal.Y + (double)vector.Z * normal.Z;
        return new((float)(normal.X * dot), (float)(normal.Y * dot), (float)(normal.Z * dot));
    }

    public static Vector3 ProjectOnPlane(Vector3 vector, Vector3 normal) => vector - Project(vector, normal);
    public static Vector3 Reflect(Vector3 vector, Vector3 normal) => vector - 2 * Project(vector, normal);

    public static float Angle(Vector3 from, Vector3 to)
    {
        if (!TryNormalize(from, out from) || !TryNormalize(to, out to)) return 0;
        return MathF.Atan2(Cross(from, to).Length(), Dot(from, to)) * Mathf.Rad2Deg;
    }

    public static float SignedAngle(Vector3 from, Vector3 to, Vector3 axis)
    {
        if (!TryNormalize(axis, out axis) || !TryNormalize(from, out from) || !TryNormalize(to, out to)) return 0;
        return Dot(axis, Cross(from, to)) < 0 ? -Angle(from, to) : Angle(from, to);
    }

    public static Vector3 Damp(Vector3 current, Vector3 target, float rate, float deltaTime) =>
        LerpUnclamped(current, target, Mathf.DampingFactor(rate, deltaTime));

    /// <summary>Moves at most maxDelta world units without overshooting; maxDelta must be finite and nonnegative.</summary>
    public static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDelta)
    {
        ValidateFinite(current, nameof(current));
        ValidateFinite(target, nameof(target));
        if (!float.IsFinite(maxDelta) || maxDelta < 0) throw new ArgumentOutOfRangeException(nameof(maxDelta));
        double x = (double)target.X - current.X, y = (double)target.Y - current.Y, z = (double)target.Z - current.Z;
        double distance = Math.Sqrt(x * x + y * y + z * z);
        if (distance <= maxDelta) return target;
        double scale = maxDelta / distance;
        return new((float)(current.X + x * scale), (float)(current.Y + y * scale), (float)(current.Z + z * scale));
    }

    /// <summary>Preserves direction and shorter vectors; maxLength must be finite and nonnegative.</summary>
    public static Vector3 ClampMagnitude(Vector3 vector, float maxLength)
    {
        ValidateFinite(vector, nameof(vector));
        if (!float.IsFinite(maxLength) || maxLength < 0) throw new ArgumentOutOfRangeException(nameof(maxLength));
        double length = Math.Sqrt((double)vector.X * vector.X + (double)vector.Y * vector.Y + (double)vector.Z * vector.Z);
        if (length <= maxLength) return vector;
        double scale = maxLength / length;
        return new((float)(vector.X * scale), (float)(vector.Y * scale), (float)(vector.Z * scale));
    }

    /// <summary>Critically damped motion in seconds; velocity persists between calls, deltaTime 0 preserves state, maxSpeed limits the tracking offset.</summary>
    public static Vector3 SmoothDamp(Vector3 current, Vector3 target, ref Vector3 velocity,
        float smoothTime, float deltaTime, float maxSpeed = float.PositiveInfinity)
    {
        ValidateFinite(current, nameof(current));
        ValidateFinite(target, nameof(target));
        ValidateFinite(velocity, nameof(velocity));
        if (!float.IsFinite(smoothTime) || smoothTime < 0) throw new ArgumentOutOfRangeException(nameof(smoothTime));
        if (!float.IsFinite(deltaTime) || deltaTime < 0) throw new ArgumentOutOfRangeException(nameof(deltaTime));
        if (float.IsNaN(maxSpeed) || maxSpeed < 0) throw new ArgumentOutOfRangeException(nameof(maxSpeed));
        if (deltaTime == 0) return current;

        double time = Math.Max(smoothTime, 0.0001), omega = 2 / time;
        double decay = Math.Exp(-omega * deltaTime);
        double x = (double)current.X - target.X, y = (double)current.Y - target.Y, z = (double)current.Z - target.Z;
        double length = Math.Sqrt(x * x + y * y + z * z), limit = maxSpeed * time;
        if (length > limit) { double scale = limit / length; x *= scale; y *= scale; z *= scale; }
        double tx = (velocity.X + omega * x) * deltaTime;
        double ty = (velocity.Y + omega * y) * deltaTime;
        double tz = (velocity.Z + omega * z) * deltaTime;
        double rx = current.X - x + (x + tx) * decay;
        double ry = current.Y - y + (y + ty) * decay;
        double rz = current.Z - z + (z + tz) * decay;
        Vector3 nextVelocity = new((float)((velocity.X - omega * tx) * decay),
            (float)((velocity.Y - omega * ty) * decay), (float)((velocity.Z - omega * tz) * decay));
        Vector3 result = new((float)rx, (float)ry, (float)rz);
        if (((double)target.X - current.X) * (rx - target.X)
            + ((double)target.Y - current.Y) * (ry - target.Y)
            + ((double)target.Z - current.Z) * (rz - target.Z) > 0)
        {
            result = target;
            nextVelocity = Zero;
        }
        ValidateFinite(result, nameof(result));
        ValidateFinite(nextVelocity, nameof(velocity));
        velocity = nextVelocity;
        return result;
    }

    internal static bool IsFinite(Vector3 value) => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    internal static void ValidateFinite(Vector3 value, string name)
    {
        if (!IsFinite(value)) throw new ArgumentOutOfRangeException(name);
    }

    public static Vector3 Lerp(Vector3 a, Vector3 b, float t)
    {
        t = Mathf.Clamp01(t);
        return new Vector3(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t, a.Z + (b.Z - a.Z) * t);
    }

    public Vector2 XY => new(X, Y);

    // Operators
    public static Vector3 operator +(Vector3 a, Vector3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static Vector3 operator -(Vector3 a, Vector3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static Vector3 operator *(Vector3 a, Vector3 b) => new(a.X * b.X, a.Y * b.Y, a.Z * b.Z);
    public static Vector3 operator *(Vector3 v, float s) => new(v.X * s, v.Y * s, v.Z * s);
    public static Vector3 operator *(float s, Vector3 v) => new(s * v.X, s * v.Y, s * v.Z);
    public static Vector3 operator /(Vector3 v, float s) => new(v.X / s, v.Y / s, v.Z / s);
    public static Vector3 operator -(Vector3 v) => new(-v.X, -v.Y, -v.Z);

    public static bool operator ==(Vector3 a, Vector3 b) => a.Equals(b);
    public static bool operator !=(Vector3 a, Vector3 b) => !a.Equals(b);

    public bool Equals(Vector3 other) => X == other.X && Y == other.Y && Z == other.Z;
    public override bool Equals(object? obj) => obj is Vector3 other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z);
    public override string ToString() => ToString(null, null);

    public string ToString(string? format, IFormatProvider? formatProvider = null) =>
        $"Vector3({X.ToString(format, formatProvider)}, {Y.ToString(format, formatProvider)}, {Z.ToString(format, formatProvider)})";

    public bool TryFormat(Span<char> destination, out int charsWritten, ReadOnlySpan<char> format, IFormatProvider? provider)
    {
        charsWritten = 0;
        int pos = 0;
        if (!TryAppend(destination, ref pos, "Vector3(")) return false;
        if (!X.TryFormat(destination[pos..], out int w, format, provider)) return false;
        pos += w;
        if (!TryAppend(destination, ref pos, ", ")) return false;
        if (!Y.TryFormat(destination[pos..], out w, format, provider)) return false;
        pos += w;
        if (!TryAppend(destination, ref pos, ", ")) return false;
        if (!Z.TryFormat(destination[pos..], out w, format, provider)) return false;
        pos += w;
        if (!TryAppend(destination, ref pos, ")")) return false;
        charsWritten = pos;
        return true;

        static bool TryAppend(Span<char> dest, ref int pos, ReadOnlySpan<char> text)
        {
            if (!text.TryCopyTo(dest[pos..])) return false;
            pos += text.Length;
            return true;
        }
    }

    // ── Conversions ─────────────────────────────────────────────
    public static explicit operator Vector2(Vector3 v) => new(v.X, v.Y);
    public static implicit operator Vector4(Vector3 v) => new(v.X, v.Y, v.Z, 0f);
    public static explicit operator Vector3(Vector4 v) => new(v.X, v.Y, v.Z);
}
