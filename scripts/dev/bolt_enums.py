"""Mirror remaining top-level public enums from the ScriptCore sources."""
import re
from collections import defaultdict
from pathlib import Path

MODULES = {"Funke": "Core", "Funke.UI": "UI", "Funke.Components": "ComponentNamespace",
           "Funke.Jobs": "Jobs", "Funke.Coroutines": "Coroutines"}


def extend(core, base, modules, save):
    for files in modules.values():
        files[:] = [name for name in files if Path(name).name != "ScriptEnums.bolt"]
    existing = set()
    for files in modules.values():
        for name in files:
            path = base / name
            if path.name == "ScriptEnums.bolt" or not path.exists():
                continue
            text = path.read_text(encoding="utf-8-sig")
            namespace = re.search(r"\bnamespace\s+([\w.]+);", text)[1]
            for match in re.finditer(r"\b(?:enum|struct|class)\s+(\w+)", text):
                existing.add((namespace, match[1]))
    additions = defaultdict(list)
    for path in sorted(core.rglob("*.cs")):
        text = re.sub(r"//[^\n]*|/\*.*?\*/", "", path.read_text(encoding="utf-8-sig"), flags=re.S)
        for match in re.finditer(r"^public enum (\w+)(?:\s*:\s*(\w+))?\s*\{([^}]+)\}", text, re.M):
            namespace = re.search(r"\bnamespace\s+([\w.]+)\s*[;{]", text)[1]
            if (namespace, match[1]) not in existing:
                members = [item.strip() for item in match[3].split(",") if item.strip()]
                members = [re.sub(r"1[Uu][Ll] << (\d+)", lambda m: str(1 << int(m[1])) + "ul", s) for s in members]
                storage = " : " + match[2] if match[2] else ""
                additions[namespace].append(f"public enum {match[1]}{storage} {{\n    " + ",\n    ".join(members) + "\n}")
    for namespace, bodies in additions.items():
        module = MODULES.get(namespace, namespace.removeprefix("Funke.").replace(".", ""))
        filename = f"{module}/ScriptEnums.bolt"
        path = base / filename
        path.parent.mkdir(parents=True, exist_ok=True)
        save(path, f"namespace {namespace};\n\n" + "\n\n".join(bodies) + "\n")
        files = modules.setdefault(module, [])
        if filename not in files:
            files.append(filename)

