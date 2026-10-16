using System;
using System.Runtime.InteropServices;
using Funke.Interop;

namespace Funke;

/// <summary>
/// A mesh asset (.obj), or a runtime mesh built from code with <see cref="Create"/>. Assign it to
/// <see cref="MeshRenderer.CustomMesh"/> or <see cref="MeshCollider3D.CustomMesh"/>; every user shares the same geometry.
/// </summary>
public sealed class Mesh : IEquatable<Mesh>
{
    public ulong UUID { get; }

    internal Mesh(ulong assetId)
    {
        UUID = assetId;
    }

    public static Mesh? FromAssetUUID(ulong assetId)
        => assetId == 0 ? null : new Mesh(assetId);

    /// <summary>
    /// Creates an empty in-memory mesh. Fill it with <see cref="SetData"/> whenever the geometry changes (every frame is fine:
    /// the GPU buffers are rewritten in place). It lives until <see cref="Destroy"/> or the end of Play mode.
    /// </summary>
    public static Mesh Create() => new(InternalCalls.Mesh_CreateRuntime());

    /// <summary>False when the GUID does not resolve to a readable mesh file, or a runtime mesh has no triangles yet.</summary>
    public bool IsValid => UUID != 0 && InternalCalls.Mesh_GetInfo(UUID, Span<float>.Empty);

    /// <summary>True for a live mesh made with <see cref="Create"/>.</summary>
    public bool IsRuntime => UUID != 0 && InternalCalls.Mesh_IsRuntime(UUID);

    public int VertexCount => (int)Read()[0];
    public int TriangleCount => (int)Read()[1];

    /// <summary>Object-space bounds in the file's units.</summary>
    public Vector3 BoundsMin { get { var v = Read(); return new Vector3(v[2], v[3], v[4]); } }
    public Vector3 BoundsMax { get { var v = Read(); return new Vector3(v[5], v[6], v[7]); } }

    /// <summary>
    /// Replaces the geometry of a runtime mesh. <paramref name="indices"/> is a triangle list (counter-clockwise front faces).
    /// Leave <paramref name="normals"/> empty to get smooth normals computed from the triangles; empty <paramref name="uvs"/> are zero.
    /// </summary>
    /// <exception cref="ArgumentException">Normals or UVs don't match the vertex count, or an index is out of range.</exception>
    /// <exception cref="InvalidOperationException">This is not a live runtime mesh.</exception>
    public void SetData(ReadOnlySpan<Vector3> positions, ReadOnlySpan<int> indices,
        ReadOnlySpan<Vector3> normals = default, ReadOnlySpan<Vector2> uvs = default)
    {
        if (!normals.IsEmpty && normals.Length != positions.Length)
            throw new ArgumentException($"Expected {positions.Length} normals, got {normals.Length}.", nameof(normals));
        if (!uvs.IsEmpty && uvs.Length != positions.Length)
            throw new ArgumentException($"Expected {positions.Length} UVs, got {uvs.Length}.", nameof(uvs));
        if (indices.Length % 3 != 0)
            throw new ArgumentException("The index count must be a multiple of 3 (triangle list).", nameof(indices));

        bool ok = InternalCalls.Mesh_SetData(UUID,
            MemoryMarshal.Cast<Vector3, float>(positions),
            MemoryMarshal.Cast<Vector3, float>(normals),
            MemoryMarshal.Cast<Vector2, float>(uvs),
            positions.Length,
            MemoryMarshal.Cast<int, uint>(indices));
        if (ok) return;
        if (!IsRuntime)
            throw new InvalidOperationException("Only meshes made with Mesh.Create() can be changed, and only until they are destroyed.");
        throw new ArgumentException($"An index is outside the {positions.Length} vertices.", nameof(indices));
    }

    /// <summary>Removes all geometry; renderers using the mesh draw nothing until the next <see cref="SetData"/>.</summary>
    public void Clear() => SetData(ReadOnlySpan<Vector3>.Empty, ReadOnlySpan<int>.Empty);

    /// <summary>Frees a runtime mesh. Renderers still referencing it draw nothing. No effect on mesh assets.</summary>
    public void Destroy() => InternalCalls.Mesh_DestroyRuntime(UUID);

    private float[] Read()
    {
        var values = new float[InternalCalls.MeshInfoValueCount];
        InternalCalls.Mesh_GetInfo(UUID, values);
        return values;
    }

    public bool Equals(Mesh? other) => other is not null && other.UUID == UUID;
    public override bool Equals(object? obj) => obj is Mesh other && Equals(other);
    public override int GetHashCode() => UUID.GetHashCode();
    public static bool operator ==(Mesh? a, Mesh? b) => a is null ? b is null : a.Equals(b);
    public static bool operator !=(Mesh? a, Mesh? b) => !(a == b);
}
