using System;
using Funke.Interop;

namespace Funke;

// Values must match the native MaterialSurface (serialized as int).
public enum MaterialSurface
{
    Opaque = 0,
    /// <summary>Blended by the alpha of BaseColor, the texture and the renderer's Color; drawn back to front, no depth writes.</summary>
    Transparent = 1,
}

/// <summary>
/// A .material asset. Every renderer that references it shares one live instance: property changes made
/// here are seen by all of them on the next frame and are not written back to the file.
/// With a user shader (<see cref="ShaderUUID"/>), its <c>// @property</c> values are read and written by name.
/// </summary>
public sealed class Material : IEquatable<Material>
{
    public ulong UUID { get; }

    internal Material(ulong assetId)
    {
        UUID = assetId;
    }

    public static Material? FromAssetUUID(ulong assetId)
        => assetId == 0 ? null : new Material(assetId);

    /// <summary>False when the GUID does not resolve to a readable .material file.</summary>
    public bool IsValid => UUID != 0 && InternalCalls.Material_Get(UUID, Span<float>.Empty);

    public Color BaseColor
    {
        get { var v = Read(); return new Color(v[0], v[1], v[2], v[3]); }
        set => Write(v => { v[0] = value.R; v[1] = value.G; v[2] = value.B; v[3] = value.A; });
    }

    /// <summary>Base color texture (sampled with repeat wrapping, multiplied into BaseColor); null for none.</summary>
    public Texture? BaseColorTexture
    {
        get => Texture.FromAssetUUID(InternalCalls.Material_GetTexture(UUID));
        set => InternalCalls.Material_SetTexture(UUID, value?.UUID ?? 0);
    }

    /// <summary>0 = dielectric, 1 = metal.</summary>
    public float Metallic
    {
        get => Read()[4];
        set => Write(v => v[4] = value);
    }

    /// <summary>0 = mirror-like, 1 = fully rough.</summary>
    public float Roughness
    {
        get => Read()[5];
        set => Write(v => v[5] = value);
    }

    public Color EmissiveColor
    {
        get { var v = Read(); return new Color(v[6], v[7], v[8], 1.0f); }
        set => Write(v => { v[6] = value.R; v[7] = value.G; v[8] = value.B; });
    }

    public float EmissiveIntensity
    {
        get => Read()[9];
        set => Write(v => v[9] = value);
    }

    /// <summary>False ignores lights, exposure and tone mapping and shows the base color as is.</summary>
    public bool Lit
    {
        get => Read()[10] > 0.5f;
        set => Write(v => v[10] = value ? 1.0f : 0.0f);
    }

    public Vector2 Tiling
    {
        get { var v = Read(); return new Vector2(v[11], v[12]); }
        set => Write(v => { v[11] = value.X; v[12] = value.Y; });
    }

    public Vector2 Offset
    {
        get { var v = Read(); return new Vector2(v[13], v[14]); }
        set => Write(v => { v[13] = value.X; v[14] = value.Y; });
    }

    public MaterialSurface Surface
    {
        get => (MaterialSurface)(int)Read()[15];
        set => Write(v => v[15] = (int)value);
    }

    /// <summary>GUID of the .wgsl user shader; 0 = the standard PBR shader.</summary>
    public ulong ShaderUUID
    {
        get => InternalCalls.Material_GetShader(UUID);
        set => InternalCalls.Material_SetShader(UUID, value);
    }

    // Values of the native ShaderPropertyType.
    private const int k_Float = 0, k_Vec2 = 1, k_Vec3 = 2, k_Vec4 = 3, k_Color = 4, k_Texture = 5;

    /// <summary>True when the shader declares the property or the material stores a value for it.</summary>
    public bool HasProperty(string name) => InternalCalls.Material_GetProperty(UUID, name, stackalloc float[4], out _) != 0;

    public void SetFloat(string name, float value) => SetValue(name, k_Float, value, 0, 0, 0);
    public void SetVector(string name, Vector2 value) => SetValue(name, k_Vec2, value.X, value.Y, 0, 0);
    public void SetVector(string name, Vector3 value) => SetValue(name, k_Vec3, value.X, value.Y, value.Z, 0);
    public void SetVector(string name, Vector4 value) => SetValue(name, k_Vec4, value.X, value.Y, value.Z, value.W);
    public void SetColor(string name, Color value) => SetValue(name, k_Color, value.R, value.G, value.B, value.A);

    /// <summary>Assigns a texture property; null samples the shader's default texture.</summary>
    public void SetTexture(string name, Texture? texture)
    {
        if (!InternalCalls.Material_SetProperty(UUID, name, k_Texture, ReadOnlySpan<float>.Empty, texture?.UUID ?? 0))
            Log.Warn($"Material {UUID}: '{name}' is not a texture property of its shader");
    }

    /// <summary>The value the shader sees (its default while the material has none); 0 for unknown names.</summary>
    public float GetFloat(string name) => GetValue(name)[0];

    public Vector4 GetVector(string name)
    {
        float[] v = GetValue(name);
        return new Vector4(v[0], v[1], v[2], v[3]);
    }

    public Color GetColor(string name)
    {
        float[] v = GetValue(name);
        return new Color(v[0], v[1], v[2], v[3]);
    }

    public Texture? GetTexture(string name)
    {
        InternalCalls.Material_GetProperty(UUID, name, stackalloc float[4], out ulong texture);
        return Texture.FromAssetUUID(texture);
    }

    private void SetValue(string name, int type, float x, float y, float z, float w)
    {
        ReadOnlySpan<float> values = stackalloc float[4] { x, y, z, w };
        if (!InternalCalls.Material_SetProperty(UUID, name, type, values, 0))
            Log.Warn($"Material {UUID}: '{name}' has another type in its shader");
    }

    private float[] GetValue(string name)
    {
        var values = new float[4];
        InternalCalls.Material_GetProperty(UUID, name, values, out _);
        return values;
    }

    private float[] Read()
    {
        var values = new float[InternalCalls.MaterialValueCount];
        InternalCalls.Material_Get(UUID, values);
        return values;
    }

    private void Write(Action<float[]> change)
    {
        float[] values = Read();
        change(values);
        InternalCalls.Material_Set(UUID, values);
    }

    public bool Equals(Material? other) => other is not null && other.UUID == UUID;
    public override bool Equals(object? obj) => obj is Material other && Equals(other);
    public override int GetHashCode() => UUID.GetHashCode();
    public static bool operator ==(Material? a, Material? b) => a is null ? b is null : a.Equals(b);
    public static bool operator !=(Material? a, Material? b) => !(a == b);
}
