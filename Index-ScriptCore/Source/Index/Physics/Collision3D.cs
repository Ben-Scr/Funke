namespace Index;

public readonly struct Collision3D
{
    internal Collision3D(ulong selfEntityId, ulong otherEntityId, Vector3 contactPoint, Vector3 normal, bool isTrigger)
    {
        Self = new Entity(selfEntityId);
        Other = new Entity(otherEntityId);
        ContactPoint = contactPoint;
        Normal = normal;
        IsTrigger = isTrigger;
    }

    /// <summary>The entity owning this script's body (the Rigidbody3D entity, or the static collider's entity).</summary>
    public Entity Self { get; }
    public Entity Other { get; }
    /// <summary>World-space contact point when the contact began, refreshed while it persists.</summary>
    public Vector3 ContactPoint { get; }
    /// <summary>Contact normal pointing from <see cref="Other"/> toward <see cref="Self"/> (e.g. Y > 0.7 means standing on top).</summary>
    public Vector3 Normal { get; }
    public bool IsTrigger { get; }
}
