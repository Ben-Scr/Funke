"""Shared generator helpers for a Funke project. Copy to <Project>/Tools/funke_kit.py and import it from every
generator (scenes, prefabs, data, art) so all of them use one GUID scheme and one writer.

    from funke_kit import *
    menu = Scene("LevelSelect")
    cam = menu.add("Main Camera", T2D(), Camera2D=dict(orthoSize=5))
    ui = menu.add("UI", RECT_FILL)
    grid = menu.add("Grid", rect(size=(900, 700)), parent=ui, VerticalLayoutGroup=dict(spacing=16))
    menu.script(ui, "LevelSelect", Catalog=f_data("LevelCatalog", gid("data", "LevelCatalog")),
                CardPrefab=f_prefab(gid("prefab", "UI/LevelCard")), Grid=f_entity(grid))
    menu.save()

Rules this module enforces: GUIDs are stable (hash of kind + name, or the GUID already in an existing .meta),
the .meta is written before its asset, files are written atomically and only when the bytes change (an open
Editor hot-reloads every write), every ScriptFields/Data Asset value is a string, UTF-8 without BOM.
"""
from __future__ import annotations

import hashlib
import json
import os
import time
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent          # <Project>/Tools/funke_kit.py
ASSETS = PROJECT / "Assets"
MAX_GUID = (1 << 63) - 1


# ----------------------------------------------------------------------------- ids and files

def gid(kind: str, name: str) -> str:
    """Stable asset GUID (decimal string) for e.g. gid("prefab", "Enemies/Slime"). Never derive GUIDs from list
    positions: reordering a list must not change any reference."""
    g = int.from_bytes(hashlib.sha1(f"{kind}:{name}".encode("utf-8")).digest()[:8], "little") & MAX_GUID
    return str(g or 1)


def write_bytes(path: Path, data: bytes) -> bool:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_bytes() == data:
        return False
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(data)
    for attempt in range(20):                              # an open Editor or a virus scanner can hold the file briefly
        try:
            os.replace(tmp, path)
            return True
        except PermissionError:
            if attempt == 19:
                raise
            time.sleep(0.25)
    return True


def write_json(path: Path, obj) -> bool:
    return write_bytes(path, (json.dumps(obj, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))


KINDS = {".scene": "Scene", ".prefab": "Prefab", ".dataasset": "DataAsset", ".png": "Texture", ".wav": "Audio",
         ".mp3": "Audio", ".ttf": "Font", ".otf": "Font", ".material": "Material", ".obj": "Mesh",
         ".anim": "AnimationClip", ".animctrl": "AnimatorController"}


def meta(asset: Path, guid: str) -> str:
    """Write <asset>.meta unless it already holds a valid GUID (which then wins and is returned)."""
    m = asset.with_name(asset.name + ".meta")
    if m.exists():
        try:
            old = str(json.loads(m.read_text(encoding="utf-8-sig")).get("AssetGUID", ""))
            if old.isdigit() and int(old) > 0:
                return old
        except Exception:
            pass
    write_json(m, {"AssetGUID": guid, "uuid": guid, "kind": KINDS.get(asset.suffix.lower(), "Other")})
    return guid


# ----------------------------------------------------------------------------- values

def enc(v) -> str:
    """Engine string encoding: bool true/false, invariant numbers, tuples as "x,y", lists newline-prefixed."""
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        return repr(v) if v != int(v) else str(int(v))
    if isinstance(v, int):
        return str(v)
    if isinstance(v, tuple):
        return ",".join(enc(x) for x in v)
    if isinstance(v, list):
        return enc_list(v)
    return "" if v is None else str(v)


def enc_list(items: list) -> str:
    return "".join("\n" + enc(i).replace("\\", "\\\\").replace("\n", "\\n") for i in items)


def enc_objects(rows: list[list]) -> str:
    """List<PlainClass>: one row of leaf values per element, in field declaration order."""
    return enc_list([enc_list(r) for r in rows])


# ScriptFields helpers: return (type, value). Pass them as keyword args to Scene.script / Prefab.script.
def f_int(v): return ("int", enc(int(v)))
def f_float(v): return ("float", enc(float(v)))
def f_bool(v): return ("bool", enc(bool(v)))
def f_str(v): return ("string", str(v))
def f_vec(*v): return (f"vector{len(v)}", enc(tuple(v)))
def f_color(r, g, b, a=1.0): return ("color", enc((r, g, b, a)))
def f_enum(v): return ("enum", enc(int(v)))
def f_entity(uuid): return ("entity", str(uuid))
def f_prefab(guid): return ("entity", f"prefab:{guid}")
def f_comp(uuid, name): return (f"component:{name}", f"{uuid}:{name}")
def f_texture(guid): return ("texture", str(guid))
def f_audio(guid): return ("audio", str(guid))
def f_scene(guid): return ("scene", str(guid))
def f_data(type_name, guid): return (f"dataasset:{type_name}", str(guid))
def f_list(values): return ("list", enc_list([v[1] if isinstance(v, tuple) else v for v in values]))
# List<Entity> / List<SomeComponent>: the element type lets prefab instances remap the ids to their own entities.
def f_entity_list(uuids): return ("list", enc_list([str(u) for u in uuids]), {"itemType": "entity"})
def f_component_list(refs): return ("list", enc_list([f"{u}:{c}" for u, c in refs]), {"itemType": f"component:{refs[0][1]}" if refs else "entity"})


# ----------------------------------------------------------------------------- components

def rgba(c, a=None):
    """'#RRGGBB[AA]' or (r,g,b[,a]) 0..1 -> dict(r,g,b,a)."""
    if isinstance(c, str):
        h = c.lstrip("#")
        vals = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)] + [int(h[6:8], 16) / 255 if len(h) == 8 else 1.0]
    else:
        vals = list(c) + [1.0] * (4 - len(c))
    if a is not None:
        vals[3] = a
    return {k: round(v, 4) for k, v in zip("rgba", vals)}


def T2D(x=0.0, y=0.0, rot=0.0, sx=1.0, sy=1.0) -> dict:
    return {"Transform2D": {"posX": x, "posY": y, "rotation": rot, "scaleX": sx, "scaleY": sy}}


def T3D(x=0.0, y=0.0, z=0.0, rx=0.0, ry=0.0, rz=0.0, s=1.0) -> dict:
    return {"Transform3D": {"posX": x, "posY": y, "posZ": z, "rotX": rx, "rotY": ry, "rotZ": rz,
                            "scaleX": s, "scaleY": s, "scaleZ": s}}


def sprite(texture: str, slice: str | None = None, ppu: float | None = None, order: int = 0, layer: int = 0,
           color="#FFFFFF") -> dict:
    d = {"textureAsset": str(texture), "sortOrder": order, "sortLayer": layer, **rgba(color)}
    if slice:
        d["spriteName"] = slice
    if ppu:
        d["pixelsPerUnit"] = ppu
    return {"SpriteRenderer": d}


def rect(pos=(0, 0), size=(100, 100), anchor=(0.5, 0.5), pivot=None, fill=False) -> dict:
    """RectTransform2D: sizes are absolute reference units; anchors only move the reference point."""
    pivot = pivot or anchor
    d = {"anchorMinX": anchor[0], "anchorMinY": anchor[1], "anchorMaxX": anchor[0], "anchorMaxY": anchor[1],
         "pivotX": pivot[0], "pivotY": pivot[1], "posX": pos[0], "posY": pos[1], "sizeX": size[0], "sizeY": size[1]}
    if fill:
        d["fillParent"] = True
    return {"RectTransform2D": d}


RECT_FILL = rect(fill=True)


def image(texture: str | None = None, color="#FFFFFF", order: int = 0, slice: str | None = None,
          border: tuple | None = None, border_scale: float = 1.0, tiled: bool = False) -> dict:
    d = {**rgba(color), "sortOrder": order}
    if texture:
        d["textureAsset"] = str(texture)
    if slice:
        d["spriteName"] = slice
    if border:
        d.update(imageType=2 if tiled else 1, border=list(border), borderScale=border_scale)
    return {"Image": d}


def text(value: str, size: int = 32, color="#FFFFFF", align: int = 1, valign: int = 1, order: int = 1,
         font: str | None = None, key: str | None = None) -> dict:
    d = {"text": value, "fontSize": size, "alignment": align, "valign": valign, "sortOrder": order, **rgba(color)}
    if font:
        d["fontAsset"] = str(font)
    out = {"TextRenderer": d}
    if key:
        out["LocalizedText"] = {"key": key}
    return out


def click(target_uuid, cls: str, method: str, arg=None) -> list:
    """onClick binding. arg: None (void), bool, int, float or str (argKind 0/1/2/3/5)."""
    kind = 0 if arg is None else 1 if isinstance(arg, bool) else 2 if isinstance(arg, int) else \
        3 if isinstance(arg, float) else 5
    return [{"entity": str(target_uuid), "class": cls, "method": method, "enabled": True,
             "argKind": kind, "argValue": "" if arg is None else enc(arg)}]


def button(on_click: list, normal="#FFFFFF", hovered="#F0F0F0", pressed="#C8C8C8", target: str = "0") -> dict:
    """Button needs RectTransform2D + Interactable + Image on the same entity; combine with rect() and image()."""
    return {"Interactable": {"interactable": True, "focusable": True},
            "Button": {"transitionMode": 0, "normal": rgba(normal), "hovered": rgba(hovered),
                       "pressed": rgba(pressed), "targetGraphic": str(target), "onClick": on_click}}


# ----------------------------------------------------------------------------- scenes and prefabs

class _EntityList:
    def __init__(self, seed: str, local_ids: bool):
        self.entities: list[dict] = []
        self.by_uuid: dict[str, dict] = {}
        self._seed = seed
        self._local = local_ids
        self._seen: dict[str, int] = {}

    def _uuid(self, name: str) -> str:
        if self._local:                                    # prefabs: 1001, 1002, ... (remapped per instance)
            return str(1001 + len(self.entities))
        n = self._seen.get(name, 0)                        # scenes: hash of name + occurrence, so inserting
        self._seen[name] = n + 1                           # another entity never shifts existing uuids
        return gid(self._seed, f"{name}#{n}")

    def add(self, name: str | None, *parts: dict, parent: str | None = None, disabled: bool = False,
            **components) -> str:
        """Add an entity; parts are component dicts (T2D(), sprite(...)), components are extra keyword
        components (Camera2D=dict(...)). Returns its uuid."""
        u = self._uuid(name or "entity")
        e: dict = {"uuid": u}
        if name:
            e["name"] = name
        if parent:
            e["parentUuid"] = str(parent)
        if disabled:
            e["disabled"] = True
        for part in parts:
            e.update(part)
        e.update(components)
        self.entities.append(e)
        self.by_uuid[u] = e
        return u

    def script(self, uuid: str, cls: str, **fields) -> None:
        """Attach a C# script and its fields (values from the f_* helpers or plain numbers/bools/strings)."""
        e = self.by_uuid[str(uuid)]
        e.setdefault("Scripts", []).append({"className": cls, "type": "Managed"})
        rows = []
        for name, v in fields.items():
            if not isinstance(v, tuple):
                v = f_bool(v) if isinstance(v, bool) else f_float(v) if isinstance(v, float) else \
                    f_int(v) if isinstance(v, int) else f_str(v)
            rows.append({"name": name, "type": v[0], "value": v[1], **(v[2] if len(v) > 2 else {})})
        e.setdefault("ScriptFields", {})[cls] = rows


class Scene(_EntityList):
    def __init__(self, name: str, folder: str = "Scenes", systems: list | None = None):
        super().__init__(f"scene:{name}", local_ids=False)
        self.name, self.folder, self.systems = name, folder, systems or []

    def instance(self, prefab_guid: str, name: str | None = None, parent: str | None = None,
                 overrides: dict | None = None, entity_overrides: dict | None = None) -> str:
        u = self._uuid(name or f"prefab:{prefab_guid}")
        e = {"Origin": "Prefab", "PrefabGUID": str(prefab_guid), "uuid": u}
        if name:
            e["name"] = name
        if parent:
            e["parentUuid"] = str(parent)
        if overrides:
            e["Overrides"] = overrides
        if entity_overrides:
            e["EntityOverrides"] = entity_overrides
        self.entities.append(e)
        self.by_uuid[u] = e
        return u

    @property
    def guid(self) -> str:
        return gid("scene", self.name)

    def save(self) -> str:
        path = ASSETS / self.folder / f"{self.name}.scene"
        g = meta(path, self.guid)
        write_json(path, {"version": 1, "name": self.name, "systems": self.systems, "entities": self.entities})
        return g


class Prefab(_EntityList):
    """First entity added is the root. Local uuids 1001, 1002, ... (remapped per instance by the engine)."""

    def __init__(self, path: str):
        super().__init__(f"prefab:{path}", local_ids=True)
        self.path = path                                    # "UI/LevelCard" -> Assets/Prefabs/UI/LevelCard.prefab

    @property
    def guid(self) -> str:
        return gid("prefab", self.path)

    def save(self) -> str:
        path = ASSETS / "Prefabs" / f"{self.path}.prefab"
        g = meta(path, self.guid)
        write_json(path, {"version": 1, "type": "Prefab", "Entities": self.entities})
        return g


def data_asset(path: str, type_name: str, overwrite: bool = False, **fields) -> str:
    """Assets/Data/<path>.dataasset. Values: plain Python (encoded) or f_* tuples (their value is used).
    overwrite=False never touches an existing file: values tuned in the Inspector win."""
    p = ASSETS / "Data" / f"{path}.dataasset"
    g = meta(p, gid("data", path))
    if p.exists() and not overwrite:
        return g
    # an f_* helper result is (type, value); every other tuple is a vector/colour
    vals = {k: (v[1] if isinstance(v, tuple) and len(v) == 2 and isinstance(v[0], str) else enc(v))
            for k, v in fields.items()}
    write_json(p, {"version": 1, "type": type_name, "fields": vals})
    return g
