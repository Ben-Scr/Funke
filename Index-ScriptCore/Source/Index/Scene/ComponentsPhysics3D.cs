using System;
using Index.Interop;

namespace Index;

/// <summary>How <see cref="Rigidbody3D.AddForce"/> interprets its vector.</summary>
public enum ForceMode3D
{
    /// <summary>Continuous force (mass * distance / time²), applied over the next physics step.</summary>
    Force = 0,
    /// <summary>Instant impulse (mass * distance / time).</summary>
    Impulse = 1,
    /// <summary>Continuous acceleration, independent of mass.</summary>
    Acceleration = 2,
    /// <summary>Instant velocity change, independent of mass.</summary>
    VelocityChange = 3,
}

// Values must match the native CapsuleAxis (serialized as int).
public enum CapsuleDirection
{
    X = 0,
    Y = 1,
    Z = 2,
}

/// <summary>Per-axis movement locks of a <see cref="Rigidbody3D"/>.</summary>
[Flags]
public enum RigidbodyConstraints3D
{
    None = 0,
    FreezePositionX = 1 << 0,
    FreezePositionY = 1 << 1,
    FreezePositionZ = 1 << 2,
    FreezeRotationX = 1 << 3,
    FreezeRotationY = 1 << 4,
    FreezeRotationZ = 1 << 5,
    FreezePosition = FreezePositionX | FreezePositionY | FreezePositionZ,
    FreezeRotation = FreezeRotationX | FreezeRotationY | FreezeRotationZ,
    FreezeAll = FreezePosition | FreezeRotation,
}

// ── Rigidbody3D ─────────────────────────────────────────────────────

/// <summary>
/// Simulates the entity with Jolt Physics. Colliders on this entity and on child entities without their own
/// Rigidbody3D form its shape. Velocities and forces only take effect in play mode.
/// </summary>
public unsafe class Rigidbody3D : Component
{
    // Packed layout documented in ScriptBindingsPhysics3D.cpp.
    private const int k_ValueCount = 7;

    private float Read(int index)
    {
        float* values = stackalloc float[k_ValueCount];
        NativeCallbacks.Bindings.Rigidbody3D_Get(RequireComponent<Rigidbody3D>(), values, k_ValueCount);
        return values[index];
    }

    private void Write(int index, float value)
    {
        ulong id = RequireComponent<Rigidbody3D>();
        float* values = stackalloc float[k_ValueCount];
        NativeCallbacks.Bindings.Rigidbody3D_Get(id, values, k_ValueCount);
        values[index] = value;
        NativeCallbacks.Bindings.Rigidbody3D_Set(id, values, k_ValueCount);
    }

    public BodyType BodyType
    {
        get => (BodyType)(int)Read(0);
        set => Write(0, (int)value);
    }

    public float Mass
    {
        get => Read(1);
        set => Write(1, value);
    }

    public float LinearDrag
    {
        get => Read(2);
        set => Write(2, value);
    }

    public float AngularDrag
    {
        get => Read(3);
        set => Write(3, value);
    }

    public float GravityScale
    {
        get => Read(4);
        set => Write(4, value);
    }

    /// <summary>Sweeps the body between steps so fast objects don't pass through thin colliders.</summary>
    public bool ContinuousCollision
    {
        get => Read(5) != 0.0f;
        set => Write(5, value ? 1.0f : 0.0f);
    }

    public RigidbodyConstraints3D Constraints
    {
        get => (RigidbodyConstraints3D)(int)Read(6);
        set => Write(6, (int)value);
    }

    public Vector3 LinearVelocity
    {
        get => ReadVelocity(0);
        set => NativeCallbacks.Bindings.Rigidbody3D_SetVelocity(RequireComponent<Rigidbody3D>(), 0, value.X, value.Y, value.Z);
    }

    /// <summary>Angular velocity in radians per second.</summary>
    public Vector3 AngularVelocity
    {
        get => ReadVelocity(1);
        set => NativeCallbacks.Bindings.Rigidbody3D_SetVelocity(RequireComponent<Rigidbody3D>(), 1, value.X, value.Y, value.Z);
    }

    public bool IsSleeping => NativeCallbacks.Bindings.Rigidbody3D_IsSleeping(RequireComponent<Rigidbody3D>()) != 0;

    public void AddForce(Vector3 force, ForceMode3D mode = ForceMode3D.Force)
        => NativeCallbacks.Bindings.Rigidbody3D_AddForce(RequireComponent<Rigidbody3D>(), 0, (int)mode, force.X, force.Y, force.Z, 0, 0, 0);

    public void AddForceAtPosition(Vector3 force, Vector3 worldPosition, ForceMode3D mode = ForceMode3D.Force)
        => NativeCallbacks.Bindings.Rigidbody3D_AddForce(RequireComponent<Rigidbody3D>(), 1, (int)mode, force.X, force.Y, force.Z, worldPosition.X, worldPosition.Y, worldPosition.Z);

    public void AddTorque(Vector3 torque, ForceMode3D mode = ForceMode3D.Force)
        => NativeCallbacks.Bindings.Rigidbody3D_AddForce(RequireComponent<Rigidbody3D>(), 2, (int)mode, torque.X, torque.Y, torque.Z, 0, 0, 0);

    public void WakeUp() => NativeCallbacks.Bindings.Rigidbody3D_SetSleeping(RequireComponent<Rigidbody3D>(), 0);
    public void Sleep() => NativeCallbacks.Bindings.Rigidbody3D_SetSleeping(RequireComponent<Rigidbody3D>(), 1);

    private Vector3 ReadVelocity(int angular)
    {
        float x, y, z;
        NativeCallbacks.Bindings.Rigidbody3D_GetVelocity(RequireComponent<Rigidbody3D>(), angular, &x, &y, &z);
        return new Vector3(x, y, z);
    }
}

// ── Colliders ───────────────────────────────────────────────────────

/// <summary>Shared settings of every 3D collider. Sizes are local; the entity's scale applies on top.</summary>
public abstract unsafe class Collider3D : Component
{
    // Packed layout documented in ScriptBindingsPhysics3D.cpp.
    private const int k_ValueCount = 9;

    private protected abstract int Kind { get; }
    private protected abstract ulong RequireSelf();

    private protected float Read(int index)
    {
        float* values = stackalloc float[k_ValueCount];
        NativeCallbacks.Bindings.Collider3D_Get(RequireSelf(), Kind, values, k_ValueCount);
        return values[index];
    }

    private protected void Write(int index, float value)
    {
        ulong id = RequireSelf();
        float* values = stackalloc float[k_ValueCount];
        NativeCallbacks.Bindings.Collider3D_Get(id, Kind, values, k_ValueCount);
        values[index] = value;
        NativeCallbacks.Bindings.Collider3D_Set(id, Kind, values, k_ValueCount);
    }

    private protected Vector3 ReadVector(int index) => new(Read(index), Read(index + 1), Read(index + 2));

    private protected void WriteVector(int index, Vector3 value)
    {
        ulong id = RequireSelf();
        float* values = stackalloc float[k_ValueCount];
        NativeCallbacks.Bindings.Collider3D_Get(id, Kind, values, k_ValueCount);
        values[index] = value.X;
        values[index + 1] = value.Y;
        values[index + 2] = value.Z;
        NativeCallbacks.Bindings.Collider3D_Set(id, Kind, values, k_ValueCount);
    }

    /// <summary>Shape offset in the entity's local space.</summary>
    public Vector3 Center
    {
        get => ReadVector(0);
        set => WriteVector(0, value);
    }

    /// <summary>Reports OnTrigger*3D overlaps instead of colliding. A body is a trigger only when all of its colliders are.</summary>
    public bool IsTrigger
    {
        get => Read(3) != 0.0f;
        set => Write(3, value ? 1.0f : 0.0f);
    }

    public float Friction
    {
        get => Read(4);
        set => Write(4, value);
    }

    /// <summary>Restitution, 0 (no bounce) to 1.</summary>
    public float Bounciness
    {
        get => Read(5);
        set => Write(5, value);
    }
}

public class BoxCollider3D : Collider3D
{
    private protected override int Kind => 0;
    private protected override ulong RequireSelf() => RequireComponent<BoxCollider3D>();

    public Vector3 Size
    {
        get => ReadVector(6);
        set => WriteVector(6, value);
    }
}

public class SphereCollider3D : Collider3D
{
    private protected override int Kind => 1;
    private protected override ulong RequireSelf() => RequireComponent<SphereCollider3D>();

    public float Radius
    {
        get => Read(6);
        set => Write(6, value);
    }
}

public class CapsuleCollider3D : Collider3D
{
    private protected override int Kind => 2;
    private protected override ulong RequireSelf() => RequireComponent<CapsuleCollider3D>();

    public float Radius
    {
        get => Read(6);
        set => Write(6, value);
    }

    /// <summary>End to end, including both hemispheres.</summary>
    public float Height
    {
        get => Read(7);
        set => Write(7, value);
    }

    public CapsuleDirection Direction
    {
        get => (CapsuleDirection)(int)Read(8);
        set => Write(8, (int)value);
    }
}

public class MeshCollider3D : Collider3D
{
    private protected override int Kind => 3;
    private protected override ulong RequireSelf() => RequireComponent<MeshCollider3D>();

    /// <summary>Collide with the mesh of this entity's MeshRenderer instead of <see cref="Mesh"/>.</summary>
    public bool UseMeshRenderer
    {
        get => Read(6) != 0.0f;
        set => Write(6, value ? 1.0f : 0.0f);
    }

    public PrimitiveMesh Mesh
    {
        get => (PrimitiveMesh)(int)Read(7);
        set => Write(7, (int)value);
    }

    /// <summary>A mesh asset (.obj) used instead of <see cref="Mesh"/> when <see cref="UseMeshRenderer"/> is off.</summary>
    public global::Index.Mesh? CustomMesh
    {
        get => global::Index.Mesh.FromAssetUUID(InternalCalls.MeshCollider3D_GetCustomMesh(RequireSelf()));
        set => InternalCalls.MeshCollider3D_SetCustomMesh(RequireSelf(), value?.UUID ?? 0);
    }

    /// <summary>Use the convex hull. Required on dynamic bodies; a concave mesh only collides as static or kinematic.</summary>
    public bool Convex
    {
        get => Read(8) != 0.0f;
        set => Write(8, value ? 1.0f : 0.0f);
    }
}
