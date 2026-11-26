using System;
using System.Runtime.InteropServices;
using NumericsQuaternion = System.Numerics.Quaternion;
using Matrix4x4 = System.Numerics.Matrix4x4;

namespace Funke;

/// <summary>Right-handed rotation, XYZW layout, -Z forward. Rotation operations normalize; zero or nonfinite inputs become Identity.</summary>
[StructLayout(LayoutKind.Sequential, Size = 16)]
public struct Quaternion : IEquatable<Quaternion>
{
    public float X, Y, Z, W;

    public static readonly Quaternion Identity = new(0, 0, 0, 1);

    internal static bool IsValidRotation(Quaternion value) =>
        float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z) && float.IsFinite(value.W)
        && (value.X != 0 || value.Y != 0 || value.Z != 0 || value.W != 0);

    public Quaternion(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; }

    public Quaternion Normalized()
    {
        double length = Math.Sqrt((double)X * X + (double)Y * Y + (double)Z * Z + (double)W * W);
        return length > 0 && double.IsFinite(length)
            ? new((float)(X / length), (float)(Y / length), (float)(Z / length), (float)(W / length)) : Identity;
    }

    public void Normalize() => this = Normalized();
    public static Quaternion Normalize(Quaternion rotation) => rotation.Normalized();

    /// <summary>Angle in degrees; a zero or nonfinite axis/angle produces Identity.</summary>
    public static Quaternion AngleAxis(float degrees, Vector3 axis)
    {
        if (!float.IsFinite(degrees) || !Vector3.TryNormalize(axis, out axis)) return Identity;
        float half = degrees * (MathF.PI / 360f);
        float sine = MathF.Sin(half);
        return new Quaternion(axis.X * sine, axis.Y * sine, axis.Z * sine, MathF.Cos(half)).Normalized();
    }

    /// <summary>Degrees, X pitch / Y yaw / Z roll; applies X then Y then Z, matching glm::quat(radians(euler)).</summary>
    public static Quaternion Euler(Vector3 degrees)
    {
        if (!float.IsFinite(degrees.X) || !float.IsFinite(degrees.Y) || !float.IsFinite(degrees.Z)) return Identity;
        float x = degrees.X * (MathF.PI / 360f), y = degrees.Y * (MathF.PI / 360f), z = degrees.Z * (MathF.PI / 360f);
        float cx = MathF.Cos(x), cy = MathF.Cos(y), cz = MathF.Cos(z);
        float sx = MathF.Sin(x), sy = MathF.Sin(y), sz = MathF.Sin(z);
        return new Quaternion(sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz).Normalized();
    }

    public static Quaternion Euler(float x, float y, float z) => Euler(new Vector3(x, y, z));

    /// <summary>Euler degrees compatible with Transform3D.Rotation; equivalent rotations need not return the original angles.</summary>
    public Vector3 EulerAngles
    {
        get
        {
            Quaternion q = Normalized();
            float pitchY = 2 * (q.Y * q.Z + q.W * q.X), pitchX = q.W * q.W - q.X * q.X - q.Y * q.Y + q.Z * q.Z;
            float pitch = MathF.Abs(pitchX) < 1e-7f && MathF.Abs(pitchY) < 1e-7f
                ? 2 * MathF.Atan2(q.X, q.W) : MathF.Atan2(pitchY, pitchX);
            double normSquared = (double)q.X * q.X + (double)q.Y * q.Y + (double)q.Z * q.Z + (double)q.W * q.W;
            float yaw = (float)Math.Asin(Math.Clamp(2 * ((double)q.W * q.Y - (double)q.X * q.Z) / normSquared, -1d, 1d));
            float rollY = 2 * (q.X * q.Y + q.W * q.Z), rollX = q.W * q.W + q.X * q.X - q.Y * q.Y - q.Z * q.Z;
            float roll = MathF.Abs(rollX) < 1e-7f && MathF.Abs(rollY) < 1e-7f ? 0 : MathF.Atan2(rollY, rollX);
            return new Vector3(pitch, yaw, roll) * (180f / MathF.PI);
        }
    }

    /// <summary>Aligns local -Z with forward; degenerate up chooses a perpendicular axis, invalid forward returns Identity.</summary>
    public static Quaternion LookRotation(Vector3 forward, Vector3 up)
    {
        if (!Vector3.TryNormalize(forward, out forward)) return Identity;
        Vector3.TryNormalize(up, out up);
        Vector3 right = Vector3.Cross(forward, up);
        if (right.LengthSquared() < 1e-12f)
            right = Vector3.Cross(forward, MathF.Abs(forward.Y) < 0.999f ? Vector3.Up : Vector3.Right);
        Vector3.TryNormalize(right, out right);
        up = Vector3.Cross(right, forward);
        return FromNumerics(NumericsQuaternion.CreateFromRotationMatrix(new Matrix4x4(
            right.X, right.Y, right.Z, 0, up.X, up.Y, up.Z, 0,
            -forward.X, -forward.Y, -forward.Z, 0, 0, 0, 0, 1))).Normalized();
    }

    public static Quaternion LookRotation(Vector3 forward) => LookRotation(forward, Vector3.Up);

    public static Quaternion Inverse(Quaternion rotation)
    {
        Quaternion q = rotation.Normalized();
        return new(-q.X, -q.Y, -q.Z, q.W);
    }

    /// <summary>Shortest rotation arc, t clamped to 0..1; nonfinite t selects from.</summary>
    public static Quaternion Slerp(Quaternion from, Quaternion to, float t) =>
        FromNumerics(NumericsQuaternion.Slerp(from.Normalized().ToNumerics(), to.Normalized().ToNumerics(),
            float.IsFinite(t) ? Math.Clamp(t, 0f, 1f) : 0f)).Normalized();

    public Vector3 RotateVector(Vector3 vector)
    {
        var result = System.Numerics.Vector3.Transform(new(vector.X, vector.Y, vector.Z), Normalized().ToNumerics());
        return new(result.X, result.Y, result.Z);
    }

    /// <summary>lhs * rhs applies rhs first, then lhs.</summary>
    public static Quaternion operator *(Quaternion lhs, Quaternion rhs) =>
        FromNumerics(NumericsQuaternion.Multiply(lhs.Normalized().ToNumerics(), rhs.Normalized().ToNumerics())).Normalized();
    public static Vector3 operator *(Quaternion rotation, Vector3 vector) => rotation.RotateVector(vector);
    public static bool operator ==(Quaternion lhs, Quaternion rhs) => lhs.Equals(rhs);
    public static bool operator !=(Quaternion lhs, Quaternion rhs) => !lhs.Equals(rhs);
    public bool Equals(Quaternion other) => X == other.X && Y == other.Y && Z == other.Z && W == other.W;
    public override bool Equals(object? obj) => obj is Quaternion other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(X, Y, Z, W);
    public override string ToString() => $"Quaternion({X}, {Y}, {Z}, {W})";

    private NumericsQuaternion ToNumerics() => new(X, Y, Z, W);
    private static Quaternion FromNumerics(NumericsQuaternion q) => new(q.X, q.Y, q.Z, q.W);
    public static bool TryLookRotation(Vector3 forward, Vector3 up, out Quaternion rotation)
    {
        rotation = Identity;
        if (!Vector3.TryNormalize(forward, out forward) || !Vector3.IsFinite(up)) return false;
        rotation = LookRotation(forward, up);
        return true;
    }

    public static Quaternion FromToRotation(Vector3 from, Vector3 to)
    {
        if (!Vector3.TryNormalize(from, out from) || !Vector3.TryNormalize(to, out to)) return Identity;
        Vector3 axis = Vector3.Cross(from, to);
        float dot = Vector3.Dot(from, to);
        if (Vector3.TryNormalize(axis, out Vector3 unit))
            return AngleAxis(MathF.Atan2(axis.Length(), dot) * Mathf.Rad2Deg, unit);
        if (dot >= 0) return Identity;
        return AngleAxis(180, Vector3.Cross(from, MathF.Abs(from.Y) < 0.9f ? Vector3.Up : Vector3.Right));
    }

    public static float Angle(Quaternion from, Quaternion to)
    {
        Quaternion delta = Inverse(from) * to;
        double length = Math.Sqrt((double)delta.X * delta.X + (double)delta.Y * delta.Y + (double)delta.Z * delta.Z);
        return (float)(2 * Math.Atan2(length, Math.Abs(delta.W)) * Mathf.Rad2Deg);
    }

    public static Quaternion RotateTowards(Quaternion from, Quaternion to, float maxDegreesDelta)
    {
        if (!float.IsFinite(maxDegreesDelta) || maxDegreesDelta < 0) throw new ArgumentOutOfRangeException(nameof(maxDegreesDelta));
        float angle = Angle(from, to);
        return angle <= maxDegreesDelta ? to.Normalized() : Slerp(from, to, maxDegreesDelta / angle);
    }

    public static Quaternion Damp(Quaternion current, Quaternion target, float rate, float deltaTime) =>
        Slerp(current, target, Mathf.DampingFactor(rate, deltaTime));
}

