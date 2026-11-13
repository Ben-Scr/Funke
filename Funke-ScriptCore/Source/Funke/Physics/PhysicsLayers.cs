namespace Funke;

/// <summary>Helpers for collider layer bitmasks (<see cref="Collider2D.Layer"/>, <see cref="Collider2D.CollisionMask"/>, query layer masks).</summary>
public static class PhysicsLayers
{
    /// <summary>Gets a configured layer name from its index (not its mask).</summary>
    public static string GetName(uint index) => PhysicsLayer.GetName(index);
    /// <summary>Gets a layer index from its case-sensitive project name.</summary>
    public static uint GetIndex(string name) => PhysicsLayer.GetIndex(name);
    public static bool TryGetIndex(string? name, out uint index) => PhysicsLayer.TryGetIndex(name, out index);
    public static PhysicsLayer FromName(string name) => PhysicsLayer.FromName(name);

    public const ulong All = ulong.MaxValue;
    public const ulong None = 0;
    /// <summary>Layer 0 — what new colliders start on.</summary>
    public const ulong Default = 1;

    /// <summary>Combines layer indices (0-63) into a mask, e.g. <c>Mask(0, 3)</c>.</summary>
    public static ulong Mask(params int[] layers)
    {
        ulong mask = 0;
        foreach (int layer in layers)
        {
            if (layer < 0 || layer > 63) throw new ArgumentOutOfRangeException(nameof(layers), layer, "Layer index must be 0-63.");
            mask |= 1UL << layer;
        }
        return mask;
    }

    public static bool Contains(ulong mask, int layer) => layer is >= 0 and <= 63 && (mask & (1UL << layer)) != 0;
}
