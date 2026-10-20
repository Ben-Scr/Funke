using System;
using Funke.Interop;

namespace Funke;

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
