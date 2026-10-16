using Index.Interop;

namespace Index.Physics;

public readonly struct RaycastHit3D
{
    internal RaycastHit3D(Entity entity, Vector3 point, Vector3 normal, float distance)
    {
        Entity = entity;
        Point = point;
        Normal = normal;
        Distance = distance;
    }

    /// <summary>The entity whose collider was hit (a child collider reports its own entity, not the Rigidbody3D's).</summary>
    public Entity Entity { get; }
    public Vector3 Point { get; }
    public Vector3 Normal { get; }
    public float Distance { get; }
}

/// <summary>Queries and settings of the active scene's 3D (Jolt) physics world. Only available in play mode.</summary>
public static unsafe class Physics3D
{
    private const int k_HitValueCount = 7;

    public static Vector3 Gravity
    {
        get
        {
            float x, y, z;
            NativeCallbacks.Bindings.Physics3D_GetGravity(&x, &y, &z);
            return new Vector3(x, y, z);
        }
        set => NativeCallbacks.Bindings.Physics3D_SetGravity(value.X, value.Y, value.Z);
    }

    /// <summary>Closest non-trigger collider along the ray.</summary>
    public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit3D hit, float maxDistance = 1000.0f)
    {
        hit = default;
        if (maxDistance <= 0.0f || float.IsNaN(maxDistance) || direction.LengthSquared() <= Mathf.Epsilon * Mathf.Epsilon)
            return false;

        ulong entityId;
        float* values = stackalloc float[k_HitValueCount];
        if (NativeCallbacks.Bindings.Physics3D_Raycast(origin.X, origin.Y, origin.Z, direction.X, direction.Y, direction.Z,
                maxDistance, &entityId, values, k_HitValueCount) == 0)
            return false;

        hit = new RaycastHit3D(new Entity(entityId), new Vector3(values[0], values[1], values[2]),
            new Vector3(values[3], values[4], values[5]), values[6]);
        return true;
    }

    public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance = 1000.0f)
        => Raycast(origin, direction, out _, maxDistance);

    /// <summary>Closest non-trigger collider along the ray.</summary>
    public static bool Raycast(Ray ray, out RaycastHit3D hit, float maxDistance = 1000.0f)
        => Raycast(ray.Origin, ray.Direction, out hit, maxDistance);

    public static bool Raycast(Ray ray, float maxDistance = 1000.0f) => Raycast(ray.Origin, ray.Direction, out _, maxDistance);

    /// <summary>
    /// What is under a screen point (viewport pixels, origin top-left: <c>Input.MousePosition</c>, a touch position), seen
    /// through <c>Camera3D.Main</c>. False without a main camera or when nothing is hit.
    /// </summary>
    public static bool RaycastFromScreen(Vector2 screenPoint, out RaycastHit3D hit, float maxDistance = 1000.0f)
    {
        hit = default;
        Camera3D? camera = Camera3D.Main;
        return camera != null && Raycast(camera.ScreenPointToRay(screenPoint), out hit, maxDistance);
    }
}
