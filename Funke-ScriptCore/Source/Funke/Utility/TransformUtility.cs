using Matrix4x4 = System.Numerics.Matrix4x4;
using NumericsVector3 = System.Numerics.Vector3;
using NumericsQuaternion = System.Numerics.Quaternion;

namespace Funke;

public enum Space
{
    Local,
    World,
}

public static class TransformUtility
{
    /// <summary>Uses current local transforms and ancestors, unlike the cached Transform3D.WorldPosition property; engine thread only.</summary>
    public static Vector3 GetWorldPosition(this Transform3D transform) => transform.TransformPoint(Vector3.Zero);

    public static (Vector3 Position, Quaternion Rotation) GetWorldPose(this Transform3D transform) =>
        (transform.GetWorldPosition(), transform.WorldRotation);

    public static bool TrySetWorldPose(this Transform3D transform, Vector3 position, Quaternion rotation)
    {
        ArgumentNullException.ThrowIfNull(transform);
        if (!Vector3.IsFinite(position) || !Quaternion.IsValidRotation(rotation) ||
            transform.Entity is null || !transform.Entity.HasComponent<Transform3D>()) return false;
        Matrix4x4 parent = WorldMatrix(transform, includeSelf: false);
        if (!Matrix4x4.Invert(parent, out Matrix4x4 inverse)) return false;
        Vector3 local = ApplyMatrix(position, inverse, point: true);
        if (!Vector3.IsFinite(local)) return false;
        transform.SetLocalPose(local, Quaternion.Inverse(transform.ParentRotation()) * rotation);
        return true;
    }

    public static void SetWorldPosition(this Transform3D transform, Vector3 position)
    {
        if (!transform.TrySetWorldPosition(position)) throw new InvalidOperationException("Parent transform cannot represent this world position.");
    }

    /// <summary>Leaves the local position unchanged when the parent matrix is singular or the result is not finite.</summary>
    public static bool TrySetWorldPosition(this Transform3D transform, Vector3 position)
    {
        Vector3.ValidateFinite(position, nameof(position));
        Matrix4x4 parent = WorldMatrix(transform, includeSelf: false);
        if (!Matrix4x4.Invert(parent, out Matrix4x4 inverse)) return false;
        Vector3 local = ApplyMatrix(position, inverse, point: true);
        if (!Vector3.IsFinite(local)) return false;
        transform.Position = local;
        return true;
    }

    /// <summary>Current local-to-world conversion including translation, scale and parent shear; engine thread only.</summary>
    public static Vector3 TransformPoint(this Transform3D transform, Vector3 point)
        => Convert(transform, point, inverse: false, point: true);

    /// <summary>Throws if the current world matrix is singular; includes translation and scale.</summary>
    public static Vector3 InverseTransformPoint(this Transform3D transform, Vector3 point)
        => Convert(transform, point, inverse: true, point: true);

    /// <summary>Includes scale and parent shear, excluding translation.</summary>
    public static Vector3 TransformVector(this Transform3D transform, Vector3 vector)
        => Convert(transform, vector, inverse: false, point: false);

    public static Vector3 InverseTransformVector(this Transform3D transform, Vector3 vector)
        => Convert(transform, vector, inverse: true, point: false);

    /// <summary>Uses current quaternion orientation only; ignores scale, reflection and shear and preserves length.</summary>
    public static Vector3 TransformDirection(this Transform3D transform, Vector3 direction)
    {
        ArgumentNullException.ThrowIfNull(transform);
        Vector3.ValidateFinite(direction, nameof(direction));
        Vector3 result = transform.WorldRotation * direction;
        Vector3.ValidateFinite(result, nameof(direction));
        return result;
    }

    public static Vector3 InverseTransformDirection(this Transform3D transform, Vector3 direction)
    {
        ArgumentNullException.ThrowIfNull(transform);
        Vector3.ValidateFinite(direction, nameof(direction));
        Vector3 result = Quaternion.Inverse(transform.WorldRotation) * direction;
        Vector3.ValidateFinite(result, nameof(direction));
        return result;
    }

    /// <summary>Displacement is in world units; Local rotates its axes without scaling. Does not perform physics movement.</summary>
    public static void Translate(this Transform3D transform, Vector3 displacement, Space space = Space.Local)
    {
        ValidateSpace(space);
        Vector3.ValidateFinite(displacement, nameof(displacement));
        Vector3 offset = space == Space.Local ? transform.TransformDirection(displacement) : displacement;
        transform.SetWorldPosition(transform.GetWorldPosition() + offset);
    }

    /// <summary>Euler delta in degrees, composed as a quaternion; Local applies before the current rotation, World after it.</summary>
    public static void Rotate(this Transform3D transform, Vector3 eulerDegrees, Space space = Space.Local)
    {
        Vector3.ValidateFinite(eulerDegrees, nameof(eulerDegrees));
        Rotate(transform, Quaternion.Euler(eulerDegrees), space);
    }

    public static void Rotate(this Transform3D transform, Vector3 axis, float degrees, Space space = Space.Local)
    {
        ValidateAxisAngle(axis, degrees);
        Rotate(transform, Quaternion.AngleAxis(degrees, axis), space);
    }

    /// <summary>Rotates position and orientation around a world-space pivot/axis; degrees are explicit and no deltaTime is applied.</summary>
    public static void RotateAround(this Transform3D transform, Vector3 pivot, Vector3 axis, float degrees)
    {
        Vector3.ValidateFinite(pivot, nameof(pivot));
        ValidateAxisAngle(axis, degrees);
        Quaternion delta = Quaternion.AngleAxis(degrees, axis);
        Vector3 position = pivot + delta * (transform.GetWorldPosition() - pivot);
        Quaternion rotation = delta * transform.WorldRotation;
        transform.SetWorldPosition(position);
        transform.WorldRotation = rotation;
    }

    /// <summary>Returns a world rotation aligning local -Z with the target; coincident points preserve current orientation.</summary>
    public static Quaternion LookAt3D(Transform3D origin, Vector3 target, Vector3? up = null)
    {
        Vector3.ValidateFinite(target, nameof(target));
        Vector3 worldUp = up ?? Vector3.Up;
        Vector3.ValidateFinite(worldUp, nameof(up));
        Vector3 direction = target - origin.GetWorldPosition();
        Vector3.ValidateFinite(direction, nameof(target));
        return direction.LengthSquared() <= Mathf.Epsilon * Mathf.Epsilon
            ? origin.WorldRotation : Quaternion.LookRotation(direction, worldUp);
    }

    public static void Face3D(Transform3D origin, Vector3 target, Vector3? up = null)
        => origin.WorldRotation = LookAt3D(origin, target, up);

    public static bool TryLookAt3D(Vector3 originWorld, Vector3 targetWorld, Vector3 up, out Quaternion rotation)
        => Quaternion.TryLookRotation(targetWorld - originWorld, up, out rotation);

    public static Vector3 DirectionTo3D(Vector3 origin, Vector3 target)
        => Vector3.TryNormalize(target - origin, out Vector3 direction) ? direction : Vector3.Zero;

    public static Vector3 RotatePoint3D(Vector3 point, Vector3 pivot, Vector3 axis, float degrees)
    {
        ValidateAxisAngle(axis, degrees);
        Vector3.ValidateFinite(point, nameof(point));
        Vector3.ValidateFinite(pivot, nameof(pivot));
        return pivot + Quaternion.AngleAxis(degrees, axis) * (point - pivot);
    }

    public static void FaceTowards3D(this Transform3D origin, Vector3 target, float maxDegreesPerSecond,
        float deltaTime, Vector3? up = null)
    {
        if (!float.IsFinite(maxDegreesPerSecond) || maxDegreesPerSecond < 0) throw new ArgumentOutOfRangeException(nameof(maxDegreesPerSecond));
        if (!float.IsFinite(deltaTime) || deltaTime < 0) throw new ArgumentOutOfRangeException(nameof(deltaTime));
        float step = (float)Math.Min(180, (double)maxDegreesPerSecond * deltaTime);
        if (step > 0) origin.WorldRotation = Quaternion.RotateTowards(origin.WorldRotation, LookAt3D(origin, target, up), step);
    }

    public static bool FaceOnPlane3D(this Transform3D origin, Vector3 target, Vector3 normal)
    {
        if (!Vector3.TryNormalize(normal, out normal)) return false;
        Vector3 direction = Vector3.ProjectOnPlane(target - origin.GetWorldPosition(), normal);
        if (direction.LengthSquared() <= Mathf.Epsilon * Mathf.Epsilon ||
            !Quaternion.TryLookRotation(direction, normal, out Quaternion rotation)) return false;
        origin.WorldRotation = rotation;
        return true;
    }

    public static Vector3 TransformNormal(this Transform3D transform, Vector3 normal)
    {
        Vector3.ValidateFinite(normal, nameof(normal));
        if (!Matrix4x4.Invert(WorldMatrix(transform, includeSelf: true), out Matrix4x4 inverse))
            throw new InvalidOperationException("World transform is singular.");
        Vector3 result = ApplyMatrix(normal, Matrix4x4.Transpose(inverse), point: false);
        if (!Vector3.TryNormalize(result, out result)) throw new ArgumentException("Surface normal has no finite direction.", nameof(normal));
        return result;
    }

    private static void Rotate(Transform3D transform, Quaternion delta, Space space)
    {
        ArgumentNullException.ThrowIfNull(transform);
        ValidateSpace(space);
        if (space == Space.Local) transform.LocalRotation = transform.LocalRotation * delta;
        else transform.WorldRotation = delta * transform.WorldRotation;
    }

    private static void ValidateSpace(Space space)
    {
        if (space != Space.Local && space != Space.World) throw new ArgumentOutOfRangeException(nameof(space));
    }

    private static void ValidateAxisAngle(Vector3 axis, float degrees)
    {
        Vector3.ValidateFinite(axis, nameof(axis));
        if (axis == Vector3.Zero) throw new ArgumentOutOfRangeException(nameof(axis));
        if (!float.IsFinite(degrees)) throw new ArgumentOutOfRangeException(nameof(degrees));
    }

    private static Vector3 Convert(Transform3D transform, Vector3 value, bool inverse, bool point)
    {
        Vector3.ValidateFinite(value, nameof(value));
        Matrix4x4 matrix = WorldMatrix(transform, includeSelf: true);
        if (inverse && !Matrix4x4.Invert(matrix, out matrix)) throw new InvalidOperationException("World transform is singular.");
        Vector3 result = ApplyMatrix(value, matrix, point);
        if (!Vector3.IsFinite(result)) throw new InvalidOperationException("Transform result is not finite.");
        return result;
    }

    private static Vector3 ApplyMatrix(Vector3 value, Matrix4x4 matrix, bool point)
    {
        NumericsVector3 input = new(value.X, value.Y, value.Z);
        NumericsVector3 result = point ? NumericsVector3.Transform(input, matrix) : NumericsVector3.TransformNormal(input, matrix);
        return new(result.X, result.Y, result.Z);
    }

    private static Matrix4x4 WorldMatrix(Transform3D transform, bool includeSelf)
    {
        ArgumentNullException.ThrowIfNull(transform);
        Matrix4x4 local = LocalMatrix(transform);
        Matrix4x4 matrix = includeSelf ? local : Matrix4x4.Identity;
        Entity? parent = transform.Entity.Parent;
        int depth = 0;
        while (parent != null && parent != Entity.Invalid)
        {
            if (++depth > 1024) throw new InvalidOperationException("Transform hierarchy exceeds 1024 ancestors or contains a cycle.");
            if (parent.GetComponent<Transform3D>() is { } ancestor) matrix *= LocalMatrix(ancestor);
            parent = parent.Parent;
        }
        return matrix;
    }

    private static Matrix4x4 LocalMatrix(Transform3D transform)
    {
        Vector3 position = transform.Position, scale = transform.Scale, angles = transform.Rotation;
        Vector3.ValidateFinite(position, nameof(transform));
        Vector3.ValidateFinite(scale, nameof(transform));
        Vector3.ValidateFinite(angles, nameof(transform));
        Quaternion q = Quaternion.Euler(angles);
        // System.Numerics uses row vectors; S * R * T is the transpose of native GLM's T * R * S.
        return Matrix4x4.CreateScale(scale.X, scale.Y, scale.Z)
            * Matrix4x4.CreateFromQuaternion(new NumericsQuaternion(q.X, q.Y, q.Z, q.W))
            * Matrix4x4.CreateTranslation(position.X, position.Y, position.Z);
    }

    public static float LookAt2D(Transform2D origin, Vector2 target, float lerp)
    {
        if (origin == null) throw new ArgumentNullException(nameof(origin));

        if (!TryLookAt2DRadians(origin.Position, target, out float targetRotation))
            return origin.RotationDegrees;

        return LerpAngleDegrees(origin.RotationDegrees, targetRotation * Mathf.Rad2Deg, lerp);
    }

    public static float LookAt2D(Transform2D origin, Vector2 target)
    {
        if (origin == null) throw new ArgumentNullException(nameof(origin));

        if (!TryLookAt2DRadians(origin.Position, target, out float targetRotation))
            return origin.RotationDegrees;

        return targetRotation * Mathf.Rad2Deg;
    }

    public static float LookAt2DRadians(Transform2D origin, Vector2 target, float lerp = 1.0f)
    {
        if (origin == null) throw new ArgumentNullException(nameof(origin));

        if (!TryLookAt2DRadians(origin.Position, target, out float targetRotation))
            return origin.Rotation * Mathf.Deg2Rad;

        return LerpAngleRadians(origin.Rotation * Mathf.Deg2Rad, targetRotation, lerp);
    }



    /// <summary>
    /// Calculates the rotation angle (in radians) for <paramref name="origin"/>'s right axis to face <paramref name="target"/>.
    /// </summary>
    public static float LookAtRight2DRadians(Vector2 origin, Vector2 target)
    {
        return TryLookAtRight2DRadians(origin, target, out float rotation) ? rotation : 0.0f;
    }

    /// <summary>
    /// Rotates <paramref name="origin"/> so its up axis (visual forward) faces <paramref name="target"/>.
    /// </summary>
    public static void Face2D(Transform2D origin, Vector2 target, float lerp = 1.0f)
    {
        if (origin == null) throw new ArgumentNullException(nameof(origin));
        origin.RotationDegrees = LookAt2D(origin, target, lerp);
    }

    /// <summary>
    /// Returns a normalized direction from <paramref name="origin"/> to <paramref name="target"/>.
    /// </summary>
    public static Vector2 DirectionTo2D(Vector2 origin, Vector2 target)
    {
        Vector2 direction = target - origin;
        return direction.LengthSquared() > Mathf.Epsilon * Mathf.Epsilon
            ? direction.Normalized()
            : Vector2.Zero;
    }

    /// <summary>
    /// Rotates <paramref name="point"/> around <paramref name="pivot"/> by <paramref name="degrees"/>.
    /// </summary>
    public static Vector2 RotatePoint2D(Vector2 point, Vector2 pivot, float degrees)
    {
        return RotatePoint2DRadians(point, pivot, degrees * Mathf.Deg2Rad);
    }

    /// <summary>
    /// Rotates <paramref name="point"/> around <paramref name="pivot"/> by <paramref name="radians"/>.
    /// </summary>
    public static Vector2 RotatePoint2DRadians(Vector2 point, Vector2 pivot, float radians)
    {
        float sin = Mathf.Sin(radians);
        float cos = Mathf.Cos(radians);
        Vector2 offset = point - pivot;

        return new Vector2(
            offset.X * cos - offset.Y * sin + pivot.X,
            offset.X * sin + offset.Y * cos + pivot.Y);
    }

    /// <summary>
    /// Interpolates between angles in degrees using the shortest path.
    /// </summary>
    public static float LerpAngleDegrees(float current, float target, float lerp)
    {
        return current + DeltaAngleDegrees(current, target) * Mathf.Clamp01(lerp);
    }

    /// <summary>
    /// Interpolates between angles in radians using the shortest path.
    /// </summary>
    public static float LerpAngleRadians(float current, float target, float lerp)
    {
        return current + DeltaAngleRadians(current, target) * Mathf.Clamp01(lerp);
    }

    /// <summary>
    /// Returns the shortest signed difference from <paramref name="current"/> to <paramref name="target"/> in degrees.
    /// </summary>
    public static float DeltaAngleDegrees(float current, float target)
    {
        float delta = Repeat(target - current, 360f);
        if (delta > 180f) delta -= 360f;
        return delta;
    }

    /// <summary>
    /// Returns the shortest signed difference from <paramref name="current"/> to <paramref name="target"/> in radians.
    /// </summary>
    public static float DeltaAngleRadians(float current, float target)
    {
        float delta = Repeat(target - current, Mathf.TwoPI);
        if (delta > Mathf.PI) delta -= Mathf.TwoPI;
        return delta;
    }

    // Origin's Up axis (-sin θ, cos θ) faces (dx, dy) when sin θ = -dx/|d|, cos θ = dy/|d|
    // i.e. θ = atan2(-dx, dy).
    private static bool TryLookAt2DRadians(Vector2 origin, Vector2 target, out float rotation)
    {
        Vector2 direction = target - origin;
        if (direction.LengthSquared() <= Mathf.Epsilon * Mathf.Epsilon)
        {
            rotation = 0.0f;
            return false;
        }

        rotation = Mathf.Atan2(-direction.X, direction.Y);
        return true;
    }

    // Origin's Right axis (cos θ, sin θ) faces (dx, dy) when θ = atan2(dy, dx).
    private static bool TryLookAtRight2DRadians(Vector2 origin, Vector2 target, out float rotation)
    {
        Vector2 direction = target - origin;
        if (direction.LengthSquared() <= Mathf.Epsilon * Mathf.Epsilon)
        {
            rotation = 0.0f;
            return false;
        }

        rotation = Mathf.Atan2(direction.Y, direction.X);
        return true;
    }

    private static float Repeat(float value, float length)
    {
        return value - Mathf.Floor(value / length) * length;
    }
}
