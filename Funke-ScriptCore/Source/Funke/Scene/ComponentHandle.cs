using System;
using Funke.Components;

namespace Funke;

/// <summary>Scene-thread, checked value access. Reacquire after any structural scene change or component schema reload.
/// Reads and writes copy the validated unmanaged payload; no pointer or ref escapes.</summary>
public readonly struct ComponentHandle<T> where T : unmanaged, IComponent
{
    private readonly ulong entityId;
    private readonly ulong scene, entityGeneration, revision, schema;

    private unsafe ComponentHandle(ulong id, ulong* identity)
    {
        entityId = id;
        scene = identity[0]; entityGeneration = identity[1]; revision = identity[2]; schema = identity[3];
    }

    internal static unsafe bool TryCreate(ulong id, out ComponentHandle<T> handle)
    {
        handle = default;
        if (id == 0) return false;
        ulong* identity = stackalloc ulong[4];
        if (!InternalCalls.Entity_CaptureComponent(id, ComponentTypes<T>.NativeName, identity)) return false;
        handle = new ComponentHandle<T>(id, identity);
        return true;
    }

    public unsafe bool TryRead(out T value)
    {
        T copy = default;
        bool ok = Transfer(&copy, false);
        value = copy;
        return ok;
    }

    public T Read() => TryRead(out T value) ? value : throw new InvalidOperationException("Component handle is stale or unavailable. Reacquire it from the entity.");

    public unsafe bool TryWrite(in T value)
    {
        T copy = value;
        return Transfer(&copy, true);
    }

    public void Write(in T value)
    {
        if (!TryWrite(value)) throw new InvalidOperationException("Component handle is stale or unavailable. Reacquire it from the entity.");
    }

    private unsafe bool Transfer(T* value, bool write)
    {
        if (scene == 0) return false;
        ulong* identity = stackalloc ulong[4] { scene, entityGeneration, revision, schema };
        return InternalCalls.Entity_TransferComponent(entityId, ComponentTypes<T>.NativeName, identity, value, sizeof(T), write);
    }
}
