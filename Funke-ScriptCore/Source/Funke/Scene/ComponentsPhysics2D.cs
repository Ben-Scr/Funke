using Funke.Interop;

namespace Funke;

// ── Rigidbody2D ─────────────────────────────────────────────────────

public enum BodyType { Static = 0, Kinematic = 1, Dynamic = 2 }

[Flags]
public enum RigidbodyConstraints2D
{
    None            = 0,
    FreezePositionX = 1 << 0,
    FreezePositionY = 1 << 1,
    FreezeRotation  = 1 << 2,
    FreezePosition  = FreezePositionX | FreezePositionY,
    FreezeAll       = FreezePosition  | FreezeRotation,
}

public class Rigidbody2D : Component
{
    public Vector2 LinearVelocity
    {
        get
        {
            ulong entityId = RequireComponent<Rigidbody2D>();
            InternalCalls.Rigidbody2D_GetLinearVelocity(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.Rigidbody2D_SetLinearVelocity(RequireComponent<Rigidbody2D>(), value.X, value.Y);
    }

    public float AngularVelocity
    {
        get => InternalCalls.Rigidbody2D_GetAngularVelocity(RequireComponent<Rigidbody2D>());
        set => InternalCalls.Rigidbody2D_SetAngularVelocity(RequireComponent<Rigidbody2D>(), value);
    }

    public BodyType BodyType
    {
        get => (BodyType)InternalCalls.Rigidbody2D_GetBodyType(RequireComponent<Rigidbody2D>());
        set => InternalCalls.Rigidbody2D_SetBodyType(RequireComponent<Rigidbody2D>(), (int)value);
    }

    public float GravityScale
    {
        get => InternalCalls.Rigidbody2D_GetGravityScale(RequireComponent<Rigidbody2D>());
        set => InternalCalls.Rigidbody2D_SetGravityScale(RequireComponent<Rigidbody2D>(), value);
    }

    public float Mass
    {
        get => InternalCalls.Rigidbody2D_GetMass(RequireComponent<Rigidbody2D>());
        set => InternalCalls.Rigidbody2D_SetMass(RequireComponent<Rigidbody2D>(), value);
    }

    public bool FreezePositionX
    {
        get => InternalCalls.Rigidbody2D_GetFreezePositionX(RequireComponent<Rigidbody2D>());
        set => InternalCalls.Rigidbody2D_SetFreezePositionX(RequireComponent<Rigidbody2D>(), value);
    }

    public bool FreezePositionY
    {
        get => InternalCalls.Rigidbody2D_GetFreezePositionY(RequireComponent<Rigidbody2D>());
        set => InternalCalls.Rigidbody2D_SetFreezePositionY(RequireComponent<Rigidbody2D>(), value);
    }

    public bool FreezeRotation
    {
        get => InternalCalls.Rigidbody2D_GetFreezeRotation(RequireComponent<Rigidbody2D>());
        set => InternalCalls.Rigidbody2D_SetFreezeRotation(RequireComponent<Rigidbody2D>(), value);
    }

    // Writes clear unmentioned flags (idempotent replace), unlike OR-ing individual properties.
    public RigidbodyConstraints2D Constraints
    {
        get
        {
            ulong entityId = RequireComponent<Rigidbody2D>();
            RigidbodyConstraints2D c = RigidbodyConstraints2D.None;
            if (InternalCalls.Rigidbody2D_GetFreezePositionX(entityId)) c |= RigidbodyConstraints2D.FreezePositionX;
            if (InternalCalls.Rigidbody2D_GetFreezePositionY(entityId)) c |= RigidbodyConstraints2D.FreezePositionY;
            if (InternalCalls.Rigidbody2D_GetFreezeRotation(entityId))  c |= RigidbodyConstraints2D.FreezeRotation;
            return c;
        }
        set
        {
            ulong entityId = RequireComponent<Rigidbody2D>();
            InternalCalls.Rigidbody2D_SetFreezePositionX(entityId, (value & RigidbodyConstraints2D.FreezePositionX) != 0);
            InternalCalls.Rigidbody2D_SetFreezePositionY(entityId, (value & RigidbodyConstraints2D.FreezePositionY) != 0);
            InternalCalls.Rigidbody2D_SetFreezeRotation (entityId, (value & RigidbodyConstraints2D.FreezeRotation)  != 0);
        }
    }

    public void ApplyForce(Vector2 force, bool wake = true)
        => InternalCalls.Rigidbody2D_ApplyForce(RequireComponent<Rigidbody2D>(), force.X, force.Y, wake);

    public void ApplyImpulse(Vector2 impulse, bool wake = true)
        => InternalCalls.Rigidbody2D_ApplyImpulse(RequireComponent<Rigidbody2D>(), impulse.X, impulse.Y, wake);

    /// <summary>Force at a world point: also turns the body when the point is off its centre of mass.</summary>
    public void ApplyForceAtPoint(Vector2 force, Vector2 worldPoint, bool wake = true)
        => InternalCalls.Rigidbody2D_ApplyAtPoint(RequireComponent<Rigidbody2D>(), 0, force.X, force.Y, worldPoint.X, worldPoint.Y, wake);

    public void ApplyImpulseAtPoint(Vector2 impulse, Vector2 worldPoint, bool wake = true)
        => InternalCalls.Rigidbody2D_ApplyAtPoint(RequireComponent<Rigidbody2D>(), 1, impulse.X, impulse.Y, worldPoint.X, worldPoint.Y, wake);

    public void ApplyTorque(float torque, bool wake = true)
        => InternalCalls.Rigidbody2D_ApplyAngular(RequireComponent<Rigidbody2D>(), 0, torque, wake);

    public void ApplyAngularImpulse(float impulse, bool wake = true)
        => InternalCalls.Rigidbody2D_ApplyAngular(RequireComponent<Rigidbody2D>(), 1, impulse, wake);

    private float Dynamic(int index)
    {
        Span<float> values = stackalloc float[8];
        InternalCalls.Rigidbody2D_GetDynamics(RequireComponent<Rigidbody2D>(), values);
        return values[index];
    }

    private void SetDynamic(int index, float value)
    {
        ulong id = RequireComponent<Rigidbody2D>();
        Span<float> values = stackalloc float[8];
        InternalCalls.Rigidbody2D_GetDynamics(id, values);
        values[index] = value;
        InternalCalls.Rigidbody2D_SetDynamics(id, values.Slice(0, 3));
    }

    /// <summary>Velocity damping (default 0.1).</summary>
    public float LinearDamping { get => Dynamic(0); set => SetDynamic(0, value); }

    /// <summary>Angular velocity damping (default 0).</summary>
    public float AngularDamping { get => Dynamic(1); set => SetDynamic(1, value); }

    /// <summary>Continuous collision against other moving bodies, for fast objects.</summary>
    public bool IsBullet { get => Dynamic(2) != 0f; set => SetDynamic(2, value ? 1f : 0f); }

    /// <summary>Rotational inertia about the centre of mass (kg·m²); 0 before the body exists.</summary>
    public float Inertia => Dynamic(3);

    public Vector2 LocalCenterOfMass => new(Dynamic(4), Dynamic(5));

    public Vector2 WorldCenterOfMass => new(Dynamic(6), Dynamic(7));
}

// ── BoxCollider2D ───────────────────────────────────────────────────

/// <summary>Settings shared by <see cref="BoxCollider2D"/>, <see cref="CircleCollider2D"/> and <see cref="PolygonCollider2D"/>.</summary>
public abstract class Collider2D : Component
{
    // Matches the colliderType argument of the native Collider2D_* bindings.
    private protected abstract int ColliderType { get; }
    private protected abstract ulong RequireSelf();

    /// <summary>Sensors don't block movement; overlaps raise OnTriggerEnter2D/Stay2D/Exit2D instead of collisions.</summary>
    public bool IsSensor
    {
        get => InternalCalls.Collider2D_GetSensor(RequireSelf(), ColliderType);
        set => InternalCalls.Collider2D_SetSensor(RequireSelf(), ColliderType, value);
    }

    /// <summary>Layer bits this collider belongs to. Build values with <see cref="PhysicsLayers.Mask"/>.</summary>
    public ulong Layer
    {
        get => InternalCalls.Collider2D_GetLayer(RequireSelf(), ColliderType);
        set => InternalCalls.Collider2D_SetLayer(RequireSelf(), ColliderType, value);
    }

    /// <summary>Layer bits this collider collides with. Both colliders must include each other's layer to touch.</summary>
    public ulong CollisionMask
    {
        get => InternalCalls.Collider2D_GetCollisionMask(RequireSelf(), ColliderType);
        set => InternalCalls.Collider2D_SetCollisionMask(RequireSelf(), ColliderType, value);
    }

    public float Friction
    {
        get => InternalCalls.Collider2D_GetFriction(RequireSelf(), ColliderType);
        set => InternalCalls.Collider2D_SetFriction(RequireSelf(), ColliderType, value);
    }

    public float Bounciness
    {
        get => InternalCalls.Collider2D_GetBounciness(RequireSelf(), ColliderType);
        set => InternalCalls.Collider2D_SetBounciness(RequireSelf(), ColliderType, value);
    }

    /// <summary>Box2D collision group: the same negative group never touches, the same positive group always touches, 0 lets the layers decide.</summary>
    public int GroupIndex
    {
        get => InternalCalls.Collider2D_GetGroupIndex(RequireSelf(), ColliderType);
        set => InternalCalls.Collider2D_SetGroupIndex(RequireSelf(), ColliderType, value);
    }

    /// <summary>Mass per area (kg/m², default 1). A Rigidbody2D without a pinned mass weighs the sum of its colliders' density × area.</summary>
    public float Density
    {
        get => InternalCalls.Collider2D_GetDensity(RequireSelf(), ColliderType);
        set => InternalCalls.Collider2D_SetDensity(RequireSelf(), ColliderType, value);
    }
}

public class BoxCollider2D : Collider2D
{
    private protected override int ColliderType => 0;
    private protected override ulong RequireSelf() => RequireComponent<BoxCollider2D>();

    public Vector2 Scale
    {
        get
        {
            ulong entityId = RequireComponent<BoxCollider2D>();
            InternalCalls.BoxCollider2D_GetScale(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
    }

    public Vector2 Center
    {
        get
        {
            ulong entityId = RequireComponent<BoxCollider2D>();
            InternalCalls.BoxCollider2D_GetCenter(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
    }

    public bool Enabled
    {
        set => InternalCalls.BoxCollider2D_SetEnabled(RequireComponent<BoxCollider2D>(), value);
    }
}

// ── CircleCollider2D ────────────────────────────────────────────────

public class CircleCollider2D : Collider2D
{
    private protected override int ColliderType => 1;
    private protected override ulong RequireSelf() => RequireComponent<CircleCollider2D>();

    // Local-space radius — the world radius is this multiplied by the larger
    // axis of the entity's transform scale (Unity's CircleCollider2D contract).
    public float Radius
    {
        get => InternalCalls.CircleCollider2D_GetRadius(RequireComponent<CircleCollider2D>());
        set => InternalCalls.CircleCollider2D_SetRadius(RequireComponent<CircleCollider2D>(), value);
    }

    public Vector2 Center
    {
        get
        {
            ulong entityId = RequireComponent<CircleCollider2D>();
            InternalCalls.CircleCollider2D_GetCenter(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.CircleCollider2D_SetCenter(RequireComponent<CircleCollider2D>(), value.X, value.Y);
    }

    public bool Enabled
    {
        set => InternalCalls.CircleCollider2D_SetEnabled(RequireComponent<CircleCollider2D>(), value);
    }
}

// ── PolygonCollider2D ───────────────────────────────────────────────

public class PolygonCollider2D : Collider2D
{
    private protected override int ColliderType => 2;
    private protected override ulong RequireSelf() => RequireComponent<PolygonCollider2D>();

    // Box2D's hard cap on convex-polygon vertex count.
    public const int MaxVertices = 8;
    public const int MinVertices = 3;

    public int VertexCount => InternalCalls.PolygonCollider2D_GetVertexCount(RequireComponent<PolygonCollider2D>());

    public Vector2 Center
    {
        get
        {
            ulong entityId = RequireComponent<PolygonCollider2D>();
            InternalCalls.PolygonCollider2D_GetCenter(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.PolygonCollider2D_SetCenter(RequireComponent<PolygonCollider2D>(), value.X, value.Y);
    }

    public Vector2 Size
    {
        get
        {
            ulong entityId = RequireComponent<PolygonCollider2D>();
            InternalCalls.PolygonCollider2D_GetSize(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.PolygonCollider2D_SetSize(RequireComponent<PolygonCollider2D>(), value.X, value.Y);
    }

    public bool Enabled
    {
        set => InternalCalls.PolygonCollider2D_SetEnabled(RequireComponent<PolygonCollider2D>(), value);
    }

    // Replace the polygon with a regular n-gon (3..MaxVertices). Sides outside
    // that range are clamped on the native side, matching the inspector.
    public void SetSides(int sides)
        => InternalCalls.PolygonCollider2D_SetSides(RequireComponent<PolygonCollider2D>(), sides);

    /// <summary>Most points one path may have.</summary>
    public const int MaxPathPoints = 16384;

    /// <summary>
    /// Replaces path 0. Up to <see cref="MaxVertices"/> points that form a convex outline (or are unordered) become their
    /// convex hull; a longer or concave outline in order is split into convex pieces automatically.
    /// </summary>
    public void SetPoints(ReadOnlySpan<Vector2> points)
    {
        if (points.Length < MinVertices || points.Length > MaxPathPoints)
        {
            Log.Error($"PolygonCollider2D.SetPoints requires {MinVertices}..{MaxPathPoints} points, got {points.Length}");
            return;
        }

        // Interleaved (x, y) floats, read back natively as Vec2[]. Large outlines go to the heap.
        float[]? rented = points.Length > 256 ? new float[points.Length * 2] : null;
        Span<float> packed = rented != null ? rented : stackalloc float[points.Length * 2];
        for (int i = 0; i < points.Length; ++i)
        {
            packed[i * 2 + 0] = points[i].X;
            packed[i * 2 + 1] = points[i].Y;
        }
        InternalCalls.PolygonCollider2D_SetPoints(RequireComponent<PolygonCollider2D>(), packed, points.Length);
    }

    /// <summary>Number of outlines; every one collides (concave ones are split into convex pieces).</summary>
    public int PathCount => InternalCalls.PolygonCollider2D_GetPathCount(RequireComponent<PolygonCollider2D>());

    /// <summary>The authored points of one path, before scale, size and offset.</summary>
    public Vector2[] GetPath(int index)
    {
        ulong id = RequireComponent<PolygonCollider2D>();
        int count = InternalCalls.PolygonCollider2D_GetPath(id, index, Span<float>.Empty);
        if (count <= 0) return Array.Empty<Vector2>();
        var packed = new float[count * 2];
        InternalCalls.PolygonCollider2D_GetPath(id, index, packed);
        var result = new Vector2[count];
        for (int i = 0; i < count; ++i) result[i] = new Vector2(packed[i * 2], packed[i * 2 + 1]);
        return result;
    }

    /// <summary>Replaces every path (e.g. a concave car body as several outlines). An empty list restores the default pentagon.</summary>
    public void SetPaths(params Vector2[][] paths)
    {
        int total = 0;
        foreach (Vector2[] path in paths) total += path.Length;
        var packed = new float[total * 2];
        var counts = new int[paths.Length];
        int offset = 0;
        for (int p = 0; p < paths.Length; ++p)
        {
            counts[p] = paths[p].Length;
            foreach (Vector2 point in paths[p])
            {
                packed[offset++] = point.X;
                packed[offset++] = point.Y;
            }
        }
        InternalCalls.PolygonCollider2D_SetPaths(RequireComponent<PolygonCollider2D>(), packed, counts);
    }

    public int GetWorldPoints(Span<Vector2> outPoints)
    {
        if (outPoints.Length == 0) return 0;

        // Native never returns more than MaxVertices; sizing the stackalloc by the caller's span could overflow the stack.
        int capacity = Math.Min(outPoints.Length, MaxVertices);
        Span<float> packed = stackalloc float[capacity * 2];
        int written = InternalCalls.PolygonCollider2D_GetWorldPoints(RequireComponent<PolygonCollider2D>(), packed);
        for (int i = 0; i < written; ++i)
        {
            outPoints[i] = new Vector2(packed[i * 2 + 0], packed[i * 2 + 1]);
        }
        return written;
    }
}

/// <summary>
/// Shared settings of the 2D joints. The entity's body (its <see cref="Rigidbody2D"/>, or the static body of its collider)
/// is body A; <see cref="ConnectedBody"/> is body B, or the world when null. The Box2D joint is built during play once
/// both bodies exist; angles and translations count from the pose the bodies have at that moment.
/// </summary>
public abstract class Joint2D : Component
{
    // Matches the kind argument of the native Joint2D_* bindings.
    private protected abstract int Kind { get; }
    private protected abstract int ValueCount { get; }
    private protected abstract ulong RequireSelf();

    // Packed layout shared by every joint; the type's own fields follow from index 6.
    private const int AnchorX = 0, AnchorY = 1, ConnectedAnchorX = 2, ConnectedAnchorY = 3, AutoConfigure = 4, Collide = 5;

    private protected float Get(int index)
    {
        Span<float> values = stackalloc float[ValueCount];
        InternalCalls.Joint2D_Get(RequireSelf(), Kind, values);
        return values[index];
    }

    private protected void Set(int index, float value)
    {
        ulong id = RequireSelf();
        Span<float> values = stackalloc float[ValueCount];
        InternalCalls.Joint2D_Get(id, Kind, values);
        values[index] = value;
        InternalCalls.Joint2D_Set(id, Kind, values);
    }

    private protected void Set(int index, float a, float b)
    {
        ulong id = RequireSelf();
        Span<float> values = stackalloc float[ValueCount];
        InternalCalls.Joint2D_Get(id, Kind, values);
        values[index] = a;
        values[index + 1] = b;
        InternalCalls.Joint2D_Set(id, Kind, values);
    }

    private protected float State(int index)
    {
        Span<float> state = stackalloc float[7];
        InternalCalls.Joint2D_GetState(RequireSelf(), Kind, state);
        return state[index];
    }

    /// <summary>Body B. Null attaches the joint to the world.</summary>
    public Entity? ConnectedBody
    {
        get
        {
            ulong id = InternalCalls.Joint2D_GetConnectedBody(RequireSelf(), Kind);
            return id != 0 ? Entity.FromID(id) : null;
        }
        set => InternalCalls.Joint2D_SetConnectedBody(RequireSelf(), Kind, value?.ID ?? 0);
    }

    /// <summary>Joint point in this body's frame, in meters (not scaled by the transform).</summary>
    public Vector2 Anchor
    {
        get => new(Get(AnchorX), Get(AnchorY));
        set => Set(AnchorX, value.X, value.Y);
    }

    /// <summary>Joint point in <see cref="ConnectedBody"/>'s frame (world space without one). Used only when <see cref="AutoConfigureConnectedAnchor"/> is off.</summary>
    public Vector2 ConnectedAnchor
    {
        get => new(Get(ConnectedAnchorX), Get(ConnectedAnchorY));
        set => Set(ConnectedAnchorX, value.X, value.Y);
    }

    /// <summary>Pins the connected anchor where <see cref="Anchor"/> is when the joint is built.</summary>
    public bool AutoConfigureConnectedAnchor
    {
        get => Get(AutoConfigure) != 0f;
        set => Set(AutoConfigure, value ? 1f : 0f);
    }

    public bool CollideConnected
    {
        get => Get(Collide) != 0f;
        set => Set(Collide, value ? 1f : 0f);
    }

    /// <summary>True once the Box2D joint exists (during play, with both bodies present).</summary>
    public bool IsCreated => State(0) != 0f;

    /// <summary>Force in newtons the joint applied to body B in the last physics step.</summary>
    public Vector2 ReactionForce => new(State(1), State(2));

    /// <summary>Torque in newton-meters the joint applied to body B in the last physics step.</summary>
    public float ReactionTorque => State(3);
}

/// <summary>Hinge: body B turns around the anchor. Angles in radians, relative to the pose when the joint was built.</summary>
public sealed class RevoluteJoint2D : Joint2D
{
    private protected override int Kind => 0;
    private protected override int ValueCount => 15;
    private protected override ulong RequireSelf() => RequireComponent<RevoluteJoint2D>();

    public bool EnableLimit { get => Get(6) != 0f; set => Set(6, value ? 1f : 0f); }
    public float LowerAngle { get => Get(7); set => Set(7, value); }
    public float UpperAngle { get => Get(8); set => Set(8, value); }
    public bool EnableMotor { get => Get(9) != 0f; set => Set(9, value ? 1f : 0f); }
    /// <summary>Radians per second; positive turns body B counter-clockwise relative to body A.</summary>
    public float MotorSpeed { get => Get(10); set => Set(10, value); }
    public float MaxMotorTorque { get => Get(11); set => Set(11, value); }
    public bool EnableSpring { get => Get(12) != 0f; set => Set(12, value ? 1f : 0f); }
    public float SpringHertz { get => Get(13); set => Set(13, value); }
    public float SpringDampingRatio { get => Get(14); set => Set(14, value); }

    public float JointAngle => State(4);
    public float JointSpeed => State(5);
    public float MotorTorque => State(6);
}

/// <summary>Slider: body B moves along <see cref="Axis"/> (in body A's frame) and keeps its angle relative to A.</summary>
public sealed class PrismaticJoint2D : Joint2D
{
    private protected override int Kind => 1;
    private protected override int ValueCount => 17;
    private protected override ulong RequireSelf() => RequireComponent<PrismaticJoint2D>();

    public Vector2 Axis { get => new(Get(6), Get(7)); set => Set(6, value.X, value.Y); }
    public bool EnableLimit { get => Get(8) != 0f; set => Set(8, value ? 1f : 0f); }
    public float LowerTranslation { get => Get(9); set => Set(9, value); }
    public float UpperTranslation { get => Get(10); set => Set(10, value); }
    public bool EnableMotor { get => Get(11) != 0f; set => Set(11, value ? 1f : 0f); }
    /// <summary>Meters per second along the axis.</summary>
    public float MotorSpeed { get => Get(12); set => Set(12, value); }
    public float MaxMotorForce { get => Get(13); set => Set(13, value); }
    public bool EnableSpring { get => Get(14) != 0f; set => Set(14, value ? 1f : 0f); }
    public float SpringHertz { get => Get(15); set => Set(15, value); }
    public float SpringDampingRatio { get => Get(16); set => Set(16, value); }

    public float JointTranslation => State(4);
    public float JointSpeed => State(5);
    public float MotorForce => State(6);
}

/// <summary>Glues body B to body A in their relative pose when the joint is built. 0 Hz is rigid; above 0 it springs.</summary>
public sealed class WeldJoint2D : Joint2D
{
    private protected override int Kind => 2;
    private protected override int ValueCount => 10;
    private protected override ulong RequireSelf() => RequireComponent<WeldJoint2D>();

    public float LinearHertz { get => Get(6); set => Set(6, value); }
    public float AngularHertz { get => Get(7); set => Set(7, value); }
    public float LinearDampingRatio { get => Get(8); set => Set(8, value); }
    public float AngularDampingRatio { get => Get(9); set => Set(9, value); }
}
