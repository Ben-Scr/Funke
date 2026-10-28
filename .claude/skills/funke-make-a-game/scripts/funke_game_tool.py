#!/usr/bin/env python3
"""Authoring helper for Funke game projects (used by the funke-make-a-game skill).

Subcommands:
  guid      print fresh random ids for entity uuids / asset GUIDs
  meta      create .meta sidecars (with a GUID) for asset files
  list      list every asset under Assets/ with its GUID
  builtin   compute the GUID of a file inside the engine's FunkeAssets/ folder
  validate  statically check project.json, metas, scenes, prefabs, data assets, scripts
  smoke     run Funke-Runtime on the project in the background with an isolated prefs profile (--ci-smoke-test,
            --quit-after or an input script; optional --scene/--screenshot-at/--exec) and check the log
  build     run Funke-Editor --build (the real Build-panel pipeline) and report the result

Mirrors the loader rules in Funke-Engine/src/Serialization/SceneSerializerDeserialize.cpp and
Funke-Engine/src/Assets/AssetRegistry.hpp. Standard library only.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import secrets
import subprocess
import sys
from pathlib import Path

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

BINARY_MAGIC = b"FNKSCNB\x1a"
MAX_PROJECT_GUID = (1 << 63) - 1
DEFAULT_FONT_GUID = 0xAB00000000000001

KIND_BY_EXT = {
    **{e: "Texture" for e in (".png", ".jpg", ".jpeg", ".bmp", ".tga")},
    **{e: "Audio" for e in (".wav", ".mp3", ".ogg", ".flac")},
    ".scene": "Scene",
    ".prefab": "Prefab",
    **{e: "Script" for e in (".cs", ".cpp", ".c", ".hpp", ".h")},
    **{e: "Font" for e in (".ttf", ".otf")},
    **{e: "Shader" for e in (".wgsl", ".vs", ".fs", ".vert", ".frag", ".glsl")},
    ".dataasset": "DataAsset",
    ".material": "Material",
    **{e: "Mesh" for e in (".obj", ".gltf", ".glb")},
    ".anim": "AnimationClip",
    ".animctrl": "AnimatorController",
}

BUILTIN_TEXTURE_FILES = ["Square", "Pixel", "circle", "Capsule", "IsometricDiamond",
                         "HexagonFlatTop", "HexagonPointedTop", "9Sliced", "Invisible"]

EXPECTED_FOLDER = {"Texture": "Textures", "Audio": "Audio", "Font": "Fonts", "Scene": "Scenes",
                   "Prefab": "Prefabs", "DataAsset": "Data", "Script": "Scripts"}

RENDERERS = ("SpriteRenderer", "Image", "ParticleSystem2D", "TextRenderer")
BOX2D_COLLIDERS = ("BoxCollider2D", "CircleCollider2D", "PolygonCollider2D")
FAST_PHYSICS = ("FastBody2D", "FastBoxCollider2D", "FastCircleCollider2D")
LAYOUT_GROUPS = ("HorizontalLayoutGroup", "VerticalLayoutGroup", "GridLayoutGroup")
UI_WIDGETS = ("Button", "Slider", "Toggle", "InputField", "Dropdown", "Scrollbar", "CircularSlider")
UI_ENTITY_REF_KEYS = {
    "Button": ("targetGraphic",),
    "Slider": ("backgroundEntity", "fillEntity", "handleEntity", "labelEntity"),
    "Toggle": ("checkmarkEntity",),
    "InputField": ("textEntity", "verticalScrollbarEntity"),
    "Dropdown": ("labelEntity",),
    "Scrollbar": ("handleEntity",),
    "CircularSlider": ("handleEntity",),
    "ScrollRect": ("content", "viewport", "horizontalScrollbar", "verticalScrollbar"),
}
UI_EVENT_KEYS = ("onClick", "onValueChanged", "onSubmitted")
UI_SPRITE_KEYS = ("normalSprite", "hoveredSprite", "pressedSprite", "disabledSprite", "focusedSprite")
BOOL_KEYS = {"sensor", "registerContacts", "loop", "playOnAwake", "freezeRotation", "freezePositionX",
             "freezePositionY", "massOverridden", "useGravity", "boundaryCheck", "postProcessing",
             "interactable", "focusable", "autoSize", "isOn", "wholeNumbers", "isReadOnly", "playOnStart",
             "disabled", "static", "deadly"}
ENTITY_TAG_KEYS = {"disabled", "static", "deadly"}  # entity-level; "disabled" inside UI widgets is a color

# Script source patterns that break the skill's house rules (warnings only).
RULE_PATTERNS = [
    (re.compile(r"\bAssetManager\.Load(ByGUID)?\b|\.FromAssetUUID\s*\("),
     "loads an asset in code - wire it as a serialized field in the scene/prefab JSON instead (rule 3)"),
    (re.compile(r"\bFind(Entity)?ByName\s*\("),
     "looks an entity up by name - prefer a serialized Entity/component field (rule 3)"),
    (re.compile(r"\bEntity\.Create(With)?\b|\bCreate(With)?\s*\(\s*\"|\bAddComponent\s*<"),
     "creates entities/components at runtime - authored content belongs in JSON; runtime spawning should Instantiate a prefab (rule 1)"),
    (re.compile(r"\bFile\.(Write|Read)All(Text|Bytes|Lines)\b|\bJsonSerializer\.(Serialize|Deserialize)\b"),
     "reads/writes its own files - settings/progress belong in PlayerPrefs or SaveData.Save<T>(slot, obj) (rule 4)"),
    (re.compile(r"\b\d{15,20}(UL|ul|L)?\b"),
     "hard-coded GUID literal - wire the asset as a serialized field in JSON (rule 3)"),
    (re.compile(r"\bconst\s+(float|int|double)\s+\w*(Speed|Damage|Health|Hp|Gravity|Jump\w*|Force|Cooldown|Cost|Price|Range|Radius|Interval|Delay|Duration|Reward|Lives)\b"),
     "tuning value as const - make it a public field (script or Data Asset) so it can be tuned in the Inspector (rule 2)"),
]

NUMBERED_FIELD_RE = re.compile(r"^\s*public\s+[\w<>?\[\]]+\s+([A-Za-z_]\w*?)([1-9])\s*[;=]", re.M)


def numbered_fields(src: str) -> list[str]:
    """Page1..Page5-style fields: three or more public fields that differ only by a trailing digit."""
    groups: dict[str, set[str]] = {}
    for m in NUMBERED_FIELD_RE.finditer(src):
        groups.setdefault(m.group(1), set()).add(m.group(2))
    return [f"{stem}1..{stem}{max(d)}" for stem, d in groups.items() if len(d) >= 3]

INITIALIZER_RE = re.compile(r"(?:static|readonly|const)[^;{=()]*=\s*(?:new\s*[\w<>\[\],. ?]*)?\s*\{")
ENTRY_RE = re.compile(r"new\s+\w+\s*[({]|\(\s*\"|\{\s*\"[^\"]*\"\s*,")
SWITCH_LITERAL_RE = re.compile(r"(?:case\s+[^:]+:\s*return|=>)\s*(?:-?\d|\")")


def _block(src: str, open_idx: int) -> str:
    depth = 0
    for i in range(open_idx, min(len(src), open_idx + 20000)):
        c = src[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return src[open_idx:i + 1]
    return src[open_idx:open_idx + 2000]


def content_tables(src: str) -> list[str]:
    """Hard-coded content in C#: static tables of entries, or switch ladders returning literals (rule 2)."""
    found = []
    for m in INITIALIZER_RE.finditer(src):
        block = _block(src, m.end() - 1)
        entries = len(ENTRY_RE.findall(block))
        if entries >= 4 and re.search(r"\d", block):
            line = src.count("\n", 0, m.start()) + 1
            found.append(f"line {line}: table of {entries} entries in code")
    for m in re.finditer(r"\bswitch\b[^{;]*\{", src):
        block = _block(src, m.end() - 1)
        hits = len(SWITCH_LITERAL_RE.findall(block))
        if hits >= 4:
            line = src.count("\n", 0, m.start()) + 1
            found.append(f"line {line}: switch with {hits} literal results")
    return found


# ----------------------------------------------------------------------------- helpers

def new_guid(taken: set[int] | None = None) -> int:
    while True:
        g = secrets.randbelow(MAX_PROJECT_GUID) + 1
        if not taken or g not in taken:
            if taken is not None:
                taken.add(g)
            return g


def builtin_guid(relative_path: str) -> int:
    """FNV-1a 64 of the lowercased forward-slash path relative to FunkeAssets, top byte 0xAA."""
    h = 0xCBF29CE484222325
    for b in relative_path.replace("\\", "/").lower().encode("utf-8"):
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return (h & 0x00FFFFFFFFFFFFFF) | 0xAA00000000000000


def classify(path: Path) -> str:
    ext = path.suffix.lower()
    if not ext:
        return "Unknown"
    return KIND_BY_EXT.get(ext, "Other")


def parse_u64(value) -> int | None:
    """Mirrors the engine: decimal digit string or JSON integer. Returns None when invalid/zero."""
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value if 0 < value < (1 << 64) else None
    if isinstance(value, str) and value.isdigit():
        v = int(value)
        return v if 0 < v < (1 << 64) else None
    return None


def read_meta_guid(meta_path: Path) -> tuple[int | None, str | None]:
    try:
        data = json.loads(meta_path.read_text(encoding="utf-8-sig"))
    except Exception as exc:  # noqa: BLE001
        return None, f"invalid JSON ({exc}) - the engine will silently replace it with a random GUID"
    if not isinstance(data, dict):
        return None, "root is not an object"
    raw = data.get("AssetGUID", data.get("uuid"))
    guid = parse_u64(raw)
    if guid is None:
        return None, f"missing/invalid AssetGUID {raw!r} (must be a non-zero decimal string)"
    if isinstance(raw, str) and str(data.get("uuid", raw)) != raw:
        return guid, "AssetGUID and uuid differ"
    return guid, None


def write_meta(asset: Path, guid: int) -> Path:
    meta = asset.with_name(asset.name + ".meta")
    payload = {"AssetGUID": str(guid), "uuid": str(guid), "kind": classify(asset)}
    meta.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return meta


# ----------------------------------------------------------------------------- data assets (import from generators)

def encode_value(v) -> str:
    """One Data Asset / ScriptFields value as the engine stores it: always a string.
    bool -> "true"/"false"; int/float -> invariant number; tuple -> "x,y[,z,w]" (vectors, colours);
    None -> ""; str -> as is (GUIDs as decimal strings, "prefab:<guid>" for prefab fields); list -> encode_list."""
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        return repr(v) if v != int(v) else str(int(v))
    if isinstance(v, tuple):
        return ",".join(encode_value(x) for x in v)
    if isinstance(v, list):
        return encode_list(v)
    if v is None:
        return ""
    return str(v)


def encode_list(items: list) -> str:
    """List<T> / T[] field: every element prefixed with a newline; backslash and newline inside an element escaped."""
    out = []
    for item in items:
        text = encode_value(item)
        out.append("\n" + text.replace("\\", "\\\\").replace("\n", "\\n"))
    return "".join(out)


def encode_objects(rows: list[list]) -> str:
    """List<PlainClass> field (inline objects): one row per element holding its leaf fields IN DECLARATION ORDER
    (scalars, enums as ints, asset/data-asset GUIDs). Nested lists inside an element are not supported."""
    return encode_list([encode_list(row) for row in rows])


def write_dataasset(path: Path, type_name: str, fields: dict, guid: int | None = None,
                    overwrite: bool = False) -> int:
    """Write <path>.dataasset (+ .meta first) with every value encoded as a string. Returns the GUID.
    overwrite=False keeps an existing file untouched: once someone tunes it in the Inspector, the file wins."""
    path = Path(path)
    meta = path.with_name(path.name + ".meta")
    existing, _ = read_meta_guid(meta) if meta.is_file() else (None, None)
    g = existing or guid or new_guid()
    if not existing:
        write_meta(path, g)
    if path.exists() and not overwrite:
        return g
    payload = {"version": 1, "type": type_name, "fields": {k: encode_value(v) for k, v in fields.items()}}
    data = (json.dumps(payload, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    if not path.exists() or path.read_bytes() != data:
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_name(path.name + ".tmp")
        tmp.write_bytes(data)
        os.replace(tmp, path)
    return g


def find_project_root(start: Path) -> Path | None:
    for p in [start, *start.parents]:
        if (p / "project.json").is_file():
            return p
    return None


def engine_root_from_skill() -> Path | None:
    # <engine>/.claude/skills/funke-make-a-game/scripts/funke_game_tool.py
    here = Path(__file__).resolve()
    for p in here.parents:
        if (p / "Funke-Engine").is_dir() and (p / "Funke-ScriptCore").is_dir():
            return p
    return None


def funke_assets_dirs(project: Path | None) -> list[Path]:
    dirs = []
    if project and (project / "FunkeAssets").is_dir():
        dirs.append(project / "FunkeAssets")
    eng = engine_root_from_skill()
    if eng:
        cands = [eng / "Funke-Runtime" / "FunkeAssets", eng / "Funke-Editor" / "FunkeAssets"]
        # built binaries carry the full shipped set even when the source folder is mid-change
        for cfg in ("Release", "Debug", "Dist"):
            for app in ("Funke-Runtime", "Funke-Editor"):
                cands.append(eng / "bin" / f"{cfg}-windows-x86_64" / app / "FunkeAssets")
        dirs += [c for c in cands if c.is_dir()]
    return dirs


def builtin_assets(project: Path | None) -> dict[int, tuple[str, str]]:
    result: dict[int, tuple[str, str]] = {DEFAULT_FONT_GUID: ("Font", "Fonts/IBMPlexSansCondensed-Regular.ttf (default font)")}
    for name in BUILTIN_TEXTURE_FILES:
        rel = f"Textures/Default/{name}.png"
        result[builtin_guid(rel)] = ("Texture", rel)
    for root in funke_assets_dirs(project):
        for f in root.rglob("*"):
            if f.is_file() and f.suffix.lower() != ".meta":
                kind = classify(f)
                if kind != "Unknown":
                    rel = f.relative_to(root).as_posix()
                    result.setdefault(builtin_guid(rel), (kind, rel))
    return result


def asset_files(assets: Path):
    for f in sorted(assets.rglob("*")):
        if f.is_file() and f.suffix.lower() != ".meta":
            yield f


def load_json_strict(path: Path, report) -> object | None:
    raw = path.read_bytes()
    if raw.startswith(BINARY_MAGIC):
        report.warn(path, "binary file (FNKSCNB) - not validated; set assetSerializationFormat to Json to author it")
        return None
    dups: list[str] = []

    def hook(pairs):
        seen = set()
        for k, _ in pairs:
            if k in seen:
                dups.append(k)
            seen.add(k)
        return dict(pairs)

    try:
        text = raw.decode("utf-8-sig")
        data = json.loads(text, object_pairs_hook=hook, parse_constant=lambda c: (_ for _ in ()).throw(ValueError(c)))
    except Exception as exc:  # noqa: BLE001
        report.error(path, f"invalid JSON: {exc}")
        return None
    for k in dups:
        report.warn(path, f"duplicate key '{k}' (last one wins)")
    return data


# ----------------------------------------------------------------------------- report

class Report:
    def __init__(self, root: Path):
        self.root = root
        self.errors: list[str] = []
        self.warnings: list[str] = []

    def _rel(self, path) -> str:
        if isinstance(path, Path):
            try:
                return path.relative_to(self.root).as_posix()
            except ValueError:
                return str(path)
        return str(path)

    def error(self, path, msg):
        self.errors.append(f"{self._rel(path)}: {msg}")

    def warn(self, path, msg):
        self.warnings.append(f"{self._rel(path)}: {msg}")


# ----------------------------------------------------------------------------- C# scanning

CLASS_RE = re.compile(r"\b(?:class|struct)\s+(\w+)(?:\s*<[^>{]*>)?\s*(?::\s*([^{]+))?\{")
NAMESPACE_RE = re.compile(r"^\s*namespace\s+([\w.]+)", re.M)
SUBSCRIBE_RE = re.compile(r"\b([A-Z]\w*(?:\.\w+)+)\s*\+=\s*((?:\w+\.)*[A-Z]\w*)\s*;")  # handler = PascalCase method
STATIC_STATE_RE = re.compile(
    r"^\s*(?:public|private|internal|protected)?\s*(?:public|private|internal|protected)?\s*static\s+"
    r"(?!readonly\b|event\b|class\b|void\b|async\b|extern\b|implicit\b|explicit\b|partial\b)"
    r"[\w<>\[\],.?]+\s+\w+\s*(?:=[^>]|;)", re.M)


class ScriptIndex:
    def __init__(self):
        self.classes: dict[str, dict] = {}  # resolvable name -> info

    def add(self, short: str, info: dict):
        self.classes.setdefault(short, info)

    def resolve(self, name: str) -> dict | None:
        return self.classes.get(name)


def scan_scripts(assets: Path, report: Report) -> ScriptIndex:
    index = ScriptIndex()
    for cs in sorted(assets.rglob("*.cs")):
        try:
            src = cs.read_text(encoding="utf-8-sig", errors="replace")
        except OSError:
            continue
        ns_match = NAMESPACE_RE.search(src)
        ns = ns_match.group(1) if ns_match else ""
        for m in CLASS_RE.finditer(src):
            name, bases = m.group(1), (m.group(2) or "")
            info = {"file": cs, "source": src, "bases": bases, "namespace": ns,
                    "fqn": f"{ns}.{name}" if ns else name}
            index.add(info["fqn"], info)
            if not ns or ns == "Funke" or ns == "Funke.Components":
                index.add(name, info)
            else:
                index.classes.setdefault("__short__" + name, info)

        code_lines = [l for l in src.splitlines() if l.strip() and not l.strip().startswith("//")]
        if len(code_lines) > 200:
            report.warn(cs, f"{len(code_lines)} code lines - likely doing more than one job; split it (rule 5)")
        for where in content_tables(src):
            report.warn(cs, f"{where} - content/tuning data belongs in Data Assets (.dataasset), not C# (rule 2)")
        for fields in numbered_fields(src):
            report.warn(cs, f"numbered fields {fields} - use a List<T> field (or a container entity's children) (rule 2)")
        for pattern, msg in RULE_PATTERNS:
            if pattern.search(src):
                report.warn(cs, msg)
        for m in SUBSCRIBE_RE.finditer(src):
            target, handler = m.group(1), m.group(2)
            if not re.search(re.escape(target) + r"\s*-=\s*" + re.escape(handler) + r"\s*;", src):
                report.warn(cs, f"'{target} += {handler}' has no matching '-=' - unsubscribe in OnDisable (statics survive Play/Stop)")
        for m in STATIC_STATE_RE.finditer(src):
            line = m.group(0).strip()
            if " const " in f" {line} " or "Instance" in line or " operator" in line:
                continue
            report.warn(cs, f"static mutable state '{line[:80]}' survives Play/Stop - keep state on a singleton instance")
    return index


def class_hint(index: ScriptIndex, name: str) -> str:
    alt = index.classes.get("__short__" + name)
    if alt:
        return f" (found {alt['fqn']} - namespaced classes must be referenced by full name)"
    return ""


def class_has_member(info: dict, member: str) -> bool:
    root = member.split(".")[0]
    return re.search(r"\b" + re.escape(root) + r"\b", info["source"]) is not None


# ----------------------------------------------------------------------------- validation

class AssetTable:
    def __init__(self, project: Path, report: Report):
        self.by_guid: dict[int, tuple[str, str]] = {}
        self.builtins = builtin_assets(project)
        self.by_path: dict[str, int] = {}
        assets = project / "Assets"
        for f in asset_files(assets):
            rel = f.relative_to(project).as_posix()
            kind = classify(f)
            meta = f.with_name(f.name + ".meta")
            if f.parent == assets:
                report.warn(f, "file directly in Assets/ - move it into its folder (rule 4)")
            else:
                top = f.relative_to(assets).parts[0]
                want = EXPECTED_FOLDER.get(kind)
                if want and top != want:
                    report.warn(f, f"{kind} outside Assets/{want}/ (rule 4)")
            if not meta.is_file():
                if kind not in ("Script", "Scene", "Other", "Unknown"):  # scenes are referenced by name
                    report.warn(f, "no .meta - the engine will assign a random GUID; run `funke_game_tool.py meta` before referencing it")
                continue
            guid, problem = read_meta_guid(meta)
            if guid is None:
                report.error(meta, problem)
                continue
            if problem:
                report.warn(meta, problem)
            if guid in self.builtins:
                report.error(meta, f"GUID {guid} collides with built-in asset {self.builtins[guid][1]}")
            if guid in self.by_guid:
                report.error(meta, f"duplicate GUID {guid} (also {self.by_guid[guid][1]}) - the engine re-rolls one silently")
                continue
            self.by_guid[guid] = (kind, rel)
            self.by_path[f.relative_to(assets).as_posix().lower()] = guid
        for meta in assets.rglob("*.meta"):
            if not meta.with_name(meta.name[:-5]).exists():
                report.warn(meta, "orphan .meta (asset missing)")

    def lookup(self, guid: int) -> tuple[str, str] | None:
        return self.by_guid.get(guid) or self.builtins.get(guid)

    def check_ref(self, report: Report, where, label: str, value, kinds: tuple[str, ...], require_string=False):
        if value in (None, "", "0", 0):
            return
        if require_string and not isinstance(value, str):
            report.error(where, f"{label}: must be a JSON string (got {type(value).__name__})")
            return
        guid = parse_u64(value)
        if guid is None:
            if isinstance(value, str) and value.lower().replace("\\", "/") in self.by_path:
                return  # Assets-relative path fallback (works for script fields)
            report.error(where, f"{label}: '{value}' is not a valid GUID or known Assets-relative path")
            return
        found = self.lookup(guid)
        if not found:
            report.error(where, f"{label}: GUID {guid} not found in any .meta or built-in asset")
        elif kinds and found[0] not in kinds:
            report.error(where, f"{label}: GUID {guid} is a {found[0]} ({found[1]}), expected {'/'.join(kinds)}")


def validate_entities(path: Path, entities: list, is_prefab: bool, report: Report, assets: AssetTable,
                      scripts: ScriptIndex, prefabs: dict[int, dict]):
    uuids: dict[int, dict] = {}
    for i, ent in enumerate(entities):
        if not isinstance(ent, dict):
            report.error(path, f"entities[{i}] is not an object - whole file fails to load")
            continue
        raw = ent.get("uuid", ent.get("SceneGUID"))
        uid = parse_u64(raw)
        label = f"entity '{ent.get('name', '?')}'"
        if raw is None:
            if is_prefab:
                report.error(path, f"{label}: prefab entities need a unique non-zero uuid")
            else:
                report.warn(path, f"{label}: no uuid - gets a random id each load and cannot be referenced")
        elif uid is None:
            report.error(path, f"{label}: invalid uuid {raw!r}")
        else:
            if not isinstance(raw, str):
                report.warn(path, f"{label}: write uuid as a decimal string")
            if uid in uuids:
                report.error(path, f"{label}: duplicate uuid {uid} - whole file fails to load")
            uuids[uid] = ent

    for i, ent in enumerate(entities):
        if not isinstance(ent, dict):
            continue
        name = ent.get("name", f"#{i}")
        where = f"{report._rel(path)} > {name}"

        parent = ent.get("parentUuid")
        if parent is not None:
            if not isinstance(parent, str):
                report.warn(where, "write parentUuid as a decimal string to preserve uint64 precision across tools")
            pid = parse_u64(parent)
            if pid is None or pid not in uuids:
                report.error(where, f"parentUuid {parent!r} does not match any uuid in this file")
        if is_prefab and i == 0 and parent is not None:
            report.error(where, "the first entity is the prefab root and must not have a parentUuid")

        origin = ent.get("Origin")
        if origin is not None and str(origin).lower() not in ("scene", "prefab"):
            report.warn(where, f"Origin '{origin}' is unknown; use Scene or Prefab")
        if origin is not None and str(origin).lower() == "prefab":
            pg = ent.get("PrefabGUID")
            pr = ent.get("PrefabRef")
            if pg is None and pr is None:
                report.error(where, "prefab instance needs PrefabGUID (or PrefabRef)")
            elif pg is not None:
                assets.check_ref(report, where, "PrefabGUID", pg, ("Prefab",))
                pdata = prefabs.get(parse_u64(pg) or 0)
                if pdata:
                    check_overrides(where, ent.get("Overrides"), pdata, report)
            continue

        for key in ENTITY_TAG_KEYS:
            if key in ent and not isinstance(ent[key], bool):
                report.error(where, f"'{key}' must be true/false")

        has_t, has_rt = "Transform2D" in ent, "RectTransform2D" in ent
        if has_t and has_rt:
            report.error(where, "Transform2D and RectTransform2D conflict - world entities use Transform2D, UI uses RectTransform2D")
        for tkey in ("Transform2D", "RectTransform2D"):
            t = ent.get(tkey)
            if isinstance(t, dict) and isinstance(t.get("rotation"), (int, float)) and abs(t["rotation"]) > 6.3:
                report.warn(where, f"{tkey}.rotation={t['rotation']} - JSON rotation is radians, not degrees")

        def count(names):
            return [n for n in names if n in ent]

        if len(count(RENDERERS)) > 1:
            report.error(where, f"only one renderer per entity: {count(RENDERERS)} - use a child entity")
        if len(count(BOX2D_COLLIDERS)) > 1:
            report.error(where, f"only one collider shape per entity: {count(BOX2D_COLLIDERS)}")
        if count(FAST_PHYSICS) and (count(BOX2D_COLLIDERS) or "Rigidbody2D" in ent):
            report.error(where, "Fast* physics components conflict with Box2D components")
        if len(count(LAYOUT_GROUPS)) > 1:
            report.error(where, f"only one layout group per entity: {count(LAYOUT_GROUPS)}")

        for comp_name, comp in ent.items():
            if not isinstance(comp, dict):
                continue
            for k, v in comp.items():
                if k in BOOL_KEYS - ENTITY_TAG_KEYS and not isinstance(v, bool):
                    report.error(where, f"{comp_name}.{k} must be true/false")

        for col in BOX2D_COLLIDERS:
            c = ent.get(col)
            if isinstance(c, dict) and c.get("layer") in (0, "0"):
                report.error(where, f"{col}.layer is 0 - collides with nothing (use 1)")
            if col == "BoxCollider2D" and isinstance(c, dict) and ("localSizeX" in c) != ("localSizeY" in c):
                report.warn(where, "BoxCollider2D needs both localSizeX and localSizeY")
        rb = ent.get("Rigidbody2D")
        if isinstance(rb, dict):
            if "mass" in rb and "massOverridden" not in rb:  # the editor always writes both
                report.warn(where, "Rigidbody2D.mass is ignored without \"massOverridden\": true")
            if rb.get("bodyType", 2) not in (0, 1, 2):
                report.error(where, "Rigidbody2D.bodyType must be 0=Static, 1=Kinematic, 2=Dynamic")

        for comp_name in ("SpriteRenderer", "Image", "ParticleSystem2D"):
            c = ent.get(comp_name)
            if isinstance(c, dict):
                assets.check_ref(report, where, f"{comp_name}.textureAsset", c.get("textureAsset"), ("Texture",))
                if "texture" in c and "textureAsset" not in c:
                    report.warn(where, f"{comp_name}.texture is a legacy path key - use textureAsset with a GUID")
        anim = ent.get("Animator")
        if isinstance(anim, dict):
            assets.check_ref(report, where, "Animator.controllerAsset", anim.get("controllerAsset"), ("AnimatorController",))
            assets.check_ref(report, where, "Animator.clipAsset", anim.get("clipAsset"), ("AnimationClip",))
        a = ent.get("AudioSource")
        if isinstance(a, dict):
            assets.check_ref(report, where, "AudioSource.clipAsset", a.get("clipAsset"), ("Audio",))
            if "clip" in a and "clipAsset" not in a:
                report.warn(where, "AudioSource.clip is a legacy path key - use clipAsset with a GUID")
        t = ent.get("TextRenderer")
        if isinstance(t, dict):
            assets.check_ref(report, where, "TextRenderer.fontAsset", t.get("fontAsset"), ("Font",), require_string=True)

        for widget in UI_WIDGETS:
            if widget in ent:
                missing = [d for d in ("RectTransform2D", "Interactable") if d not in ent]
                if missing:
                    report.error(where, f"{widget} needs {missing} on the same entity (dependencies are not auto-added on load)")
        for comp_name, keys in UI_ENTITY_REF_KEYS.items():
            c = ent.get(comp_name)
            if not isinstance(c, dict):
                continue
            for k in keys:
                if k in c:
                    check_entity_ref(where, f"{comp_name}.{k}", c[k], uuids, report, require_string=True)
            for k in UI_SPRITE_KEYS:
                if k in c:
                    assets.check_ref(report, where, f"{comp_name}.{k}", c[k], ("Texture",), require_string=True)
            for ev in UI_EVENT_KEYS:
                for b, binding in enumerate(c.get(ev) or []):
                    check_binding(where, f"{comp_name}.{ev}[{b}]", binding, ent, uuids, scripts, report)

        listed = set()
        for s in ent.get("Scripts") or []:
            cls = s if isinstance(s, str) else (s.get("className") or s.get("class")) if isinstance(s, dict) else None
            if not cls:
                report.error(where, f"invalid Scripts entry {s!r}")
                continue
            listed.add(cls)
            if isinstance(s, dict) and str(s.get("type", "Managed")).lower() not in ("managed", "csharp", "c#"):
                continue
            if not scripts.resolve(cls):
                report.error(where, f"script class '{cls}' not found in Assets/**/*.cs - the engine skips it silently{class_hint(scripts, cls)}")
        for mc in ent.get("ManagedComponents") or []:
            listed.add(mc)
            if not scripts.resolve(mc):
                report.error(where, f"managed component '{mc}' not found{class_hint(scripts, mc)}")

        fields = ent.get("ScriptFields") or {}
        if not isinstance(fields, dict):
            report.error(where, "ScriptFields must be an object {ClassName: [ {name,type,value} ]}")
            continue
        for cls, entries in fields.items():
            if cls not in listed:
                report.error(where, f"ScriptFields['{cls}'] ignored - class not listed in Scripts/ManagedComponents")
            info = scripts.resolve(cls)
            for entry in entries if isinstance(entries, list) else []:
                check_script_field(where, cls, info, entry, uuids, is_prefab, assets, report)


def check_entity_ref(where, label, value, uuids, report, require_string=False):
    if require_string and not isinstance(value, str):
        report.error(where, f"{label}: entity ref must be a JSON string")
        return
    if value in ("0", "", 0, None):
        return
    uid = parse_u64(value)
    if uid is None or uid not in uuids:
        report.error(where, f"{label}: entity {value!r} not found in this file")


def check_binding(where, label, binding, ent, uuids, scripts, report):
    if not isinstance(binding, dict):
        report.error(where, f"{label}: binding must be an object")
        return
    target = binding.get("entity", "0")
    check_entity_ref(where, f"{label}.entity", target, uuids, report, require_string=True)
    target_ent = ent if target in ("0", "", None) else uuids.get(parse_u64(target) or 0)
    cls, method = binding.get("class"), binding.get("method")
    if not cls or not method:
        report.error(where, f"{label}: needs class and method")
        return
    info = scripts.resolve(cls)
    if not info:
        report.error(where, f"{label}: class '{cls}' not found{class_hint(scripts, cls)}")
        return
    if not re.search(r"\b" + re.escape(method) + r"\s*\(", info["source"]):
        report.error(where, f"{label}: method '{method}' not found in {cls}")
    if isinstance(target_ent, dict):
        names = {s if isinstance(s, str) else s.get("className") for s in target_ent.get("Scripts") or []}
        if cls not in names:
            report.warn(where, f"{label}: target entity has no '{cls}' script")


def check_script_field(where, cls, info, entry, uuids, is_prefab, assets, report):
    if not isinstance(entry, dict) or "name" not in entry:
        report.error(where, f"ScriptFields['{cls}'] entry must be {{name, type, value}}")
        return
    name, ftype, value = entry["name"], str(entry.get("type", "")), entry.get("value")
    label = f"{cls}.{name}"
    if not isinstance(value, str):
        report.error(where, f"{label}: value must be a JSON string (numbers lose precision / are mis-parsed)")
        return
    if info and not entry.get("readOnly") and name != "Entity" and not class_has_member(info, name):  # editor writes inherited read-only fields
        report.warn(where, f"{label}: no member '{name}' in {info['file'].name} - unknown fields are dropped")
    if ftype == "entity" or (not ftype and value.startswith("prefab:")):
        if value.startswith("prefab:"):
            assets.check_ref(report, where, label, value[7:], ("Prefab",))
        elif value not in ("", "0"):
            uid = parse_u64(value)
            if uid is None:
                report.error(where, f"{label}: invalid entity ref {value!r}")
            elif uid not in uuids:
                report.warn(where, f"{label}: entity {uid} not in this file (only valid if it lives in another loaded scene)")
    elif ftype.startswith("component:"):
        if value not in ("", "0"):
            uid_str, _, comp = value.partition(":")
            uid = parse_u64(uid_str)
            if uid is None or not comp:
                report.error(where, f"{label}: component ref must be \"<entityUuid>:<ComponentName>\"")
            elif uid not in uuids:
                report.warn(where, f"{label}: entity {uid} not in this file")
    elif ftype in ("texture", "audio", "scene", "material", "mesh", "animationclip", "animatorcontroller") or ftype.startswith("dataasset"):
        kind = {"texture": "Texture", "audio": "Audio", "scene": "Scene", "material": "Material", "mesh": "Mesh",
                "animationclip": "AnimationClip", "animatorcontroller": "AnimatorController"}.get(ftype, "DataAsset")
        assets.check_ref(report, where, label, value, (kind,))
    elif ftype == "bool" and value.lower() not in ("true", "false", "1", "0"):
        report.error(where, f"{label}: bool value must be \"true\"/\"false\"")
    elif ftype in ("float", "double", "int", "long", "short", "byte", "uint", "ulong") and value:
        try:
            float(value)
        except ValueError:
            report.error(where, f"{label}: '{value}' is not a number (invariant culture, '.' decimal)")
    if is_prefab and value and value.isdigit() and value in {str(u) for u in uuids} and ftype != "entity":
        report.warn(where, f"{label}: looks like a sibling entity ref - add \"type\": \"entity\" so it is remapped per instance")


def check_overrides(where, overrides, prefab_root_entities, report):
    if not isinstance(overrides, dict):
        return
    root = prefab_root_entities[0] if prefab_root_entities else {}
    scripts = {s if isinstance(s, str) else s.get("className") for s in root.get("Scripts") or []}
    fields = root.get("ScriptFields") or {}
    for key in overrides:
        cls, _, field = key.partition(".")
        if cls in scripts and field:
            names = {e.get("name") for e in fields.get(cls, []) if isinstance(e, dict)}
            if field not in names:
                report.error(where, f"Overrides['{key}']: the prefab root has no ScriptFields entry '{field}' for {cls} - add it to the prefab first")


def validate_scene(path, data, report, assets, scripts, prefabs):
    if not isinstance(data, dict):
        report.error(path, "root must be an object")
        return
    ents = data.get("entities")
    if not isinstance(ents, list):
        report.error(path, "missing 'entities' array - load fails")
        return
    v = data.get("version")
    if v is not None and (not isinstance(v, int) or v < 0 or v > 1):
        report.error(path, f"version {v!r} unsupported (0..1)")
    if "name" in data and data["name"] != path.stem:
        report.warn(path, f"name '{data['name']}' differs from file name '{path.stem}'")
    for i, sys_entry in enumerate(data.get("systems") or []):
        cls = sys_entry if isinstance(sys_entry, str) else sys_entry.get("className") if isinstance(sys_entry, dict) else None
        if not cls:
            report.error(path, f"systems[{i}] invalid - load fails")
            continue
        if isinstance(sys_entry, dict):
            for k, val in (sys_entry.get("fields") or {}).items():
                if not isinstance(val, str):
                    report.error(path, f"systems[{i}].fields.{k} must be a string - load fails")
        if not scripts.resolve(cls):
            report.error(path, f"SceneSystem '{cls}' not found{class_hint(scripts, cls)}")
    validate_entities(path, ents, False, report, assets, scripts, prefabs)


def prefab_entities(data) -> list | None:
    if not isinstance(data, dict):
        return None
    if isinstance(data.get("Entities"), list) and data["Entities"]:
        return data["Entities"]
    for k in ("Entity", "prefab"):
        if isinstance(data.get(k), dict):
            return [data[k]]
    if any(k in data for k in ("Transform2D", "Scripts", "name")):
        return [data]
    return None


def validate(project: Path) -> int:
    report = Report(project)
    pj_path = project / "project.json"
    pj = load_json_strict(pj_path, report) if pj_path.is_file() else None
    if not pj_path.is_file():
        report.error(pj_path, "missing")
    assets_dir = project / "Assets"
    if not assets_dir.is_dir():
        report.error(assets_dir, "missing Assets/ folder")
        return finish(report)

    scripts = scan_scripts(assets_dir, report)
    assets = AssetTable(project, report)

    scene_files: dict[str, list[Path]] = {}
    for f in assets_dir.rglob("*.scene"):
        scene_files.setdefault(f.stem.lower(), []).append(f)
    for stem, files in scene_files.items():
        if len(files) > 1:
            report.error(files[0], f"scene name '{stem}' is ambiguous: {[report._rel(x) for x in files]}")

    if isinstance(pj, dict):
        fv = pj.get("projectFormatVersion", 0)
        if isinstance(fv, int) and fv > 0 and not str(pj.get("engineVersion") or ""):
            report.error(pj_path, "projectFormatVersion is set but engineVersion is empty - the project is rejected")
        fmt = str(pj.get("assetSerializationFormat", "Binary")).lower()
        if fmt not in ("json",):
            report.warn(pj_path, "assetSerializationFormat is not \"Json\" - the Editor will re-save your scenes as binary")
        def scene_ok(name, label):
            if isinstance(name, dict):
                name = name.get("name")
            if name and str(name).lower() not in scene_files:
                report.error(pj_path, f"{label} '{name}' has no matching .scene under Assets/")
        if pj.get("startupScene"):
            scene_ok(pj["startupScene"], "startupScene")
        for s in pj.get("buildScenes") or []:
            scene_ok(s, "buildScenes entry")
        for g in pj.get("globalSystems") or []:
            cls = g if isinstance(g, str) else g.get("className") if isinstance(g, dict) else None
            if cls and not scripts.resolve(cls):
                report.error(pj_path, f"globalSystems '{cls}' not found{class_hint(scripts, cls)}")

    prefab_data: dict[int, list] = {}
    for f in sorted(assets_dir.rglob("*.prefab")):
        data = load_json_strict(f, report)
        if data is None:
            continue
        ents = prefab_entities(data)
        if ents is None:
            if not (isinstance(data, dict) and data.get("PrefabRef")):
                report.error(f, "no 'Entities' array - prefab has no entities")
            continue
        meta = f.with_name(f.name + ".meta")
        guid = read_meta_guid(meta)[0] if meta.is_file() else None
        if guid:
            prefab_data[guid] = ents
        validate_entities(f, ents, True, report, assets, scripts, prefab_data)

    for f in sorted(assets_dir.rglob("*.scene")):
        data = load_json_strict(f, report)
        if data is not None:
            validate_scene(f, data, report, assets, scripts, prefab_data)

    for f in sorted(assets_dir.rglob("*.dataasset")):
        data = load_json_strict(f, report)
        if not isinstance(data, dict):
            continue
        t = data.get("type")
        if not t:
            report.error(f, "missing 'type' - load fails")
        elif not scripts.resolve(t):
            report.error(f, f"DataAsset type '{t}' not found{class_hint(scripts, t)}")
        for k, v in (data.get("fields") or {}).items():
            if not isinstance(v, str):
                report.error(f, f"fields.{k} must be a string (numbers/bools become empty)")

    return finish(report)


def finish(report: Report) -> int:
    for w in report.warnings:
        print(f"WARN  {w}")
    for e in report.errors:
        print(f"ERROR {e}")
    print(f"\n{len(report.errors)} error(s), {len(report.warnings)} warning(s)")
    return 1 if report.errors else 0


# ----------------------------------------------------------------------------- smoke

REQUIRED_MARKERS = ["Loaded project:", "Loaded scene:", "User assembly loaded:"]
END_MARKERS = ["CI smoke test reached a stable frame", "--quit-after", "[Automation] Quit at frame"]  # clean end
FATAL_MARKERS = ["Unhandled exception at app boundary", "Unhandled frame exception", "Wrote minidump",
                 "Failed to load user assembly"]
BAD_MARKERS = ["Exception in ", "[Coroutine]", "registered event(s) still active", "no matching class was found",
               "could not be resolved", "no scene file resolves"]


def find_runtime(arg: str | None) -> Path | None:
    for cand in (arg, os.environ.get("FUNKE_RUNTIME")):
        if cand and Path(cand).is_file():
            return Path(cand)
    eng = engine_root_from_skill()
    if eng:
        for cfg in ("Release", "Debug", "Dist"):  # same default as Run.ps1 and funke run
            exe = eng / "bin" / f"{cfg}-windows-x86_64" / "Funke-Runtime" / "Funke-Runtime.exe"
            if exe.is_file():
                return exe
    return None


def find_editor(arg: str | None) -> Path | None:
    for cand in (arg, os.environ.get("FUNKE_EDITOR_EXE")):
        if cand and Path(cand).is_file():
            return Path(cand)
    eng = engine_root_from_skill()
    if eng:
        for cfg in ("Dist", "Release", "Debug"):
            exe = eng / "bin" / f"{cfg}-windows-x86_64" / "Funke-Editor" / "Funke-Editor.exe"
            if exe.is_file():
                return exe
    return None


def run_and_read_log(exe: Path, args: list[str], timeout: int) -> tuple[int, str, Path]:
    """Runs a Funke app from its own folder with CI=1 and returns (exit code, new log text, log path)."""
    log_path = Path(os.environ.get("LOCALAPPDATA", "")) / "Funke" / "Logs" / f"{exe.stem}.log"
    offset = log_path.stat().st_size if log_path.is_file() else 0
    cmd = [str(exe), *args]
    print("RUN  ", " ".join(cmd))
    try:
        proc = subprocess.run(cmd, cwd=exe.parent, env=dict(os.environ, CI="1"), capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=timeout)
        code, out = proc.returncode, (proc.stdout or "") + (proc.stderr or "")
    except subprocess.TimeoutExpired as exc:
        code, out = 124, (exc.stdout or "") if isinstance(exc.stdout, str) else ""
        print(f"ERROR timed out after {timeout}s")
    if code > 0x7FFFFFFF:  # Windows reports -1 as 4294967295
        code -= 1 << 32
    log = out
    if log_path.is_file():
        with log_path.open("rb") as fh:
            fh.seek(offset if log_path.stat().st_size >= offset else 0)
            log += fh.read().decode("utf-8", errors="replace")
    return code, log, log_path


def build(project: Path, editor_arg: str | None, profile: str | None, output: str | None, timeout: int) -> int:
    editor = find_editor(editor_arg)
    if not editor:
        print("ERROR editor not found - pass --editor <path to Funke-Editor.exe> or set FUNKE_EDITOR_EXE")
        return 2
    args = [f"--project={project}", "--build"]
    if profile:
        args.append(f"--build-profile={profile}")
    if output:
        args.append(f"--build-output={Path(output).resolve()}")
    code, log, log_path = run_and_read_log(editor, args, timeout)
    shown = set()
    for line in log.splitlines():
        if "[Build]" in line and ("error" in line.lower() or "fail" in line.lower() or "Command-line" in line):
            key = re.sub(r"\[(trace|debug|info|warning|error|critical)\] ", "", line)  # stdout + file sink repeat lines
            if key not in shown:
                shown.add(key)
                print("  " + key)
    if code != 0 or "Command-line build succeeded" not in log:
        print(f"BUILD FAILED (exit {code}) - full log: {log_path}")
        return 1
    print("BUILD OK")
    return 0


def quit_after_shots(times: str) -> str:
    """Game time 0.5 s (or 30 frames) after the last --screenshot-at entry ("2s,5s", "500ms", "300f")."""
    secs, frames = [], []
    for t in (x.strip().lower() for x in times.split(",") if x.strip()):
        try:
            if t.endswith("ms"):
                secs.append(float(t[:-2]) / 1000)
            elif t.endswith("f"):
                frames.append(int(t[:-1]))
            else:
                secs.append(float(t.rstrip("s")))
        except ValueError:
            continue
    if secs:
        return f"{max(secs) + 0.5:g}s"
    return f"{max(frames) + 30}f" if frames else "5s"


def smoke(project: Path, runtime_arg: str | None, expects: list[str], timeout: int,
          seconds: float | None = None, scene: str | None = None, prefs_profile: str = "agent",
          input_script: str | None = None, screenshot_at: str | None = None, exec_line: str | None = None) -> int:
    runtime = find_runtime(runtime_arg)
    if not runtime:
        print("ERROR runtime not found - pass --runtime <path to Funke-Runtime.exe> or set FUNKE_RUNTIME")
        return 2
    cfg = runtime.parent.parent.name.split("-")[0] if "-" in runtime.parent.parent.name else "Debug"
    pj = json.loads((project / "project.json").read_text(encoding="utf-8-sig"))
    name = pj.get("name", project.name)
    dll = project / "bin" / cfg / f"{name}.dll"
    if not dll.is_file():
        print(f"WARN  {dll} missing - run: dotnet build \"{project / (name + '.csproj')}\" -c {cfg}")
    else:
        newest_cs = max((p.stat().st_mtime for p in (project / "Assets").rglob("*.cs")), default=0)
        if newest_cs > dll.stat().st_mtime:
            print(f"WARN  {dll.name} is older than the newest .cs file - rebuild before testing")

    # --background: never steal focus or go exclusive fullscreen; own prefs profile: real saves stay untouched
    args = [f"--project={project}", "--background", f"--prefs-profile={prefs_profile}"]
    if input_script:
        args.append(f"--input-script={Path(input_script).resolve()}")
    if screenshot_at:
        args.append(f"--screenshot-at={screenshot_at}")
    if exec_line:
        args.append(f"--exec={exec_line}")
    if not input_script:
        if seconds is not None:
            args.append(f"--quit-after={seconds}")
        elif screenshot_at:
            args.append(f"--quit-at={quit_after_shots(screenshot_at)}")  # like Run.ps1: just after the last shot
        else:
            args.append("--ci-smoke-test")
    if scene:
        args.append(f"--scene={scene}")
    code, log, log_path = run_and_read_log(runtime, args, timeout)

    failures = []
    for m in REQUIRED_MARKERS + expects:
        if m not in log:
            failures.append(f"missing marker: {m}")
    if not any(m in log for m in END_MARKERS):
        failures.append("the run did not reach its planned end (stable frame / --quit-after / script quit step)")
    for m in FATAL_MARKERS:
        if m in log:
            failures.append(f"fatal marker: {m}")
    suspicious = [l for l in log.splitlines()
                  if (any(b in l for b in BAD_MARKERS) or "[error]" in l or "[critical]" in l
                      or re.search(r"\b(exception|failed)\b", l, re.I))
                  and "last-error=0" not in l]
    print(f"exit code: {code}")
    if suspicious:
        print("---- suspicious log lines ----")
        for l in dict.fromkeys(suspicious):
            print("  " + l)
    if code != 0:
        failures.append(f"exit code {code}")
    if failures:
        print("---- FAILED ----")
        for f in failures:
            print("  " + f)
        print(f"(full log: {log_path})")
        return 1
    print("SMOKE OK" + (" (but read the suspicious lines above)" if suspicious else ""))
    return 0


# ----------------------------------------------------------------------------- main

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("guid", help="print random ids")
    g.add_argument("-n", type=int, default=1)
    m = sub.add_parser("meta", help="create .meta files for assets")
    m.add_argument("files", nargs="+")
    m.add_argument("--force", action="store_true", help="replace an INVALID existing meta (valid metas are never replaced)")
    lst = sub.add_parser("list", help="list assets and GUIDs")
    lst.add_argument("project", nargs="?", default=".")
    b = sub.add_parser("builtin", help="GUID of a file in FunkeAssets/, e.g. Fonts/PressStart2P.ttf")
    b.add_argument("relpath", nargs="+")
    v = sub.add_parser("validate", help="validate a project")
    v.add_argument("project", nargs="?", default=".")
    s = sub.add_parser("smoke", help="run Funke-Runtime --ci-smoke-test and check the log")
    s.add_argument("project", nargs="?", default=".")
    s.add_argument("--runtime")
    s.add_argument("--expect", action="append", default=[], help="log substring that must appear (repeatable)")
    s.add_argument("--seconds", type=float, help="play N seconds (--quit-after) instead of 3 frames")
    s.add_argument("--scene", help="start in this scene (--scene)")
    s.add_argument("--prefs-profile", default="agent", help="isolated PlayerPrefs/SaveData profile (default: agent)")
    s.add_argument("--input-script", help="scripted input JSON (its quit step ends the run)")
    s.add_argument("--screenshot-at", help="game-time screenshots, e.g. 2s,5s (saved to %%LOCALAPPDATA%%/Funke/Screenshots)")
    s.add_argument("--exec", dest="exec_line", help="console/[DevCommand] line run on the first frame")
    s.add_argument("--timeout", type=int, default=120)
    bd = sub.add_parser("build", help="Funke-Editor --build: real editor build of the project, no clicks")
    bd.add_argument("project", nargs="?", default=".")
    bd.add_argument("--editor", help="path to Funke-Editor.exe (default: FUNKE_EDITOR_EXE or the engine's bin/)")
    bd.add_argument("--profile", help="BuildProfiles/<name>.build (default: active, else first Windows)")
    bd.add_argument("--output", help="output folder (default: <project>/Builds/<profile>)")
    bd.add_argument("--timeout", type=int, default=900)
    args = ap.parse_args()

    if args.cmd == "guid":
        taken: set[int] = set()
        for _ in range(args.n):
            print(new_guid(taken))
        return 0

    if args.cmd == "builtin":
        for rel in args.relpath:
            print(f"{builtin_guid(rel)}  {rel}")
        return 0

    if args.cmd == "meta":
        rc = 0
        project = None
        taken: set[int] = set()
        for f in args.files:
            asset = Path(f).resolve()
            if not asset.is_file():
                print(f"ERROR {f}: not a file")
                rc = 1
                continue
            if asset.suffix.lower() == ".meta":
                continue
            project = project or find_project_root(asset.parent)
            if project and not taken:
                for meta in (project / "Assets").rglob("*.meta"):
                    gid = read_meta_guid(meta)[0]
                    if gid:
                        taken.add(gid)
            if project and "Assets" not in asset.relative_to(project).parts[:1]:
                print(f"WARN  {f}: not under <project>/Assets - the engine only indexes Assets/")
            meta = asset.with_name(asset.name + ".meta")
            if meta.exists():
                gid, problem = read_meta_guid(meta)
                if gid and not problem:
                    print(f"{gid}  {f}  (existing)")
                    continue
                if not args.force:
                    print(f"ERROR {meta}: {problem} - fix it by hand or pass --force")
                    rc = 1
                    continue
            gid = new_guid(taken)
            write_meta(asset, gid)
            print(f"{gid}  {f}")
        return rc

    if args.cmd == "list":
        project = Path(args.project).resolve()
        for f in asset_files(project / "Assets"):
            meta = f.with_name(f.name + ".meta")
            gid = read_meta_guid(meta)[0] if meta.is_file() else None
            print(f"{str(gid) if gid else 'NO-META':>20}  {classify(f):<9}  {f.relative_to(project).as_posix()}")
        return 0

    if args.cmd == "validate":
        return validate(Path(args.project).resolve())

    if args.cmd == "smoke":
        return smoke(Path(args.project).resolve(), args.runtime, args.expect, args.timeout, args.seconds, args.scene,
                     args.prefs_profile, args.input_script, args.screenshot_at, args.exec_line)

    if args.cmd == "build":
        return build(Path(args.project).resolve(), args.editor, args.profile, args.output, args.timeout)
    return 2


if __name__ == "__main__":
    sys.exit(main())
