using Funke.Interop;

namespace Funke;
// ── NameComponent ───────────────────────────────────────────────────

public class NameComponent : Component
{
    public string Name
    {
        get => InternalCalls.NameComponent_GetName(RequireComponent<NameComponent>());
        set => InternalCalls.NameComponent_SetName(RequireComponent<NameComponent>(), value);
    }
}

// ── Transform2D ─────────────────────────────────────────────────────

public class Transform2D : Component
{
    public Vector2 Position
    {
        get
        {
            ulong entityId = RequireComponent<Transform2D>();
            InternalCalls.Transform2D_GetPosition(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.Transform2D_SetPosition(RequireComponent<Transform2D>(), value.X, value.Y);
    }

    public float Rotation
    {
        get => InternalCalls.Transform2D_GetRotation(RequireComponent<Transform2D>()) * Mathf.Rad2Deg;
        set => InternalCalls.Transform2D_SetRotation(RequireComponent<Transform2D>(), value * Mathf.Deg2Rad);
    }

    public float RotationDegrees
    {
        get => Rotation;
        set => Rotation = value;
    }

    public Vector2 Scale
    {
        get
        {
            ulong entityId = RequireComponent<Transform2D>();
            InternalCalls.Transform2D_GetScale(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.Transform2D_SetScale(RequireComponent<Transform2D>(), value.X, value.Y);
    }

    public Vector2 LocalTransform
    {
        get
        {
            ulong entityId = RequireComponent<Transform2D>();
            InternalCalls.Transform2D_GetLocalPosition(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.Transform2D_SetLocalPosition(RequireComponent<Transform2D>(), value.X, value.Y);
    }
    public float LocalRotation
    {
        get => InternalCalls.Transform2D_GetLocalRotation(RequireComponent<Transform2D>()) * Mathf.Rad2Deg;
        set => InternalCalls.Transform2D_SetLocalRotation(RequireComponent<Transform2D>(), value * Mathf.Deg2Rad);
    }
    public Vector2 LocalScale
    {
        get
        {
            ulong entityId = RequireComponent<Transform2D>();
            InternalCalls.Transform2D_GetLocalScale(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.Transform2D_SetLocalScale(RequireComponent<Transform2D>(), value.X, value.Y);
    }

    public new Entity? Entity
    {
        get
        {
            ulong entityId = InternalCalls.Transform2D_GetEntity(RequireComponent<Transform2D>());
            return entityId != 0 ? new Entity(entityId) : null;
        }
    }

    public Vector2 Up
    {
        get
        {
            float radians = Rotation * Mathf.Deg2Rad;
            return new Vector2(-Mathf.Sin(radians), Mathf.Cos(radians));
        }
    }

    public Vector2 Down
    {
        get => -Up;
    }

    public Vector2 Left
    {
        get => -Right;
    }
    public Vector2 Right
    {
        get
        {
            float radians = Rotation * Mathf.Deg2Rad;
            return new Vector2(Mathf.Cos(radians), Mathf.Sin(radians));
        }
    }
}

// ── SpriteRenderer ──────────────────────────────────────────────────

public class SpriteRenderer : Component
{
    public Vector4 Color
    {
        get
        {
            ulong entityId = RequireComponent<SpriteRenderer>();
            InternalCalls.SpriteRenderer_GetColor(entityId, out float r, out float g, out float b, out float a);
            return new Color(r, g, b, a);
        }
        set => InternalCalls.SpriteRenderer_SetColor(RequireComponent<SpriteRenderer>(), value.X, value.Y, value.Z, value.W);
    }

    public Texture Texture
    {
        get
        {
            ulong assetId = InternalCalls.SpriteRenderer_GetTexture(RequireComponent<SpriteRenderer>());
            return Texture.FromAssetUUID(assetId)!;
        }
        set => InternalCalls.SpriteRenderer_SetTexture(RequireComponent<SpriteRenderer>(), value?.UUID ?? 0);
    }

    public int SortingOrder
    {
        get => InternalCalls.SpriteRenderer_GetSortingOrder(RequireComponent<SpriteRenderer>());
        set => InternalCalls.SpriteRenderer_SetSortingOrder(RequireComponent<SpriteRenderer>(), value);
    }

    public int SortingLayer
    {
        get => InternalCalls.SpriteRenderer_GetSortingLayer(RequireComponent<SpriteRenderer>());
        set => InternalCalls.SpriteRenderer_SetSortingLayer(RequireComponent<SpriteRenderer>(), value);
    }

    public TextureFilter FilterMode
    {
        get => (TextureFilter)InternalCalls.SpriteRenderer_GetFilter(RequireComponent<SpriteRenderer>());
        set => InternalCalls.SpriteRenderer_SetFilter(RequireComponent<SpriteRenderer>(), (int)value);
    }

    /// <summary>Material whose Sprite2D shader draws this sprite; null uses the standard sprite shader.</summary>
    public Material? Material
    {
        get => Material.FromAssetUUID(InternalCalls.SpriteRenderer_GetMaterial(RequireComponent<SpriteRenderer>()));
        set => InternalCalls.SpriteRenderer_SetMaterial(RequireComponent<SpriteRenderer>(), value?.UUID ?? 0);
    }

    /// <summary>Sprite slice name from the texture's .meta. Empty = full texture. Stale names fall back to full texture with a log warning.</summary>
    public string SpriteName
    {
        get => InternalCalls.SpriteRenderer_GetSpriteName(RequireComponent<SpriteRenderer>());
        set => InternalCalls.SpriteRenderer_SetSpriteName(RequireComponent<SpriteRenderer>(), value);
    }

    /// <summary>0 stretches the sprite over the transform scale. Above 0 the sprite keeps its native size (slice pixels / this value, times the scale) and sits on the slice pivot from the texture's .meta.</summary>
    public float PixelsPerUnit
    {
        get => NativeData.PixelsPerUnit;
        set => NativeData.PixelsPerUnit = MathF.Max(0f, value);
    }

    /// <summary>Mirrors the sprite horizontally around its pivot.</summary>
    public bool FlipX
    {
        get => NativeData.FlipX;
        set => NativeData.FlipX = value;
    }

    /// <summary>Mirrors the sprite vertically around its pivot.</summary>
    public bool FlipY
    {
        get => NativeData.FlipY;
        set => NativeData.FlipY = value;
    }

    private ref Funke.Native.NativeSpriteRenderer NativeData
    {
        get
        {
            RequireComponent<SpriteRenderer>();
            return ref Entity.GetRefOrThrow<Funke.Native.NativeSpriteRenderer>();
        }
    }
}


// ── TextRenderer ────────────────────────────────────────────────────

public enum TextAlignment { Left = 0, Center = 1, Right = 2 }
public enum TextVerticalAlignment { Top = 0, Middle = 1, Bottom = 2 }
public enum TextWrapMode { None = 0, Word = 1, Character = 2 }

public class TextRenderer : Component
{
    public string Text
    {
        get => InternalCalls.TextRenderer_GetText(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetText(RequireComponent<TextRenderer>(), value ?? "");
    }

    // Returns null when the entity references a missing or invalid font asset.
    public Font? Font
    {
        get
        {
            ulong assetId = InternalCalls.TextRenderer_GetFont(RequireComponent<TextRenderer>());
            return Font.FromAssetUUID(assetId);
        }
        set => InternalCalls.TextRenderer_SetFont(RequireComponent<TextRenderer>(), value?.UUID ?? 0);
    }

    public float FontSize
    {
        get => InternalCalls.TextRenderer_GetFontSize(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetFontSize(RequireComponent<TextRenderer>(), value);
    }

    public Vector4 Color
    {
        get
        {
            ulong entityId = RequireComponent<TextRenderer>();
            InternalCalls.TextRenderer_GetColor(entityId, out float r, out float g, out float b, out float a);
            return new Color(r, g, b, a);
        }
        set => InternalCalls.TextRenderer_SetColor(RequireComponent<TextRenderer>(), value.X, value.Y, value.Z, value.W);
    }

    public float LetterSpacing
    {
        get => InternalCalls.TextRenderer_GetLetterSpacing(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetLetterSpacing(RequireComponent<TextRenderer>(), value);
    }

    // Extra space between lines, in baked-atlas pixels (added to the font's line height).
    public float LineSpacing
    {
        get => InternalCalls.TextRenderer_GetLineSpacing(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetLineSpacing(RequireComponent<TextRenderer>(), value);
    }

    public TextAlignment Alignment
    {
        get => (TextAlignment)InternalCalls.TextRenderer_GetHAlign(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetHAlign(RequireComponent<TextRenderer>(), (int)value);
    }

    public TextVerticalAlignment VerticalAlignment
    {
        get => (TextVerticalAlignment)InternalCalls.TextRenderer_GetVAlign(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetVAlign(RequireComponent<TextRenderer>(), (int)value);
    }

    public TextWrapMode WrapMode
    {
        get => (TextWrapMode)InternalCalls.TextRenderer_GetWrapMode(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetWrapMode(RequireComponent<TextRenderer>(), (int)value);
    }

    public int SortingOrder
    {
        get => InternalCalls.TextRenderer_GetSortingOrder(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetSortingOrder(RequireComponent<TextRenderer>(), value);
    }

    public int SortingLayer
    {
        get => InternalCalls.TextRenderer_GetSortingLayer(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetSortingLayer(RequireComponent<TextRenderer>(), value);
    }

    /// <summary>Outline around the glyphs in text pixels (Font Size units); 0 = none. Fades with the text color's alpha.</summary>
    public float OutlineWidth
    {
        get { Span<float> v = stackalloc float[k_EffectValues]; ReadEffects(v); return v[0]; }
        set { Span<float> v = stackalloc float[k_EffectValues]; ReadEffects(v); v[0] = value; WriteEffects(v); }
    }

    public Funke.Color OutlineColor
    {
        get { Span<float> v = stackalloc float[k_EffectValues]; ReadEffects(v); return new Funke.Color(v[1], v[2], v[3], v[4]); }
        set { Span<float> v = stackalloc float[k_EffectValues]; ReadEffects(v); v[1] = value.R; v[2] = value.G; v[3] = value.B; v[4] = value.A; WriteEffects(v); }
    }

    /// <summary>Drop shadow offset in text pixels, +Y up: (2, -2) falls down-right. Zero = no shadow.</summary>
    public Vector2 ShadowOffset
    {
        get { Span<float> v = stackalloc float[k_EffectValues]; ReadEffects(v); return new Vector2(v[5], v[6]); }
        set { Span<float> v = stackalloc float[k_EffectValues]; ReadEffects(v); v[5] = value.X; v[6] = value.Y; WriteEffects(v); }
    }

    public Funke.Color ShadowColor
    {
        get { Span<float> v = stackalloc float[k_EffectValues]; ReadEffects(v); return new Funke.Color(v[7], v[8], v[9], v[10]); }
        set { Span<float> v = stackalloc float[k_EffectValues]; ReadEffects(v); v[7] = value.R; v[8] = value.G; v[9] = value.B; v[10] = value.A; WriteEffects(v); }
    }

    // Outline width + rgba, shadow offset xy + rgba, in TextRenderer_GetEffects order.
    private const int k_EffectValues = 11;
    private void ReadEffects(Span<float> values) => InternalCalls.TextRenderer_GetEffects(RequireComponent<TextRenderer>(), values);
    private void WriteEffects(ReadOnlySpan<float> values) => InternalCalls.TextRenderer_SetEffects(RequireComponent<TextRenderer>(), values);
}

// ── Camera2D ────────────────────────────────────────────────────────

public class Camera2D : Component
{
    public float OrthographicSize
    {
        get => InternalCalls.Camera2D_GetOrthographicSize(RequireComponent<Camera2D>());
        set => InternalCalls.Camera2D_SetOrthographicSize(RequireComponent<Camera2D>(), value);
    }

    public float Zoom
    {
        get => InternalCalls.Camera2D_GetZoom(RequireComponent<Camera2D>());
        set => InternalCalls.Camera2D_SetZoom(RequireComponent<Camera2D>(), value);
    }

    public static Camera2D? Main
    {
        get
        {
            ulong entityId = InternalCalls.Camera2D_GetMainEntity();
            if (entityId == 0) return null;
            return new Entity(entityId).GetComponent<Camera2D>();
        }
    }

    public Vector4 ClearColor
    {
        get
        {
            ulong entityId = RequireComponent<Camera2D>();
            InternalCalls.Camera2D_GetClearColor(entityId, out float r, out float g, out float b, out float a);
            return new Color(r, g, b, a);
        }
        set => InternalCalls.Camera2D_SetClearColor(RequireComponent<Camera2D>(), value.X, value.Y, value.Z, value.W);
    }

    public Vector2 ScreenToWorld(Vector2 screenPos)
    {
        InternalCalls.Camera2D_ScreenToWorld(RequireComponent<Camera2D>(), screenPos.X, screenPos.Y, out float x, out float y);
        return new Vector2(x, y);
    }

    public Vector2 WorldToScreen(Vector2 worldPos)
    {
        GetViewportSize(out float viewportWidth, out float viewportHeight);
        if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
            return Vector2.Zero;

        GetCameraBasis(viewportWidth, viewportHeight, out Vector2 position, out float cos, out float sin, out float halfWidth, out float halfHeight);
        if (halfWidth <= Mathf.Epsilon || halfHeight <= Mathf.Epsilon)
            return Vector2.Zero;

        Vector2 delta = worldPos - position;
        float localX = (cos * delta.X) + (sin * delta.Y);
        float localY = (-sin * delta.X) + (cos * delta.Y);

        float ndcX = localX / halfWidth;
        float ndcY = localY / halfHeight;

        return new Vector2(
            (ndcX + 1.0f) * 0.5f * viewportWidth,
            (1.0f - ndcY) * 0.5f * viewportHeight);
    }

    /// <summary>Pixel size of the screen the camera draws to, the one <see cref="Input.MousePosition"/> refers to (the Game View in the editor).</summary>
    public float ViewportWidth => InternalCalls.Camera2D_GetViewportWidth(RequireComponent<Camera2D>());
    public float ViewportHeight => InternalCalls.Camera2D_GetViewportHeight(RequireComponent<Camera2D>());

    // World-space axis-aligned bounds the camera currently sees (refreshed on read).
    public AABB Frustum
    {
        get
        {
            InternalCalls.Camera2D_GetFrustum(RequireComponent<Camera2D>(), out float minX, out float minY, out float maxX, out float maxY);
            return new AABB(new Vector2(minX, minY), new Vector2(maxX, maxY));
        }
    }

    public Vector2 FrustumSize => Frustum.Size;

    private void GetViewportSize(out float viewportWidth, out float viewportHeight)
    {
        ulong entityId = RequireComponent<Camera2D>();
        viewportWidth = InternalCalls.Camera2D_GetViewportWidth(entityId);
        viewportHeight = InternalCalls.Camera2D_GetViewportHeight(entityId);
    }

    private void GetCameraBasis(float viewportWidth, float viewportHeight, out Vector2 position, out float cos, out float sin, out float halfWidth, out float halfHeight)
    {
        // Entity.Transform returns null for tag entities; throw here to preserve the diagnostic at the one required call site.
        Transform2D transform = Entity.Transform
            ?? throw new InvalidOperationException(
                "Camera2DComponent.GetCameraBasis: host entity has no Transform2D component.");
        position = transform.Position;

        float rotation = transform.Rotation * Mathf.Deg2Rad;
        cos = Mathf.Cos(rotation);
        sin = Mathf.Sin(rotation);

        halfHeight = OrthographicSize * Zoom;
        halfWidth = halfHeight * (viewportWidth / viewportHeight);
    }
}

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

// ── AudioSource ─────────────────────────────────────────────────────

public class AudioSource : Component
{
    public Audio? Audio
    {
        get
        {
            ulong assetId = InternalCalls.AudioSource_GetAudio(RequireComponent<AudioSource>());
            return Funke.Audio.FromAssetUUID(assetId);
        }
        set => InternalCalls.AudioSource_SetAudio(RequireComponent<AudioSource>(), value?.UUID ?? 0);
    }

    public float Volume
    {
        get => InternalCalls.AudioSource_GetVolume(RequireComponent<AudioSource>());
        set => InternalCalls.AudioSource_SetVolume(RequireComponent<AudioSource>(), value);
    }

    public float Pitch
    {
        get => InternalCalls.AudioSource_GetPitch(RequireComponent<AudioSource>());
        set => InternalCalls.AudioSource_SetPitch(RequireComponent<AudioSource>(), value);
    }

    public bool Loop
    {
        get => InternalCalls.AudioSource_GetLoop(RequireComponent<AudioSource>());
        set => InternalCalls.AudioSource_SetLoop(RequireComponent<AudioSource>(), value);
    }

    public bool IsPlaying => InternalCalls.AudioSource_IsPlaying(RequireComponent<AudioSource>());
    public bool IsPaused => InternalCalls.AudioSource_IsPaused(RequireComponent<AudioSource>());

    public void Play() => InternalCalls.AudioSource_Play(RequireComponent<AudioSource>());
    public void Pause() => InternalCalls.AudioSource_Pause(RequireComponent<AudioSource>());
    public void Stop() => InternalCalls.AudioSource_Stop(RequireComponent<AudioSource>());
    public void Resume() => InternalCalls.AudioSource_Resume(RequireComponent<AudioSource>());

    /// <summary>
    /// Plays from the entity's position: quieter with distance from the listener (the main camera) and panned to its
    /// side. False (default) = a plain 2D sound the camera never affects.
    /// </summary>
    public bool Spatial
    {
        get => ReadSpatial(0) != 0f;
        set => WriteSpatial(0, value ? 1f : 0f);
    }

    /// <summary>Full volume up to this distance.</summary>
    public float MinDistance { get => ReadSpatial(1); set => WriteSpatial(1, value); }
    /// <summary>Silent from here with <see cref="AudioRolloff.Linear"/>; the logarithmic falloff stops getting quieter here.</summary>
    public float MaxDistance { get => ReadSpatial(2); set => WriteSpatial(2, value); }
    public AudioRolloff Rolloff { get => (AudioRolloff)(int)ReadSpatial(3); set => WriteSpatial(3, (float)value); }
    /// <summary>0 = no left/right panning, 1 = a sound to the side is silent on the other ear (default 0.8).</summary>
    public float Panning { get => ReadSpatial(4); set => WriteSpatial(4, value); }
    /// <summary>Pitch shift when source and listener move towards or away from each other; 0 = off, 1 = physical.</summary>
    public float DopplerLevel { get => ReadSpatial(5); set => WriteSpatial(5, value); }

    private float ReadSpatial(int index)
    {
        Span<float> values = stackalloc float[InternalCalls.AudioSourceSpatialValueCount];
        InternalCalls.AudioSource_GetSpatial(RequireComponent<AudioSource>(), values);
        return values[index];
    }

    private void WriteSpatial(int index, float value)
    {
        ulong id = RequireComponent<AudioSource>();
        Span<float> values = stackalloc float[InternalCalls.AudioSourceSpatialValueCount];
        InternalCalls.AudioSource_GetSpatial(id, values);
        values[index] = value;
        InternalCalls.AudioSource_SetSpatial(id, values);
    }
}

// ── ParticleSystem2D ──────────────────────────────────────────────────

/// <summary>Flip-book animation over sprite-sheet slices; frames, rate and looping are set in the Inspector.</summary>
public class SpriteAnimator2D : Component
{
    public bool IsPlaying => InternalCalls.SpriteAnimator2D_IsPlaying(RequireComponent<SpriteAnimator2D>());

    /// <summary>Index into the Frames list. Setting it restarts the timer of that frame.</summary>
    public int Frame
    {
        get => InternalCalls.SpriteAnimator2D_GetFrame(RequireComponent<SpriteAnimator2D>());
        set => InternalCalls.SpriteAnimator2D_SetFrame(RequireComponent<SpriteAnimator2D>(), value);
    }

    public float FramesPerSecond
    {
        get => InternalCalls.SpriteAnimator2D_GetFramesPerSecond(RequireComponent<SpriteAnimator2D>());
        set => InternalCalls.SpriteAnimator2D_SetFramesPerSecond(RequireComponent<SpriteAnimator2D>(), value);
    }

    public void Play() => InternalCalls.SpriteAnimator2D_Command(RequireComponent<SpriteAnimator2D>(), 0);
    public void Pause() => InternalCalls.SpriteAnimator2D_Command(RequireComponent<SpriteAnimator2D>(), 1);
    /// <summary>Stops and rewinds to the first frame.</summary>
    public void Stop() => InternalCalls.SpriteAnimator2D_Command(RequireComponent<SpriteAnimator2D>(), 2);
}

public class ParticleSystem2D : Component
{
    public bool PlayOnAwake
    {
        get => InternalCalls.ParticleSystem2D_GetPlayOnAwake(RequireComponent<ParticleSystem2D>());
        set => InternalCalls.ParticleSystem2D_SetPlayOnAwake(RequireComponent<ParticleSystem2D>(), value);
    }

    public bool IsPlaying => InternalCalls.ParticleSystem2D_IsPlaying(RequireComponent<ParticleSystem2D>());

    public Texture Texture
    {
        get
        {
            ulong assetId = InternalCalls.ParticleSystem2D_GetTexture(RequireComponent<ParticleSystem2D>());
            return Texture.FromAssetUUID(assetId)!;
        }
        set => InternalCalls.ParticleSystem2D_SetTexture(RequireComponent<ParticleSystem2D>(), value?.UUID ?? 0);
    }

    public Vector4 Color
    {
        get
        {
            ulong entityId = RequireComponent<ParticleSystem2D>();
            InternalCalls.ParticleSystem2D_GetColor(entityId, out float r, out float g, out float b, out float a);
            return new Color(r, g, b, a);
        }
        set => InternalCalls.ParticleSystem2D_SetColor(RequireComponent<ParticleSystem2D>(), value.X, value.Y, value.Z, value.W);
    }

    public float LifeTime
    {
        get => InternalCalls.ParticleSystem2D_GetLifeTime(RequireComponent<ParticleSystem2D>());
        set => InternalCalls.ParticleSystem2D_SetLifeTime(RequireComponent<ParticleSystem2D>(), value);
    }

    public float Speed
    {
        get => InternalCalls.ParticleSystem2D_GetSpeed(RequireComponent<ParticleSystem2D>());
        set => InternalCalls.ParticleSystem2D_SetSpeed(RequireComponent<ParticleSystem2D>(), value);
    }

    public Vector2 Scale
    {
        get
        {
            ulong entityId = RequireComponent<ParticleSystem2D>();
            InternalCalls.ParticleSystem2D_GetScale(entityId, out float x, out float y);
            return new Vector2(x, y);
        }
        set => InternalCalls.ParticleSystem2D_SetScale(RequireComponent<ParticleSystem2D>(), value.X, value.Y);
    }

    public int EmitOverTime
    {
        get => InternalCalls.ParticleSystem2D_GetEmitOverTime(RequireComponent<ParticleSystem2D>());
        set => InternalCalls.ParticleSystem2D_SetEmitOverTime(RequireComponent<ParticleSystem2D>(), value);
    }

    public void Play() => InternalCalls.ParticleSystem2D_Play(RequireComponent<ParticleSystem2D>());
    public void Pause() => InternalCalls.ParticleSystem2D_Pause(RequireComponent<ParticleSystem2D>());
    public void Stop() => InternalCalls.ParticleSystem2D_Stop(RequireComponent<ParticleSystem2D>());
    public void Emit(int count) => InternalCalls.ParticleSystem2D_Emit(RequireComponent<ParticleSystem2D>(), count);
}

