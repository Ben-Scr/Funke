"""Generate typed Bolt adapters for the native services used by gameplay wrappers."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Funke-BoltScriptCore/Source/Funke"
OUTPUT = SOURCE / "Interop/FkService.bolt"
TYPES = {"uint64_t": "ulong", "uint32_t": "uint", "int": "int", "int32_t": "int",
         "uint8_t": "byte", "float": "float", "double": "double", "void": "void", "int64_t": "long"}
PACK = {"ulong": "{}", "long": "Reinterpret<ulong>({})", "int": "(ulong)Reinterpret<uint>({})",
        "uint": "(ulong){}", "byte": "(ulong){}", "float": "(ulong)Reinterpret<uint>({})",
        "double": "Reinterpret<ulong>({})", "void*": "FkNativeSlot.Pointer({})"}
UNPACK = {"ulong": "{}", "long": "Reinterpret<long>({})", "int": "Reinterpret<int>((uint)({} & 4294967295ul))",
          "uint": "(uint){}", "byte": "(byte){}", "float": "Reinterpret<float>((uint){})", "double": "Reinterpret<double>({})"}


def generate():
    api = (ROOT / "Funke-Engine/src/Scripting/ScriptGlue.hpp").read_text(encoding="utf-8")
    api = re.sub(r"//[^\n]*|/\*.*?\*/", "", api, flags=re.S)
    slots = {name: (ret, args) for ret, name, args in re.findall(
        r"\b(\w+)\s*\(\s*\*(\w+)\)\(([^()]*?)\);", api)}
    used = set()
    for path in SOURCE.rglob("*.bolt"):
        if path != OUTPUT:
            used.update(re.findall(r"\bFkService\.(\w+)\(", path.read_text(encoding="utf-8-sig")))
    lines = ["import System", "namespace Funke;", "", "public static class FkService {", "public:",
             "    static byte[] CString(string text) {",
             "        byte[] bytes = text.ToUtf8();", "        byte[] result = new byte[bytes.Length + 1];",
             "        for (int i = 0; i < bytes.Length; i++) result[i] = bytes[i];", "        return result;", "    }",
             "    static string ReadText(byte[] buffer, int length) {",
             "        byte[] bytes = new byte[length];",
             "        for (int i = 0; i < length; i++) bytes[i] = buffer[i];",
             "        return string.FromUtf8(bytes);", "    }"]
    for name in sorted(used - {"CString", "ReadText"}):
        ret, arguments = slots[name]
        params, prelude, values = [], [], []
        for index, arg in enumerate(arguments.split(",") if arguments.strip() else ()):
            match = re.fullmatch(r"\s*(const\s+)?(\w+)\s*(\*)?\s*(\w+)\s*", arg)
            if not match:
                raise ValueError(f"Unsupported {name} parameter: {arg}")
            const, raw, ptr, _ = match.groups()
            typ = "string" if const and raw == "char" and ptr else "void*" if ptr else TYPES[raw]
            param = f"a{index}"
            params.append(f"{typ} {param}")
            if typ == "string":
                prelude.append(f"        byte[] bytes{index} = CString({param});")
                values.append(f"FkNativeSlot.Pointer(&bytes{index}[0])")
            else:
                values.append(PACK[typ].format(param))
        lines.append(f"    static {TYPES[ret]} {name}({', '.join(params)}) {{")
        lines.extend(prelude)
        if values: lines.append("        ulong[] words = {" + ", ".join(values) + "};")
        call = f"FkNativeInvoke(FkNativeSlot.{name}, " + ("&words[0]" if values else "null") + ", null)"
        lines.append("        " + (call if ret == "void" else "return " + UNPACK[TYPES[ret]].format(call)) + ";")
        lines.append("    }")
    result = "\n".join([*lines, "}", ""])
    if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != result:
        OUTPUT.write_text(result, encoding="utf-8")


if __name__ == "__main__":
    generate()

