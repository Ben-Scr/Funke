using System;
using Index.Interop;

namespace Index;
public abstract class Component
{
    public Entity Entity { get; internal set; } = Entity.Invalid;

    protected ulong RequireComponent<TComponent>() where TComponent : Component, new()
    {
        if (Entity == null || Entity == Entity.Invalid)
            throw new InvalidOperationException($"{typeof(TComponent).Name} is no longer attached to a live entity.");

        // Runs on every property access: HasComponent alone is false for dead entities, so IsValid is only needed to pick the message.
        if (!Entity.HasComponent<TComponent>())
        {
            if (!InternalCalls.Entity_IsValid(Entity.ID))
                throw new InvalidOperationException($"{typeof(TComponent).Name} is no longer attached to a live entity.");
            throw new InvalidOperationException($"{typeof(TComponent).Name} is no longer attached to entity {Entity.ID}.");
        }

        return Entity.ID;
    }

    internal void Invalidate() => Entity = Entity.Invalid;
}
