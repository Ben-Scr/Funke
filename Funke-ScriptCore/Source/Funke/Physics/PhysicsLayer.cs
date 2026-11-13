using System;
using System.Collections.Generic;
using System.Numerics;
using Funke.Interop;

namespace Funke;

/// <summary>
/// A set of physics layers. Indices (0-63) identify layers; <see cref="Mask"/> contains their bits.
/// Combine named layers with | and use the result for Entity.Layer or physics queries.
/// Names are configured in Project Settings > Physics and are case-sensitive.
/// </summary>
public readonly struct PhysicsLayer : IEquatable<PhysicsLayer>
{
    public const uint Count = 64;
    public ulong Mask { get; }
    public static PhysicsLayer None => default;
    public static PhysicsLayer All => FromMask(ulong.MaxValue);
    public static PhysicsLayer Default => FromIndex(0);
    public bool IsEmpty => Mask == 0;
    public bool IsSingleLayer => BitOperations.IsPow2(Mask);

    /// <summary>The index of a single layer. Throws for None or a combined mask.</summary>
    public uint Index => IsSingleLayer ? (uint)BitOperations.TrailingZeroCount(Mask)
        : throw new InvalidOperationException("A combined or empty PhysicsLayer has no single index. Use Mask or GetNames().");
    /// <summary>The current project name of a single layer. Throws for a combined or empty mask.</summary>
    public string Name => GetName(Index);

    /// <summary>Creates a single layer from its index, not from mask bits.</summary>
    public PhysicsLayer(uint index)
    {
        if (index >= Count) throw new ArgumentOutOfRangeException(nameof(index), "Layer index must be 0-63.");
        Mask = 1UL << (int)index;
    }

    /// <summary>Looks up a project layer by name. Throws if it does not exist.</summary>
    public PhysicsLayer(string name) : this(GetIndex(name)) { }
    private PhysicsLayer(ulong mask, bool _) => Mask = mask;

    public static PhysicsLayer FromIndex(uint index) => new(index);
    public static PhysicsLayer FromName(string name) => new(name);
    /// <summary>Wraps existing mask bits without interpreting them as an index.</summary>
    public static PhysicsLayer FromMask(ulong mask) => new(mask, true);

    public static string GetName(uint index)
    {
        if (index >= Count) throw new ArgumentOutOfRangeException(nameof(index), "Layer index must be 0-63.");
        return InternalCalls.PhysicsLayer_GetName(index);
    }

    public static uint GetIndex(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        if (!TryGetIndex(name, out uint index))
            throw new ArgumentException($"Unknown physics layer '{name}'. Create it in Project Settings > Physics.", nameof(name));
        return index;
    }

    public static bool TryGetIndex(string? name, out uint index)
    {
        index = default;
        if (string.IsNullOrWhiteSpace(name) || name.Contains('\0')) return false;
        int result = InternalCalls.PhysicsLayer_GetIndex(name);
        if (result < 0 || result >= Count) return false;
        index = (uint)result;
        return true;
    }

    public static bool TryFromName(string? name, out PhysicsLayer layer)
    {
        bool found = TryGetIndex(name, out uint index);
        layer = found ? FromIndex(index) : None;
        return found;
    }

    public static PhysicsLayer FromNames(params string[] names)
    {
        ArgumentNullException.ThrowIfNull(names);
        PhysicsLayer result = None;
        foreach (string name in names) result |= FromName(name);
        return result;
    }

    public static PhysicsLayer FromIndices(params uint[] indices)
    {
        ArgumentNullException.ThrowIfNull(indices);
        PhysicsLayer result = None;
        foreach (uint index in indices) result |= FromIndex(index);
        return result;
    }

    /// <summary>True if every layer in the supplied mask is included (None is always included).</summary>
    public bool Contains(PhysicsLayer layers) => (Mask & layers.Mask) == layers.Mask;
    public bool Contains(uint index) => Contains(FromIndex(index));
    public bool Intersects(PhysicsLayer layers) => (Mask & layers.Mask) != 0;
    public PhysicsLayer With(PhysicsLayer layers) => this | layers;
    public PhysicsLayer Without(PhysicsLayer layers) => this & ~layers;

    public string[] GetNames()
    {
        var names = new List<string>();
        for (uint i = 0; i < Count; ++i)
            if (Contains(i)) names.Add(GetName(i));
        return names.ToArray();
    }

    public bool Equals(PhysicsLayer other) => Mask == other.Mask;
    public override bool Equals(object? obj) => obj is PhysicsLayer other && Equals(other);
    public override int GetHashCode() => Mask.GetHashCode();
    public override string ToString() => IsEmpty ? "None" : Mask == ulong.MaxValue ? "All" : string.Join(", ", GetNames());
    public static PhysicsLayer operator |(PhysicsLayer a, PhysicsLayer b) => FromMask(a.Mask | b.Mask);
    public static PhysicsLayer operator &(PhysicsLayer a, PhysicsLayer b) => FromMask(a.Mask & b.Mask);
    public static PhysicsLayer operator ^(PhysicsLayer a, PhysicsLayer b) => FromMask(a.Mask ^ b.Mask);
    public static PhysicsLayer operator ~(PhysicsLayer layer) => FromMask(~layer.Mask);
    public static bool operator ==(PhysicsLayer a, PhysicsLayer b) => a.Equals(b);
    public static bool operator !=(PhysicsLayer a, PhysicsLayer b) => !a.Equals(b);
    /// <summary>Interoperates with existing ulong collider/query APIs. Use FromMask for the reverse conversion.</summary>
    public static implicit operator ulong(PhysicsLayer layer) => layer.Mask;
    public static explicit operator PhysicsLayer(ulong mask) => FromMask(mask);
}
