"""Generate the internal native-slot allowlist from the shipped Bolt modules."""
import re


def generate(base, modules, glue, save):
    available = set(re.findall(r"\(\*(\w+)\)", glue.split("struct ManagedCallbacks")[0]))
    used = set()
    for files in modules.values():
        for name in files:
            if name != "Interop/NativeSlots.bolt":
                path = base / name
                used.update(re.findall(r"\bFkNativeSlot\.(\w+)", path.read_text(encoding="utf-8-sig")))
    used.discard("Pointer")
    unknown = used - available
    if unknown:
        raise ValueError("Unregistered ScriptGlue.hpp slots: " + ", ".join(sorted(unknown)))
    slots = {}
    for name in sorted(used):
        value = 2166136261
        for byte in name.encode("ascii"):
            value = ((value ^ byte) * 16777619) & 0xffffffff
        if value in slots:
            raise ValueError(f"Native slot hash collision: {name} and {slots[value]}")
        slots[value] = name
    save("ServiceSlots.inc", "".join(f"FUNKE_BOLT_SERVICE({value}u, {name})\n" for value, name in slots.items()))
    constants = "".join(f"    static const ulong {name} = {value}ul;\n" for value, name in slots.items())
    save(base / "Interop/NativeSlots.bolt", """namespace Funke;

public static class FkNativeSlot {
public:
    static ulong Pointer(void* value) {
        ulong address = 0ul;
        *((void**)&address) = value;
        return address;
    }
""" + constants + "}\n")

