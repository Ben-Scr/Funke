using System;

namespace Funke;

/// <summary>Explicit camera-follow state. Call once after moving the target, using the final render pose.
/// Does not select a camera, read entities, or interpolate physics. Reset on target switches/teleports.</summary>
public sealed class CameraFollow3D
{
    public Vector3 Offset { get; set; } = new(0, 2, 5);
    /// <summary>Exponential convergence rate per second. Zero snaps; positive values smooth.</summary>
    public float Damping { get; set; } = 8;
    public Vector3 Position { get; private set; }
    public bool IsInitialized { get; private set; }

    /// <summary>Discard history after target destruction/switch or a teleport. Next Step snaps.</summary>
    public void Reset() => IsInitialized = false;

    public Vector3 Step(Vector3 targetPosition, float deltaTime)
    {
        Validate(targetPosition, nameof(targetPosition));
        Validate(Offset, nameof(Offset));
        float blend = Blend(Damping, deltaTime);
        Vector3 desired = targetPosition + Offset;
        Validate(desired, nameof(targetPosition));
        if (!IsInitialized) { Position = desired; IsInitialized = true; }
        else if (blend > 0) Position = new Vector3(
            (float)((double)Position.X * (1 - (double)blend) + (double)desired.X * blend),
            (float)((double)Position.Y * (1 - (double)blend) + (double)desired.Y * blend),
            (float)((double)Position.Z * (1 - (double)blend) + (double)desired.Z * blend));
        return Position;
    }

    /// <summary>Right-handed orbit: yaw/pitch in degrees, positive distance, local -Z looks at target.
    /// Pitch is clamped to +/-89 degrees; offset is replaced by the orbit offset.</summary>
    public Vector3 StepOrbit(Vector3 targetPosition, float yaw, float pitch, float distance, float deltaTime)
    {
        if (!float.IsFinite(yaw)) throw new ArgumentOutOfRangeException(nameof(yaw));
        if (!float.IsFinite(pitch)) throw new ArgumentOutOfRangeException(nameof(pitch));
        if (!float.IsFinite(distance) || distance <= 0) throw new ArgumentOutOfRangeException(nameof(distance));
        Validate(targetPosition, nameof(targetPosition));
        Blend(Damping, deltaTime);
        Offset = Quaternion.Euler(Math.Clamp(pitch, -89, 89), yaw % 360, 0) * (Vector3.Back * distance);
        return Step(targetPosition, deltaTime);
    }

    public Quaternion LookAt(Vector3 targetPosition)
    {
        Validate(targetPosition, nameof(targetPosition));
        return Quaternion.LookRotation(targetPosition - Position);
    }

    internal static float Blend(float damping, float deltaTime)
    {
        if (!float.IsFinite(damping) || damping < 0) throw new ArgumentOutOfRangeException(nameof(damping));
        if (!float.IsFinite(deltaTime) || deltaTime < 0) throw new ArgumentOutOfRangeException(nameof(deltaTime));
        if (deltaTime == 0) return 0; // Pause preserves an initialized pose, also in snap mode.
        return damping == 0 ? 1 : (float)(1 - Math.Exp(-(double)damping * deltaTime));
    }

    internal static void Validate(Vector3 value, string name)
    {
        if (!float.IsFinite(value.X) || !float.IsFinite(value.Y) || !float.IsFinite(value.Z))
            throw new ArgumentOutOfRangeException(name);
    }
}

/// <summary>2D counterpart of CameraFollow3D; world-space position in, world-space position out.</summary>
public sealed class CameraFollow2D
{
    private readonly CameraFollow3D follow = new();
    public Vector2 Offset { get; set; }
    public float Damping { get => follow.Damping; set => follow.Damping = value; }
    public Vector2 Position => follow.Position.XY;
    public bool IsInitialized => follow.IsInitialized;
    public void Reset() => follow.Reset();
    public Vector2 Step(Vector2 targetPosition, float deltaTime)
    {
        follow.Offset = new Vector3(Offset);
        return follow.Step(new Vector3(targetPosition), deltaTime).XY;
    }
}
