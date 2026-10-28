"""Scene-generation kit for portrait mobile apps made with Funke. Copy into <project>/Tools/ and import it from the
app's own generator (see gen_example.py). Writes JSON scenes, .meta files and UI textures (Pillow).

Coordinates are UI units on a 1080-wide canvas (project: MatchWidthOrHeight, match 0). top(...) hangs a rect from
its parent's top edge (y <= 0 goes down), bottom(...) stands it on the bottom edge."""
import hashlib
import json
import math
import os

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "Assets")
TEXTURES = os.path.join(ASSETS, "Textures")
SQUARE = "12290035569547768290"  # engine built-in white square texture
MAX63 = (1 << 63) - 1
KINDS = {".png": "Texture", ".ttf": "Font", ".otf": "Font", ".scene": "Scene", ".prefab": "Prefab"}
SS = 4  # supersampling for drawn shapes

# Default dark theme; apps override these module globals before building.
BG = (0.071, 0.071, 0.071, 1)
SURFACE = (0.157, 0.157, 0.157, 1)
WHITE = (1, 1, 1, 1)
MUTED = (0.702, 0.702, 0.702, 1)
FAINT = (0.42, 0.42, 0.42, 1)
ACCENT = (0.118, 0.843, 0.376, 1)
DARK_TEXT = (0.071, 0.071, 0.071, 1)
CLEAR = (1, 1, 1, 0)
FONT_REGULAR = FONT_BOLD = "0"  # set from meta("Fonts/...") by the app generator; "0" = engine default font


# ------------------------------------------------------------------ files and GUIDs

def h63(text):
    return (int(hashlib.sha1(text.encode("utf-8")).hexdigest()[:16], 16) & MAX63) or 1


def write_if_changed(path, data):
    """Atomic, change-only write: an open Editor hot-reloads assets and must never see a half-written file."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path):
        with open(path, "rb") as f:
            if f.read() == data:
                return
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def dump(data):
    return (json.dumps(data, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def meta(rel):
    """GUID of Assets/<rel>; creates the .meta if missing and never changes an existing GUID (scenes reference it)."""
    path = os.path.join(ASSETS, rel) + ".meta"
    if os.path.exists(path):
        try:
            with open(path, encoding="utf-8") as f:
                guid = str(json.load(f)["AssetGUID"])
            if guid.isdigit() and int(guid) != 0:
                return guid
        except (ValueError, KeyError):
            pass
    ext = os.path.splitext(rel)[1].lower()
    guid = str(h63("asset:" + rel.replace("\\", "/")))
    data = {"AssetGUID": guid, "uuid": guid, "kind": KINDS.get(ext, "Other")}
    if ext == ".png":
        data["import"] = {"filter": "Bilinear", "wrapU": "Clamp", "wrapV": "Clamp"}
    write_if_changed(path, dump(data))
    return guid


# ------------------------------------------------------------------ textures (white; tinted by Image colour)

def _save_png(img, name):
    guid = meta(f"Textures/{name}.png")
    path = os.path.join(TEXTURES, name + ".png")
    os.makedirs(TEXTURES, exist_ok=True)
    tmp = path + ".tmp"
    img.save(tmp, "PNG")
    if os.path.exists(path) and open(path, "rb").read() == open(tmp, "rb").read():
        os.remove(tmp)
    else:
        os.replace(tmp, path)
    return guid


def _canvas(w, h):
    img = Image.new("RGBA", (w * SS, h * SS), (255, 255, 255, 0))
    return img, ImageDraw.Draw(img)


def rr(w, h, r):
    """Rounded rectangle texture at exactly w x h, one per size. Alternative: a 9-sliced Image ("imageType": 1,
    "border": [r, r, r, r]) stretches one small rounded sprite to any size."""
    name = f"RR_{w}x{h}_{r}"
    if os.path.exists(os.path.join(TEXTURES, name + ".png")):
        return meta(f"Textures/{name}.png")
    img, d = _canvas(w, h)
    d.rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], r * SS, fill=(255, 255, 255, 255))
    return _save_png(img.resize((w, h), Image.LANCZOS), name)


def circle_tex():
    if os.path.exists(os.path.join(TEXTURES, "Circle.png")):
        return meta("Textures/Circle.png")
    img, d = _canvas(160, 160)
    d.ellipse([0, 0, 160 * SS - 1, 160 * SS - 1], fill=(255, 255, 255, 255))
    return _save_png(img.resize((160, 160), Image.LANCZOS), "Circle")


def fade_tex(name="FadeDown", down=True):
    """4x256 alpha ramp: tint it for page gradients (FadeDown) or a darkening behind the tab bar (FadeUp)."""
    if not os.path.exists(os.path.join(TEXTURES, name + ".png")):
        img = Image.new("RGBA", (4, 256))
        for y in range(256):
            a = 255 - y if down else y
            for x in range(4):
                img.putpixel((x, y), (255, 255, 255, a))
        _save_png(img, name)
    return meta(f"Textures/{name}.png")


def band_tex():
    """Slider track: an 8-unit bar centred in a 64-unit tall texture, so the touch area is tall but the bar is thin."""
    if not os.path.exists(os.path.join(TEXTURES, "Track.png")):
        img, d = _canvas(16, 64)
        d.rectangle([0, 28 * SS, 16 * SS, 36 * SS - 1], fill=(255, 255, 255, 255))
        _save_png(img.resize((16, 64), Image.LANCZOS), "Track")
    return meta("Textures/Track.png")


def icon(name, draw):
    """128x128 white icon drawn by `draw(d, s)` where s(v) scales design units to the supersampled canvas."""
    path = os.path.join(TEXTURES, f"Icon{name}.png")
    if os.path.exists(path):
        return meta(f"Textures/Icon{name}.png")
    img, d = _canvas(128, 128)
    draw(d, lambda v: v * SS)
    return _save_png(img.resize((128, 128), Image.LANCZOS), f"Icon{name}")


def _line(d, s, pts, width):
    d.line([(s(x), s(y)) for x, y in pts], fill=(255, 255, 255, 255), width=int(s(width)), joint="curve")
    for x, y in (pts[0], pts[-1]):
        r = width / 2
        d.ellipse([s(x - r), s(y - r), s(x + r), s(y + r)], fill=(255, 255, 255, 255))


# A starter icon set; draw app-specific icons the same way.
ICONS = {
    "Plus": lambda d, s: (_line(d, s, [(64, 22), (64, 106)], 11), _line(d, s, [(22, 64), (106, 64)], 11)),
    "ChevronLeft": lambda d, s: _line(d, s, [(82, 24), (42, 64), (82, 104)], 11),
    "ChevronDown": lambda d, s: _line(d, s, [(26, 46), (64, 84), (102, 46)], 11),
    "More": lambda d, s: [d.ellipse([s(x - 9), s(55), s(x + 9), s(73)], fill=(255, 255, 255, 255)) for x in (30, 64, 98)],
    "Check": lambda d, s: _line(d, s, [(26, 66), (52, 92), (104, 38)], 12),
    "Close": lambda d, s: (_line(d, s, [(36, 36), (92, 92)], 10), _line(d, s, [(92, 36), (36, 92)], 10)),
    "Search": lambda d, s: (d.ellipse([s(18), s(18), s(86), s(86)], outline=(255, 255, 255, 255), width=int(s(11))),
                            _line(d, s, [(78, 78), (108, 108)], 13)),
    # One closed shape so the filled icon has no seams; the door is cut back out.
    "Home": lambda d, s: (d.polygon([(s(x), s(y)) for x, y in [(64, 12), (118, 60), (104, 60), (104, 112), (24, 112), (24, 60), (10, 60)]],
                                    fill=(255, 255, 255, 255)),
                          d.rectangle([s(54), s(80), s(74), s(113)], fill=(255, 255, 255, 0))),
    "Gear": lambda d, s: ([d.ellipse([s(64 + math.cos(a) * 44 - 11), s(64 + math.sin(a) * 44 - 11),
                                       s(64 + math.cos(a) * 44 + 11), s(64 + math.sin(a) * 44 + 11)], fill=(255, 255, 255, 255))
                           for a in [math.pi * i / 4 for i in range(8)]],
                          d.ellipse([s(24), s(24), s(104), s(104)], fill=(255, 255, 255, 255)),
                          d.ellipse([s(48), s(48), s(80), s(80)], fill=(255, 255, 255, 0))),
    "Trash": lambda d, s: (d.rounded_rectangle([s(30), s(40), s(98), s(112)], s(8), fill=(255, 255, 255, 255)),
                           d.rounded_rectangle([s(20), s(26), s(108), s(36)], s(4), fill=(255, 255, 255, 255))),
}


def ic(name):
    return icon(name, ICONS[name])


# ------------------------------------------------------------------ scene building

class Scene:
    """Entities are addressed by path ("Canvas/Pages/Home"); uuids derive from the path, so regenerating is stable."""

    def __init__(self, name):
        self.name = name
        self.entities = []

    def uid(self, path):
        return str(h63(f"{self.name}/{path}"))

    def add(self, path, **components):
        e = {"name": path.split("/")[-1], "uuid": self.uid(path)}
        if "/" in path:
            e["parentUuid"] = self.uid(path.rsplit("/", 1)[0])
        if components.pop("disabled", False):
            e["disabled"] = True
        scripts = components.pop("scripts", None)
        e.update(components)
        if scripts:
            e["Scripts"] = [{"className": c, "type": "Managed"} for c in scripts]
            fields = {c: [{"name": n, "type": t, "value": v} for (n, t, v) in fs] for c, fs in scripts.items() if fs}
            if fields:
                e["ScriptFields"] = fields
        self.entities.append(e)
        return path

    def save(self, rel="Scenes/Main.scene"):
        guid = meta(rel)
        write_if_changed(os.path.join(ASSETS, rel), dump({"version": 1, "name": self.name, "systems": [], "entities": self.entities}))
        return guid


def rect(x=0, y=0, w=100, h=100, ax=0.5, ay=0.5, px=None, py=None, **flags):
    """flags: fillWidth / fillHeight / fillParent (span the parent), ignoreSafeArea (backgrounds reaching under bars)."""
    r = {"anchorMinX": ax, "anchorMinY": ay, "anchorMaxX": ax, "anchorMaxY": ay,
         "pivotX": ax if px is None else px, "pivotY": ay if py is None else py,
         "posX": x, "posY": y, "sizeX": w, "sizeY": h}
    r.update(flags)
    return r


def top(x, y, w, h, ax=0.0, **flags):
    return rect(x, y, w, h, ax, 1, ax, 1, **flags)


def bottom(x, y, w, h, ax=0.5, **flags):
    return rect(x, y, w, h, ax, 0, ax, 0, **flags)


def image(texture, sort, color=WHITE):
    return {"textureAsset": texture, "sortOrder": sort, "r": color[0], "g": color[1], "b": color[2], "a": color[3]}


def text(t, size, sort, bold=False, align=0, color=WHITE, wrap=False, valign=1):
    """align 0/1/2 = left/center/right; valign 0/1/2 = top/middle/bottom."""
    return {"text": t, "fontAsset": FONT_BOLD if bold else FONT_REGULAR, "fontSize": size, "alignment": align,
            "valign": valign, "wrapMode": 1 if wrap else 0, "letterSpacing": 0.0,
            "r": color[0], "g": color[1], "b": color[2], "a": color[3], "sortOrder": sort}


def button(s, target, cls, method, arg=None):
    """Button whose click calls cls.method(arg) on the script attached to entity `target`."""
    binding = {"entity": s.uid(target), "class": cls, "method": method, "enabled": True,
               "argKind": 0 if arg is None else 2, "argValue": "" if arg is None else str(arg)}
    return {"Interactable": {"interactable": True, "focusable": False},
            "Button": {"transitionMode": 2, "targetGraphic": "0", "onClick": [binding]}}


def blocker():
    """Only Interactables hit-test: give panels and dim layers one so taps never fall through to what's behind."""
    return {"Interactable": {"interactable": True, "focusable": False}}


def _color(c):
    return {"r": c[0], "g": c[1], "b": c[2], "a": c[3]}


def input_field(s, path, placeholder, text_color=WHITE, placeholder_color=FAINT, limit=128):
    """Add a child "<path>/Text" with a TextRenderer; Android shows the soft keyboard on focus."""
    return {"Interactable": {"interactable": True, "focusable": True},
            "InputField": {"text": "", "placeholder": placeholder, "textEntity": s.uid(f"{path}/Text"),
                           "characterLimit": limit, "contentType": 0, "transitionMode": 2,
                           "textColor": _color(text_color), "placeholderColor": _color(placeholder_color),
                           "caretColor": _color(ACCENT), "selectionColor": _color((ACCENT[0], ACCENT[1], ACCENT[2], 0.35))}}


def icon_button(s, path, icon_guid, x, y, size, icon_size, target, cls, method, sort, ax=0.5, ay=0.5,
                tint=WHITE, bg=CLEAR, arg=None):
    s.add(path, RectTransform2D=rect(x, y, size, size, ax, ay, 0.5, 0.5), Image=image(circle_tex(), sort, bg),
          **button(s, target, cls, method, arg))
    s.add(f"{path}/Icon", RectTransform2D=rect(0, 0, icon_size, icon_size), Image=image(icon_guid, sort + 1, tint))


def pill(s, path, label, x, y, w, h, fill, label_color, target, cls, method, sort, ax=0.0, size=30):
    s.add(path, RectTransform2D=top(x, y, w, h, ax), Image=image(rr(w, h, h // 2), sort, fill), **button(s, target, cls, method))
    s.add(f"{path}/Label", RectTransform2D=rect(0, 0, w - 40, h), TextRenderer=text(label, size, sort + 1, True, 1, label_color))


def scroll(s, path, horizontal=False):
    """ScrollRect + Mask + Interactable on a viewport whose child "<path>/Content" holds the items.
    The viewport is set explicitly: left unset, the engine picks the first child (the content) as viewport."""
    return {"Mask": {"showMaskGraphic": False},
            "Interactable": {"interactable": True, "focusable": False},
            "ScrollRect": {"content": s.uid(f"{path}/Content"), "viewport": s.uid(path), "horizontal": horizontal,
                           "vertical": not horizontal, "movementType": 1, "elasticity": 0.1, "inertia": True,
                           "decelerationRate": 0.135, "scrollSensitivity": 8, "verticalScrollbarVisibility": 1,
                           "horizontalScrollbarVisibility": 1}}


def fit_height(top_offset, bottom_offset):
    return {"FitHeight": [("Top", "float", str(top_offset)), ("Bottom", "float", str(bottom_offset))]}


# ------------------------------------------------------------------ building blocks

def root_canvas(s, scripts=None):
    s.add("Canvas", RectTransform2D=rect(0, 0, 1080, 1920, fillParent=True), scripts=scripts or {})
    s.add("Canvas/Background", RectTransform2D=rect(0, 0, 6000, 6000, ignoreSafeArea=True), Image=image(SQUARE, 0, BG))
    s.add("Canvas/Pages", RectTransform2D=top(0, 0, 1080, 1920, 0.5, fillWidth=True))


def page(s, name, scripts=None):
    path = f"Canvas/Pages/{name}"
    s.add(path, RectTransform2D=top(0, 0, 1080, 1920, 0.5, fillWidth=True), scripts=scripts or {})
    return path


def tab_bar(s, tabs, height=156, sort=125):
    """tabs: [(PageName, icon_guid, label)]. Calls Router.SelectTab(i) on the canvas."""
    s.add("Canvas/NavFade", RectTransform2D=bottom(0, 0, 1080, height + 200, fillWidth=True, ignoreSafeArea=True),
          Image=image(fade_tex("FadeUp", down=False), sort - 15, (0, 0, 0, 1)))
    s.add("Canvas/Nav", RectTransform2D=bottom(0, 0, 1080, height, fillWidth=True), Image=image(SQUARE, sort, CLEAR), **blocker())
    width = 1080 / len(tabs)
    for i, (name, ico, label) in enumerate(tabs):
        t = f"Canvas/Nav/{name}"
        s.add(t, RectTransform2D=rect((i - (len(tabs) - 1) / 2) * width, 0, width, height), Image=image(SQUARE, sort + 1, CLEAR),
              **button(s, "Canvas", "Router", "SelectTab", i))
        s.add(f"{t}/Icon", RectTransform2D=rect(0, 24, 60, 60), Image=image(ico, sort + 2, MUTED))
        s.add(f"{t}/Label", RectTransform2D=rect(0, -40, width - 20, 36), TextRenderer=text(label, 22, sort + 2, True, 1, MUTED))


def virtual_list(s, path, script, top_offset, bottom_offset, row_height, pool, build_row, header=None, sort=10):
    """Viewport + Content + `pool` recycled rows (Content/Row0..). `script` is the VirtualList subclass on the
    viewport; rows call script.OnRowClicked(slot). build_row(row_path, slot) adds the row's children."""
    scripts = {script: [("RowHeight", "float", str(row_height)), ("HeaderHeight", "float", str(header[0] if header else 0))]}
    scripts.update(fit_height(top_offset, bottom_offset))
    s.add(path, RectTransform2D=top(0, -top_offset, 1080, 1200, 0.5, fillWidth=True), Image=image(SQUARE, 1, CLEAR),
          scripts=scripts, **scroll(s, path))
    s.add(f"{path}/Content", RectTransform2D=top(0, 0, 1080, 0, 0.5))
    if header:
        header[1](f"{path}/Content")
    for i in range(pool):
        row = f"{path}/Content/Row{i}"
        s.add(row, RectTransform2D=top(0, -i * row_height, 1080, row_height, 0.5), Image=image(SQUARE, sort, CLEAR),
              disabled=True, **button(s, path, script, "OnRowClicked", i))
        build_row(row, i)


def action_sheet(s, options=6, sort=300):
    a = "Canvas/ActionSheet"
    s.add(a, RectTransform2D=rect(0, 0, 1080, 1920, fillParent=True), scripts={"ActionSheet": []})
    s.add(f"{a}/Dim", RectTransform2D=rect(0, 0, 6000, 6000, ignoreSafeArea=True), Image=image(SQUARE, sort, (0, 0, 0, 0.6)),
          **button(s, a, "ActionSheet", "Close"))
    p = f"{a}/Panel"
    s.add(p, RectTransform2D=bottom(0, -60, 1080, 900, fillWidth=True), Image=image(SQUARE, sort + 1, SURFACE), **blocker())
    s.add(f"{p}/Handle", RectTransform2D=top(0, -22, 90, 10, 0.5), Image=image(rr(90, 10, 5), sort + 2, FAINT))
    s.add(f"{p}/Title", RectTransform2D=top(48, -70, 984, 50), TextRenderer=text("", 32, sort + 3, True))
    s.add(f"{p}/Subtitle", RectTransform2D=top(48, -122, 984, 40), TextRenderer=text("", 26, sort + 3, color=MUTED))
    s.add(f"{p}/Divider", RectTransform2D=top(0, -180, 1080, 2, 0.5, fillWidth=True), Image=image(SQUARE, sort + 2, (1, 1, 1, 0.1)))
    for i in range(options):
        o = f"{p}/Option{i}"
        s.add(o, RectTransform2D=top(0, -196 - i * 132, 1080, 132, 0.5, fillWidth=True), Image=image(SQUARE, sort + 2, CLEAR),
              disabled=True, **button(s, a, "ActionSheet", "OnOption", i))
        s.add(f"{o}/Icon", RectTransform2D=rect(84, 0, 56, 56, 0, 0.5, 0.5, 0.5), Image=image(ic("Plus"), sort + 3, MUTED))
        s.add(f"{o}/Label", RectTransform2D=rect(150, 0, 880, 56, 0, 0.5), TextRenderer=text("", 32, sort + 3))


def input_dialog(s, sort=340):
    d = "Canvas/InputDialog"
    s.add(d, RectTransform2D=rect(0, 0, 1080, 1920, fillParent=True), scripts={"InputDialog": []})
    s.add(f"{d}/Background", RectTransform2D=rect(0, 0, 6000, 6000, ignoreSafeArea=True), Image=image(SQUARE, sort, (0.2, 0.2, 0.2, 0.98)), **blocker())
    s.add(f"{d}/Title", RectTransform2D=rect(0, 300, 960, 150), TextRenderer=text("", 46, sort + 1, True, 1, wrap=True))
    f = f"{d}/Field"
    s.add(f, RectTransform2D=rect(0, 120, 936, 130), Image=image(SQUARE, sort + 1, CLEAR), **input_field(s, f, "", WHITE, (1, 1, 1, 0.35), 80))
    s.add(f"{f}/Text", RectTransform2D=rect(0, 0, 936, 110), TextRenderer=text("", 60, sort + 2, True, 1))
    s.add(f"{d}/Underline", RectTransform2D=rect(0, 46, 936, 4), Image=image(SQUARE, sort + 1, (1, 1, 1, 0.5)))
    s.add(f"{d}/Cancel", RectTransform2D=rect(-180, -110, 320, 104), Image=image(rr(320, 104, 52), sort + 1, (0.25, 0.25, 0.25, 1)),
          **button(s, d, "InputDialog", "Cancel"))
    s.add(f"{d}/Cancel/Label", RectTransform2D=rect(0, 0, 300, 104), TextRenderer=text("Abbrechen", 30, sort + 2, True, 1))
    s.add(f"{d}/Confirm", RectTransform2D=rect(180, -110, 320, 104), Image=image(rr(320, 104, 52), sort + 1, ACCENT),
          **button(s, d, "InputDialog", "Confirm"))
    s.add(f"{d}/Confirm/Label", RectTransform2D=rect(0, 0, 300, 104), TextRenderer=text("OK", 30, sort + 2, True, 1, DARK_TEXT))


def toast(s, y=200, sort=400):
    s.add("Canvas/Toast", RectTransform2D=bottom(0, y, 900, 96), Image=image(rr(900, 96, 16), sort, WHITE), scripts={"Toast": []})
    s.add("Canvas/Toast/Label", RectTransform2D=rect(0, 0, 860, 96), TextRenderer=text("", 28, sort + 1, True, 1, DARK_TEXT))


def camera(s):
    s.add("Main Camera", Transform2D={"posX": 0, "posY": 0, "rotation": 0, "scaleX": 1, "scaleY": 1},
          Camera2D={"orthoSize": 5, "zoom": 1, "clearR": BG[0], "clearG": BG[1], "clearB": BG[2], "clearA": 1})


def mobile_project_settings(scene_name, android_options=None):
    """project.json for a portrait phone app + BuildProfiles/Android.build. android_options: platformOptions dict,
    e.g. {"permissions": "READ_MEDIA_AUDIO, READ_EXTERNAL_STORAGE:32", "backgroundAudio": "true"}."""
    path = os.path.join(ROOT, "project.json")
    with open(path, encoding="utf-8") as f:
        p = json.load(f)
    p.update({
        "startupScene": scene_name, "lastOpenedScene": scene_name, "buildScenes": [scene_name],
        "gameViewAspect": "9:16", "buildWidth": 540, "buildHeight": 1000, "buildFullscreen": False, "buildResizable": True,
        "mobileOrientation": "Portrait",
        # Width-driven scale: every phone shows the 1080-wide layout edge to edge; height varies and pages stretch.
        "uiReferenceWidth": 1080, "uiReferenceHeight": 1920, "uiScaleMode": "MatchWidthOrHeight", "uiScaleMatch": 0.0,
        "showRuntimeStats": False, "showRuntimeLogs": False,
    })
    p.setdefault("splashScreen", {}).update({"backgroundR": BG[0], "backgroundG": BG[1], "backgroundB": BG[2]})
    write_if_changed(path, dump(p))
    profile_path = os.path.join(ROOT, "BuildProfiles", "Android.build")
    if os.path.exists(profile_path):
        with open(profile_path, encoding="utf-8") as f:
            android = json.load(f)
    else:
        android = {"name": "Android", "platform": "Android", "renderBackend": "Auto"}
    if android_options:
        android.setdefault("platformOptions", {}).update(android_options)
    write_if_changed(profile_path, dump(android))
