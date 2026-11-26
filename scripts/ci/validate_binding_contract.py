"""Validate the append-only Physics3D interop area against its declaration contract."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[2]


def declarations(native, managed):
    native = native.split('struct NativeBindings', 1)[1].split('struct ManagedCallbacks', 1)[0]
    cpp = [(m[2], re.sub(r'\s+', '', m[0])) for m in re.finditer(
        r'([\w:* ]+)\(\*(\w+)\)\s*\(([^;]+?)?\);', native)]
    cs = [(m[2], re.sub(r'\s+', '', m[0])) for m in re.finditer(
        r'public delegate\* unmanaged<([^;]+)>\s+(\w+);', managed)]
    return cpp, cs


def validate(native, managed, module_slots, handshake, contract):
    cpp, cs = declarations(native, managed)
    errors = []
    for slot in contract['slots']:
        for language, actual in [('native', cpp), ('managed', cs)]:
            index = slot['index']
            if index >= len(actual) or actual[index] != (slot['name'], slot[language]):
                errors.append(f"{slot['name']}: {language} signature/order mismatch at slot {index}")
        if f"X({slot['name']})" not in module_slots:
            errors.append(f"{slot['name']}: missing module stub/registration slot")
    native_version = re.search(r'k_ScriptInteropLayoutVersion\s*=\s*(\d+)', native)
    managed_version = re.search(r'LayoutVersion\s*=\s*(\d+)', handshake)
    if not native_version or not managed_version or native_version[1] != managed_version[1]:
        errors.append('Interop layout versions disagree')
    return errors


def inputs():
    return [(ROOT / p).read_text(encoding='utf-8-sig') for p in [
        'Funke-Engine/src/Scripting/ScriptGlue.hpp',
        'Funke-ScriptCore/Source/Funke/Interop/NativeCallbacks.cs',
        'Funke-Engine/src/Scripting/ScriptBindingModuleSlots.hpp',
        'Funke-ScriptCore/Source/Funke/Interop/ScriptHostBridge.cs']]


def contract():
    return json.loads((ROOT / 'scripts/ci/contracts/physics3d-bindings.json').read_text(encoding='utf-8'))


if __name__ == '__main__':
    errors = validate(*inputs(), contract())
    if errors:
        raise SystemExit('\n'.join(errors))
    print('Physics3D signatures, slot positions, module membership and layout versions match.')
