using Funke.Interop;

namespace Funke;

// ── Transform2D ─────────────────────────────────────────────────────

public class Transform2D : Component
{
    /// <summary>Read/write position relative to the parent; alias of LocalTransform.</summary>
    public Vector2 LocalPosition { get => LocalTransform; set => LocalTransform = value; }

    /// <summary>Read/write world position; alias of Position. Unlike Transform3D.WorldPosition, this has a setter.</summary>
    public Vector2 WorldPosition { get => Position; set => Position = value; }

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
    public bool Bold
    {
        get => InternalCalls.TextRenderer_GetBold(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetBold(RequireComponent<TextRenderer>(), value);
    }

    public bool Italic
    {
        get => InternalCalls.TextRenderer_GetItalic(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetItalic(RequireComponent<TextRenderer>(), value);
    }

    public bool RichText
    {
        get => InternalCalls.TextRenderer_GetRichText(RequireComponent<TextRenderer>());
        set => InternalCalls.TextRenderer_SetRichText(RequireComponent<TextRenderer>(), value);
    }

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
    /// <summary>Enables this camera's Post Processing effects; global graphics settings must also allow them.</summary>
    public bool PostProcessingEnabled
    {
        get => InternalCalls.Camera2D_GetPostProcessingEnabled(RequireComponent<Camera2D>());
        set => InternalCalls.Camera2D_SetPostProcessingEnabled(RequireComponent<Camera2D>(), value);
    }

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
        // Entity.Transform2D returns null for tag entities; throw here to preserve the diagnostic at the one required call site.
        Transform2D transform = Entity.Transform2D
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

/// <summary>How a <see cref="TerrainRenderer2D"/> maps its fill texture.</summary>
public enum TerrainFillMapping
{
    /// <summary>Fixed in the entity's frame: texture rows stay horizontal under the hills.</summary>
    World = 0,
    /// <summary>The texture's top row follows the surface.</summary>
    Draped = 1,
}

/// <summary>
/// Ground along a polyline: a fill texture below the surface and an edge texture along it. Each segment is one
/// sheared quad. Points are in the entity's frame, left to right; textures that tile need Wrap Repeat.
/// </summary>
public sealed class TerrainRenderer2D : Component
{
    private const int ValueCount = 19;

    private float Get(int index)
    {
        Span<float> v = stackalloc float[ValueCount];
        InternalCalls.TerrainRenderer2D_Get(RequireComponent<TerrainRenderer2D>(), v);
        return v[index];
    }

    private void Set(int index, ReadOnlySpan<float> values)
    {
        ulong id = RequireComponent<TerrainRenderer2D>();
        Span<float> v = stackalloc float[ValueCount];
        InternalCalls.TerrainRenderer2D_Get(id, v);
        values.CopyTo(v.Slice(index));
        InternalCalls.TerrainRenderer2D_Set(id, v);
    }

    private void Set(int index, float value) => Set(index, stackalloc float[] { value });

    public Texture? FillTexture
    {
        get => Texture.FromAssetUUID(InternalCalls.TerrainRenderer2D_GetTexture(RequireComponent<TerrainRenderer2D>(), 0));
        set => InternalCalls.TerrainRenderer2D_SetTexture(RequireComponent<TerrainRenderer2D>(), 0, value?.UUID ?? 0);
    }

    public Texture? EdgeTexture
    {
        get => Texture.FromAssetUUID(InternalCalls.TerrainRenderer2D_GetTexture(RequireComponent<TerrainRenderer2D>(), 1));
        set => InternalCalls.TerrainRenderer2D_SetTexture(RequireComponent<TerrainRenderer2D>(), 1, value?.UUID ?? 0);
    }

    public Color FillColor
    {
        get => new(Get(0), Get(1), Get(2), Get(3));
        set => Set(0, stackalloc float[] { value.R, value.G, value.B, value.A });
    }

    /// <summary>Width and height one repeat of the fill texture covers.</summary>
    public Vector2 FillTileSize
    {
        get => new(Get(4), Get(5));
        set => Set(4, stackalloc float[] { value.X, value.Y });
    }

    public TerrainFillMapping FillMapping
    {
        get => Get(6) >= 0.5f ? TerrainFillMapping.Draped : TerrainFillMapping.World;
        set => Set(6, (float)value);
    }

    /// <summary>How far the fill reaches below the surface.</summary>
    public float FillDepth { get => Get(7); set => Set(7, value); }

    public Color EdgeColor
    {
        get => new(Get(8), Get(9), Get(10), Get(11));
        set => Set(8, stackalloc float[] { value.R, value.G, value.B, value.A });
    }

    public float EdgeThickness { get => Get(12); set => Set(12, value); }

    /// <summary>Height of the edge's top above the surface (negative sinks it into the fill).</summary>
    public float EdgeOffset { get => Get(13); set => Set(13, value); }

    /// <summary>Surface length one repeat of the edge texture covers.</summary>
    public float EdgeTileLength { get => Get(14); set => Set(14, value); }

    /// <summary>Where along its texture the edge starts, in repeats; continues a neighbouring piece's pattern.</summary>
    public float EdgeTextureOffset { get => Get(18); set => Set(18, value); }

    public int SortingLayer { get => (int)Get(15); set => Set(15, value); }
    public int SortingOrder { get => (int)Get(16); set => Set(16, value); }
    public int EdgeSortingOrder { get => (int)Get(17); set => Set(17, value); }

    public int PointCount => InternalCalls.TerrainRenderer2D_GetPoints(RequireComponent<TerrainRenderer2D>(), Span<float>.Empty);

    /// <summary>Replaces the surface (left to right, in the entity's frame).</summary>
    public void SetPoints(ReadOnlySpan<Vector2> points)
    {
        var packed = new float[points.Length * 2];
        for (int i = 0; i < points.Length; ++i)
        {
            packed[i * 2] = points[i].X;
            packed[i * 2 + 1] = points[i].Y;
        }
        InternalCalls.TerrainRenderer2D_SetPoints(RequireComponent<TerrainRenderer2D>(), packed);
    }

    public Vector2[] GetPoints()
    {
        ulong id = RequireComponent<TerrainRenderer2D>();
        int count = InternalCalls.TerrainRenderer2D_GetPoints(id, Span<float>.Empty);
        var packed = new float[count * 2];
        InternalCalls.TerrainRenderer2D_GetPoints(id, packed);
        var result = new Vector2[count];
        for (int i = 0; i < count; ++i) result[i] = new Vector2(packed[i * 2], packed[i * 2 + 1]);
        return result;
    }
}

// ── ParticleSystem2D ──────────────────────────────────────────────────

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
