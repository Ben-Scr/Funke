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
