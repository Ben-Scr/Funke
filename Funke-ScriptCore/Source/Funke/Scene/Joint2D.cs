using System;
using Funke.Interop;

namespace Funke;

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
