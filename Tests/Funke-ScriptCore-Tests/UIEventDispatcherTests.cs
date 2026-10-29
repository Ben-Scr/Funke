using System;
using System.Runtime.InteropServices;
using Funke;
using Funke.Interop;
using Funke.UI;
using Xunit;

namespace FunkeScriptCoreTests;

[Collection(NativeCallbacksCollection.Name)]
public unsafe class UIEventDispatcherTests
{
    private const ulong k_First = 9_100_000;
    private const int k_Count = 200;
    private static ulong s_Hovered;

    [UnmanagedCallersOnly]
    private static int QueryEntities(byte* names, ulong* ids, int capacity)
    {
        // Compared as bytes: a decoded string would allocate inside the measured ticks.
        if (!MemoryMarshal.CreateReadOnlySpanFromNullTerminated(names).SequenceEqual("Interactable"u8)) return 0;
        for (int i = 0; i < Math.Min(capacity, k_Count); i++) ids[i] = k_First + (ulong)i;
        return k_Count;
    }

    [UnmanagedCallersOnly]
    private static int IsHovered(ulong id) => id == s_Hovered ? 1 : 0;

    [UnmanagedCallersOnly]
    private static int Never(ulong id) => 0;

    [UnmanagedCallersOnly]
    private static int HasComponent(ulong id, byte* name) => 1;

    [UnmanagedCallersOnly]
    private static int HasComponentByTypeId(ulong id, uint typeId) => typeId == 1 ? 1 : 0;

    [UnmanagedCallersOnly]
    private static uint GetTypeId(byte* name)
        => MemoryMarshal.CreateReadOnlySpanFromNullTerminated(name).SequenceEqual("Interactable"u8) ? 1u : 0u;

    private static void WithWidgets(Action body)
    {
        NativeBindingsStruct previous = NativeCallbacks.Bindings;
        Entity.ClearManagedComponentStore();
        try
        {
            NativeBindingsStruct bindings = previous;
            bindings.Scene_QueryEntities = &QueryEntities;
            bindings.Interactable_GetIsHovered = &IsHovered;
            bindings.Interactable_GetIsClicked = &Never;
            bindings.Interactable_GetIsPressed = &Never;
            bindings.Interactable_GetIsMouseDown = &Never;
            bindings.Interactable_GetIsMouseUp = &Never;
            bindings.Interactable_GetIsFocused = &Never;
            bindings.Entity_HasComponent = &HasComponent;
            bindings.Entity_HasComponentByTypeId = &HasComponentByTypeId;
            bindings.Component_GetTypeId = &GetTypeId;
            NativeCallbacks.Bindings = bindings;
            body();
        }
        finally
        {
            Entity.ClearManagedComponentStore();
            NativeCallbacks.Bindings = previous;
            s_Hovered = 0;
        }
    }

    [Fact]
    public void IdleWidgetsAllocateNothing()
    {
        WithWidgets(() =>
        {
            // The first tick grows the query buffer and caches the query names.
            UIEventDispatcher.Tick();
            long before = GC.GetAllocatedBytesForCurrentThread();
            for (int i = 0; i < 10; i++) UIEventDispatcher.Tick();
            Assert.Equal(0, GC.GetAllocatedBytesForCurrentThread() - before);
        });
    }

    [Fact]
    public void HoverIsRaisedOnceWhenItStarts()
    {
        WithWidgets(() =>
        {
            ulong id = k_First + 7;
            Interactable interactable = new Entity(id).GetComponent<Interactable>()!;
            int hovers = 0;
            interactable.Hovered += _ => hovers++;

            s_Hovered = id;
            UIEventDispatcher.Tick();
            UIEventDispatcher.Tick();
            Assert.Equal(1, hovers);

            s_Hovered = 0;
            UIEventDispatcher.Tick();
            s_Hovered = id;
            UIEventDispatcher.Tick();
            Assert.Equal(2, hovers);
        });
    }
}
