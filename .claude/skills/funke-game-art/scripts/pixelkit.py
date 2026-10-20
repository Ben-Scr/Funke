#!/usr/bin/env python3
"""Game-art building blocks for Funke projects (funke-game-art skill).

Import it from a project's art generator (Tools/generate_art.py):

    sys.path.insert(0, r"<skill>/scripts"); import pixelkit as pk
    pal = pk.Palette.load(project / "Design/palette.json")
    img = pk.pixel_panel(24, 24, pal)                    # 9-slice-able pixel frame, border = 3 art px
    pk.save_texture(project / "Assets/Textures/UI/Panel.png", pk.scale(img, 3), project=project)

Or run it:
    pixelkit.py sample <project>          Design/style-sample.png from Design/palette.json (for approval)
    pixelkit.py ramp "#4C8C4A" [-n 5]     hue-shifted ramp dark -> light
    pixelkit.py check <png> [--project P] [--scale k]   palette / soft-edge / pixel-grid lint for pixel art
    pixelkit.py scale <png> <k> [-o out]  nearest-neighbour upscale
    pixelkit.py sheet <out.png> <frame.png ...> [--cols N]   pack frames + write named slices into the .meta
    pixelkit.py meta <asset> [--filter Point|Bilinear]       write/merge a .meta, keeping an existing GUID

Needs Pillow. Files are written atomically and only when the bytes change (an open Editor hot-reloads every write),
and a texture's .meta is written before the PNG so the Editor never invents a random GUID for it.
"""
from __future__ import annotations

import argparse
import colorsys
import hashlib
import json
import os
import sys
from dataclasses import dataclass, field
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover
    sys.exit("pixelkit needs Pillow: python -m pip install pillow")

RGBA = tuple[int, int, int, int]
MAX_GUID = (1 << 63) - 1


# ----------------------------------------------------------------------------- colour

def rgba(value) -> RGBA:
    """'#RRGGBB', '#RRGGBBAA', (r,g,b) or (r,g,b,a) -> (r,g,b,a) 0..255."""
    if isinstance(value, str):
        h = value.lstrip("#")
        if len(h) == 6:
            h += "ff"
        if len(h) != 8:
            raise ValueError(f"bad colour {value!r}")
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4, 6))  # type: ignore[return-value]
    t = tuple(int(c) for c in value)
    return (t + (255,))[:4] if len(t) == 3 else t  # type: ignore[return-value]


def hexstr(c: RGBA) -> str:
    return "#%02X%02X%02X" % c[:3] + ("" if c[3] == 255 else "%02X" % c[3])


def mul(c: RGBA, k: float) -> RGBA:
    return (min(255, round(c[0] * k)), min(255, round(c[1] * k)), min(255, round(c[2] * k)), c[3])


def mix(a: RGBA, b: RGBA, t: float) -> RGBA:
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(4))  # type: ignore[return-value]


def _shift_hue(h: float, target: float, degrees: float) -> float:
    d = ((target - h + 0.5) % 1.0) - 0.5          # shortest way round the colour wheel
    step = min(abs(d), degrees / 360.0)
    return (h + step * (1 if d > 0 else -1)) % 1.0


def ramp(base, n: int = 5, spread: float = 0.55, hue_shift: float = 18.0) -> list[RGBA]:
    """Dark -> light ramp around `base` (the middle step). Shadows drift toward blue-violet and lose a little
    saturation at the very bottom; highlights drift toward warm yellow and desaturate. That hue shift is what makes
    pixel art look painted instead of 'base colour times brightness'."""
    r, g, b, a = rgba(base)
    h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
    out: list[RGBA] = []
    for i in range(n):
        t = (i / (n - 1) - 0.5) * 2 if n > 1 else 0.0          # -1 .. 1
        li = min(0.96, max(0.04, l + t * spread * (0.5 if t > 0 else 0.6)))
        hi = _shift_hue(h, 0.70 if t < 0 else 0.14, hue_shift * abs(t))
        si = min(1.0, max(0.0, s * (1.0 + 0.15 * t if t < 0 else 1.0 - 0.35 * t)))
        rr, gg, bb = colorsys.hls_to_rgb(hi, li, si)
        out.append((round(rr * 255), round(gg * 255), round(bb * 255), a))
    return out


@dataclass
class Palette:
    name: str = "palette"
    tokens: dict[str, RGBA] = field(default_factory=dict)
    ramps: dict[str, list[RGBA]] = field(default_factory=dict)
    raw: dict = field(default_factory=dict)

    @staticmethod
    def load(path: Path | str) -> "Palette":
        data = json.loads(Path(path).read_text(encoding="utf-8-sig"))
        return Palette(name=data.get("name", Path(path).stem),
                       tokens={k: rgba(v) for k, v in data.get("tokens", {}).items()},
                       ramps={k: [rgba(v) for v in vs] for k, vs in data.get("ramps", {}).items()},
                       raw=data)

    def __getitem__(self, key: str) -> RGBA:
        if key in self.tokens:
            return self.tokens[key]
        name, _, idx = key.partition(".")                      # "grass.2" = third step of ramp grass
        if idx.isdigit() and (name in self.ramps or name in self.tokens):
            return self.ramp_of(name)[int(idx)]                # tokens get a derived 5-step ramp
        raise KeyError(f"palette has no token {key!r} (tokens: {', '.join(self.tokens)})")

    def get(self, key: str, default) -> RGBA:
        try:
            return self[key]
        except KeyError:
            return rgba(default)

    def ramp_of(self, token: str, n: int = 5) -> list[RGBA]:
        return self.ramps.get(token) or ramp(self[token], n)

    def all_colours(self) -> set[tuple[int, int, int]]:
        """Tokens, explicit ramps and the derived ramp of every token (what the kit's helpers draw with)."""
        cols = {c[:3] for c in self.tokens.values()}
        for vs in self.ramps.values():
            cols |= {c[:3] for c in vs}
        for t in self.tokens:
            cols |= {c[:3] for c in self.ramp_of(t)}
        return cols


# ----------------------------------------------------------------------------- pixel drawing

def new(w: int, h: int, fill=(0, 0, 0, 0)) -> Image.Image:
    return Image.new("RGBA", (w, h), rgba(fill))


def from_grid(grid: str, legend: dict[str, str], pal: Palette | None = None) -> Image.Image:
    """Sprite from a text grid: one character per pixel, '.' or ' ' = transparent. `legend` maps characters to
    palette keys ("ink", "primary", "grass.3") or hex colours. Rows may differ in length (padded right).

        SLIME = '''
        ..kkkk..
        .k3443k.
        k332233k
        kkkkkkkk
        '''
        img = from_grid(SLIME, {"k": "ink", "2": "primary.1", "3": "primary.2", "4": "primary.3"}, pal)
    """
    rows = [r.strip() for r in grid.strip("\n").splitlines() if r.strip()]
    w, h = max(len(r) for r in rows), len(rows)
    img = new(w, h)
    cache: dict[str, RGBA] = {}
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch in ". ":
                continue
            if ch not in cache:
                if ch not in legend:
                    raise KeyError(f"grid character {ch!r} (row {y}) is not in the legend")
                v = legend[ch]
                cache[ch] = rgba(v) if v.startswith("#") or pal is None else pal[v]
            img.putpixel((x, y), cache[ch])
    return img


def mirror_x(img: Image.Image) -> Image.Image:
    """Draw half a symmetric sprite, mirror the rest (odd widths: the last column is shared)."""
    return img.transpose(Image.FLIP_LEFT_RIGHT)


def scale(img: Image.Image, k: int) -> Image.Image:
    """Nearest-neighbour upscale: art pixels -> texture pixels. Never scale pixel art with any other filter."""
    return img if k == 1 else img.resize((img.width * k, img.height * k), Image.NEAREST)


def outline(img: Image.Image, colour, diagonal: bool = False) -> Image.Image:
    """1 px outline around every opaque pixel (grows the image by 1 px on each side)."""
    src = img.convert("RGBA")
    out = new(src.width + 2, src.height + 2)
    out.paste(src, (1, 1), src)
    a = src.getchannel("A").load()
    o = out.load()
    c = rgba(colour)
    nbrs = [(-1, 0), (1, 0), (0, -1), (0, 1)] + ([(-1, -1), (1, -1), (-1, 1), (1, 1)] if diagonal else [])
    for y in range(out.height):
        for x in range(out.width):
            if o[x, y][3]:
                continue
            for dx, dy in nbrs:
                sx, sy = x - 1 + dx, y - 1 + dy
                if 0 <= sx < src.width and 0 <= sy < src.height and a[sx, sy] > 127:
                    o[x, y] = c
                    break
    return out


def bevel_rect(img: Image.Image, box: tuple[int, int, int, int], body, light, shadow, width: int = 1) -> None:
    """Raised block lit from the top-left: light edge top/left, shadow edge bottom/right. Swap light/shadow for an
    inset (recessed slot, well)."""
    x0, y0, x1, y1 = box                                       # inclusive
    d = ImageDraw.Draw(img)
    d.rectangle(box, fill=rgba(body))
    for i in range(width):
        d.line([(x0 + i, y1 - i), (x0 + i, y0 + i), (x1 - i, y0 + i)], fill=rgba(light))
        d.line([(x0 + i + 1, y1 - i), (x1 - i, y1 - i), (x1 - i, y0 + i + 1)], fill=rgba(shadow))


def pixel_panel(w: int, h: int, pal: Palette, body: str = "surface", ink: str = "ink",
                cut: int = 1, inset: bool = False) -> Image.Image:
    """Pixel-art frame for 9-slice: 1 px ink outline with cut corners, 1 px bevel inside, flat body.
    9-slice border = cut + 2 art px on every side (use 3 for cut=1)."""
    img = new(w, h)
    r = pal.ramp_of(body) if body in pal.tokens or body in pal.ramps else ramp("#3D2A45")
    b, light, shadow = r[2], r[3], r[1]
    if inset:
        light, shadow = shadow, light
    bevel_rect(img, (1, 1, w - 2, h - 2), b, light, shadow)
    d = ImageDraw.Draw(img)
    k = rgba(pal.get(ink, "#1B1A2E"))
    d.rectangle((0, 0, w - 1, h - 1), outline=k)
    for i in range(cut):                                       # cut corners: transparent steps
        for (cx, cy) in ((i, 0), (0, i), (w - 1 - i, 0), (w - 1, i), (i, h - 1), (0, h - 1 - i),
                         (w - 1 - i, h - 1), (w - 1, h - 1 - i)):
            img.putpixel((cx, cy), (0, 0, 0, 0))
        for (cx, cy) in ((i + 1, 1), (w - 2 - i, 1), (i + 1, h - 2), (w - 2 - i, h - 2)):
            img.putpixel((cx, cy), k)
    return img


def pixel_button(w: int, h: int, pal: Palette, colour: str = "primary", ink: str = "ink",
                 pressed: bool = False, lip: int = 2) -> Image.Image:
    """Raised pixel button with a darker bottom lip; the pressed variant drops the lip and moves the face down,
    so swapping the two textures reads as a press. 9-slice border: 3 left/right/top, 3 + lip bottom."""
    img = new(w, h)
    r = pal.ramp_of(colour) if colour in pal.tokens or colour in pal.ramps else ramp("#E26D5C")
    c = r[2]
    face_top = 1 + (lip if pressed else 0)
    face_bottom = h - 2 - (0 if pressed else lip)
    d = ImageDraw.Draw(img)
    if not pressed:
        d.rectangle((1, face_bottom + 1, w - 2, h - 2), fill=r[0])
    bevel_rect(img, (1, face_top, w - 2, face_bottom), c, r[3], r[1])
    k = rgba(pal.get(ink, "#1B1A2E"))
    d.rectangle((0, face_top - 1, w - 1, h - 1), outline=k)
    for (cx, cy) in ((0, face_top - 1), (w - 1, face_top - 1), (0, h - 1), (w - 1, h - 1)):
        img.putpixel((cx, cy), (0, 0, 0, 0))
    return img


def rounded_panel(w: int, h: int, fill, radius: int, outline_colour=None, outline_w: int = 0,
                  lip: int = 0, lip_colour=None, gradient: float = 0.0, ss: int = 4) -> Image.Image:
    """Smooth (non-pixel) UI shape at its final size, 4x supersampled: rounded rect, optional outline, bottom lip
    and vertical gradient (top lighter by `gradient`). Draw it white and tint it with the Button/Image colour when
    one sprite should serve several colours. 9-slice border = radius + outline_w (+ lip at the bottom)."""
    W, H, R, O, L = w * ss, h * ss, radius * ss, outline_w * ss, lip * ss
    big = new(W, H)
    d = ImageDraw.Draw(big)
    f = rgba(fill)
    if L:
        d.rounded_rectangle((0, L, W - 1, H - 1), R, fill=rgba(lip_colour) if lip_colour else mul(f, 0.65))
    if O:
        d.rounded_rectangle((0, 0, W - 1, H - 1 - L), R, fill=rgba(outline_colour))
    d.rounded_rectangle((O, O, W - 1 - O, H - 1 - L - O), max(0, R - O), fill=f)
    if gradient:
        top = mix(f, (255, 255, 255, 255), gradient)
        grad = new(W, H)
        gd = ImageDraw.Draw(grad)
        for y in range(H):
            gd.line([(0, y), (W, y)], fill=mix(top, f, min(1.0, y / max(1, H - L))))
        mask = new(W, H)
        ImageDraw.Draw(mask).rounded_rectangle((O, O, W - 1 - O, H - 1 - L - O), max(0, R - O), fill=(255,) * 4)
        big.paste(grad, (0, 0), mask.getchannel("A"))
    return big.resize((w, h), Image.LANCZOS)


# ----------------------------------------------------------------------------- files

def _write_if_changed(path: Path, data: bytes) -> bool:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_bytes() == data:
        return False
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(data)
    os.replace(tmp, path)
    return True


def stable_guid(key: str) -> int:
    """Deterministic 63-bit GUID from a stable key (e.g. 'Textures/UI/Panel.png'): regenerating a deleted meta
    yields the same GUID, so scene references survive."""
    g = int.from_bytes(hashlib.sha1(("pixelkit:" + key).encode("utf-8")).digest()[:8], "little") & MAX_GUID
    return g or 1


def _kind(path: Path) -> str:
    ext = path.suffix.lower()
    if ext in (".png", ".jpg", ".jpeg", ".bmp", ".tga"):
        return "Texture"
    if ext in (".ttf", ".otf"):
        return "Font"
    if ext in (".wav", ".mp3", ".flac"):
        return "Audio"
    return {".dataasset": "DataAsset", ".prefab": "Prefab", ".scene": "Scene"}.get(ext, "Other")


def write_meta(asset: Path, project: Path | None = None, filter: str | None = None, wrap: str | None = None,
               sprites: list[dict] | None = None, guid: int | None = None,
               border: tuple[int, int, int, int] | None = None) -> int:
    """Create or merge `<asset>.meta`. An existing valid GUID always wins (something may reference it). New GUIDs
    are `stable_guid(<path relative to Assets/>)` unless `guid` is given. Returns the GUID."""
    asset = Path(asset)
    meta_path = asset.with_name(asset.name + ".meta")
    data: dict = {}
    if meta_path.exists():
        try:
            data = json.loads(meta_path.read_text(encoding="utf-8-sig"))
        except Exception:
            data = {}
    old = str(data.get("AssetGUID", ""))
    if old.isdigit() and int(old) > 0:
        g = int(old)
    else:
        if guid is None:
            rel = asset.resolve()
            if project is not None:
                try:
                    rel = asset.resolve().relative_to((Path(project) / "Assets").resolve())
                except ValueError:
                    pass
            guid = stable_guid(rel.as_posix())
        g = guid
    data["AssetGUID"] = str(g)
    data["uuid"] = str(g)
    data["kind"] = _kind(asset)
    if filter or wrap:
        imp = data.setdefault("import", {})
        if filter:
            imp["filter"] = filter
        if wrap:
            imp["wrapU"] = imp["wrapV"] = wrap
    if sprites is not None:
        data["sprites"] = sprites
    if border is not None:                                     # [left, bottom, right, top] in texture px
        data["border"] = list(border)
    _write_if_changed(meta_path, (json.dumps(data, indent=2) + "\n").encode("utf-8"))
    return g


def bleed(img: Image.Image, passes: int = 4) -> Image.Image:
    """Copy edge colours into fully transparent neighbours (alpha stays 0). Funke blends straight alpha, so under
    Bilinear filtering and mips the RGB of transparent texels shows as a dark fringe around smooth art."""
    out = img.convert("RGBA").copy()
    px = out.load()
    w, h = out.size
    for _ in range(passes):
        updates = []
        for y in range(h):
            for x in range(w):
                if px[x, y][3]:
                    continue
                acc = [0, 0, 0, 0]
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h and (px[nx, ny][3] or px[nx, ny][:3] != (0, 0, 0)):
                        c = px[nx, ny]
                        acc[0] += c[0]; acc[1] += c[1]; acc[2] += c[2]; acc[3] += 1
                if acc[3]:
                    updates.append((x, y, (acc[0] // acc[3], acc[1] // acc[3], acc[2] // acc[3], 0)))
        if not updates:
            break
        for x, y, c in updates:
            px[x, y] = c
    return out


def save_texture(path: Path | str, img: Image.Image, project: Path | None = None, upscale: int = 4,
                 filter: str = "Bilinear", sprites: list[dict] | None = None,
                 border: tuple[int, int, int, int] | None = None, guid: int | None = None) -> int:
    """Save art drawn at its native size. Defaults = "sharp bilinear", the setup that stays crisp at any scale in
    Funke: the PNG is stored `upscale`x nearest-neighbour and imported Bilinear (blur limited to 1/upscale art px,
    no shimmer, no uneven pixels at non-integer UI/camera scales, no distortion from the half-texel slice inset).
    `sprites` rects and `border` ([l, b, r, t]) are given in ART pixels and scaled here. For smooth (non-pixel) UI
    art pass upscale=1; its transparent edge is colour-bled. Writes the .meta first, then the PNG (atomic, only on
    change). Returns the GUID. World sprites: SpriteRenderer.pixelsPerUnit = art px per unit * upscale."""
    path = Path(path)
    k = max(1, int(upscale))
    if k > 1:
        img = scale(img, k)
        if sprites is not None:
            sprites = [{**sp, "rect": [v * k for v in sp["rect"]]} for sp in sprites]
        if border is not None:
            border = tuple(v * k for v in border)  # type: ignore[assignment]
    elif filter != "Point":
        img = bleed(img)
    g = write_meta(path, project=project, filter=filter, wrap="Clamp", sprites=sprites, guid=guid, border=border)
    import io
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=True)
    _write_if_changed(path, buf.getvalue())
    return g


def pack_sheet(frames: list[tuple[str, Image.Image]], cols: int | None = None, extrude: int = 1,
               pivot: tuple[float, float] = (0.5, 0.5)) -> tuple[Image.Image, list[dict]]:
    """Grid-pack equally sized frames. Returns the sheet and the `.meta` "sprites" list (rect = [x, y, w, h] from
    the top-left, pivot 0..1 from the slice's bottom-left) — reference a slice by name in spriteName /
    SpriteAnimator2D frames. Every frame gets `extrude` art px of its own edge pixels around it: Bilinear
    sampling at a slice border would otherwise pick up the neighbouring frame and draw a thin line between
    tiles (seen in an eval run with tightly packed tiles)."""
    if not frames:
        raise ValueError("no frames")
    fw = max(f.width for _, f in frames)
    fh = max(f.height for _, f in frames)
    e = max(0, extrude)
    cw, ch = fw + 2 * e, fh + 2 * e
    cols = cols or len(frames)
    rows = (len(frames) + cols - 1) // cols
    sheet = new(cols * cw, rows * ch)
    sprites = []
    for i, (name, f) in enumerate(frames):
        cx, cy = (i % cols) * cw, (i // cols) * ch
        # each slice keeps its OWN size: padding a 16 px tile to a wider neighbour's size adds a transparent
        # column that shows as a dark line between tiles
        w, h = f.width, f.height
        cell = f.convert("RGBA")
        if e:
            padded = new(w + 2 * e, h + 2 * e)
            padded.paste(cell.crop((0, 0, w, 1)).resize((w, e), Image.NEAREST), (e, 0))
            padded.paste(cell.crop((0, h - 1, w, h)).resize((w, e), Image.NEAREST), (e, h + e))
            padded.paste(cell.crop((0, 0, 1, h)).resize((e, h), Image.NEAREST), (0, e))
            padded.paste(cell.crop((w - 1, 0, w, h)).resize((e, h), Image.NEAREST), (w + e, e))
            for (sx, sy, dx, dy) in ((0, 0, 0, 0), (w - 1, 0, w + e, 0), (0, h - 1, 0, h + e),
                                     (w - 1, h - 1, w + e, h + e)):
                padded.paste(cell.crop((sx, sy, sx + 1, sy + 1)).resize((e, e), Image.NEAREST), (dx, dy))
            padded.paste(cell, (e, e))
            cell = padded
        sheet.paste(cell, (cx, cy))
        sprites.append({"name": name, "rect": [cx + e, cy + e, w, h], "pivot": [pivot[0], pivot[1]]})
    return sheet, sprites


# ----------------------------------------------------------------------------- lint

def detect_pixel_scale(img: Image.Image) -> int:
    """Largest k such that the image is made of k x k blocks (k = how much the art was upscaled)."""
    from math import gcd
    px = img.convert("RGBA").load()
    g = 0
    for y in range(img.height):
        run = 1
        for x in range(1, img.width + 1):
            if x < img.width and px[x, y] == px[x - 1, y]:
                run += 1
            else:
                g = gcd(g, run)
                run = 1
        if g == 1:
            return 1
    for x in range(img.width):
        run = 1
        for y in range(1, img.height + 1):
            if y < img.height and px[x, y] == px[x, y - 1]:
                run += 1
            else:
                g = gcd(g, run)
                run = 1
        if g == 1:
            return 1
    return max(1, g)


def check(img_path: Path, pal: Palette | None, expect_scale: int | None) -> list[str]:
    img = Image.open(img_path).convert("RGBA")
    issues = []
    k = detect_pixel_scale(img)
    if expect_scale and k != expect_scale:
        issues.append(f"pixel grid: blocks of {k}x{k}, expected {expect_scale}x{expect_scale} "
                      "(mixed densities or a non-nearest resize)")
    alphas = [a for *_, a in img.getdata()]
    soft = sum(1 for a in alphas if 0 < a < 255)
    if soft:
        issues.append(f"{soft} semi-transparent pixels (anti-aliased edges look blurry in pixel art; "
                      "use 0 or 255 alpha)")
    cols = {p[:3] for p in img.getdata() if p[3] > 0}
    if pal is not None:
        allowed = pal.all_colours()
        off = cols - allowed
        if off:
            sample = ", ".join(hexstr(c + (255,)) for c in sorted(off)[:8])
            issues.append(f"{len(off)} of {len(cols)} colours are not in the palette ({sample}...)")
    if len(cols) > 64:
        issues.append(f"{len(cols)} distinct colours: noise or gradients rather than placed pixels?")
    return issues


# ----------------------------------------------------------------------------- style sample

def _font(project: Path, size: int, prefer: str = "") -> ImageFont.ImageFont:
    fonts = sorted((project / "Assets" / "Fonts").glob("*.[to]tf")) if (project / "Assets" / "Fonts").exists() else []
    if prefer:
        fonts = sorted(fonts, key=lambda p: prefer.lower() not in p.name.lower())
    for f in fonts:
        try:
            return ImageFont.truetype(str(f), size)
        except Exception:
            continue
    try:
        return ImageFont.truetype("arial.ttf", size)
    except Exception:
        return ImageFont.load_default()


def _blob(pal: Palette, colour: str) -> Image.Image:
    """16x12 slime shaded by the rules (ink outline, light from the top-left, hue-shifted ramp): a density probe."""
    r = pal.ramp_of(colour)
    ink = pal.get("ink", "#1B1A2E")
    img = new(16, 12)
    d = ImageDraw.Draw(img)
    d.ellipse((0, 0, 15, 21), fill=r[2])                       # dome, flat bottom (clipped by the canvas)
    d.rectangle((1, 10, 14, 11), fill=r[1])                    # shadow band at the base
    d.ellipse((3, 2, 7, 5), fill=r[3])                         # highlight top-left
    img.putpixel((4, 3), r[4])
    for x in (6, 10):                                          # eyes: 2 px tall, glint on top
        img.putpixel((x, 6), (255, 255, 255, 255))
        img.putpixel((x, 7), ink)
    return outline(img, ink)


def sample(project: Path) -> Path:
    pal = Palette.load(project / "Design" / "palette.json")
    ui = pal.raw.get("ui", {})
    style = ui.get("style", "pixel")                           # "pixel" | "rounded"
    k = int(ui.get("artScale", 3))                             # screen px per art px
    W, H = 1280, 720
    bg = pal.get("background", pal.get("ink", "#1B1A2E"))
    img = new(W, H, bg)
    d = ImageDraw.Draw(img)
    text_c = pal.get("text", "#FFFFFF")
    muted = pal.get("textMuted", mix(text_c, bg, 0.4))
    f_title, f_body, f_small = _font(project, 40, ui.get("titleFont", "")), _font(project, 22), _font(project, 15)
    d.text((32, 24), f"{pal.name} — style sample", font=f_title, fill=text_c)

    x, y = 32, 92                                              # token swatches
    for name, c in pal.tokens.items():
        d.rectangle((x, y, x + 96, y + 56), fill=c, outline=mix(c, (0, 0, 0, 255), 0.4))
        d.text((x, y + 60), name, font=f_small, fill=text_c)
        d.text((x, y + 78), hexstr(c), font=f_small, fill=muted)
        x += 108
        if x > W - 120:
            x, y = 32, y + 110
    y += 112
    for name, steps in list(pal.ramps.items())[:5]:            # ramps
        d.text((32, y + 6), name, font=f_small, fill=text_c)
        for i, c in enumerate(steps):
            d.rectangle((140 + i * 44, y, 140 + i * 44 + 40, y + 28), fill=c)
        y += 36

    px, py = 32, y + 24                                        # panel + buttons
    if style == "rounded":
        panel = rounded_panel(360, 288, pal.get("surface", "#3D2A45"), 22, pal.get("ink", "#1B1A2E"), 4,
                              6, None, 0.12)
        btn = rounded_panel(220, 64, pal.get("primary", "#E26D5C"), 32, pal.get("ink", "#1B1A2E"), 4, 8, None, 0.2)
        btn_p = rounded_panel(220, 64, pal.get("primary", "#E26D5C"), 32, pal.get("ink", "#1B1A2E"), 4, 0, None, 0.2)
    else:
        panel = scale(pixel_panel(360 // k, 288 // k, pal), k)
        btn = scale(pixel_button(220 // k, 64 // k, pal), k)
        btn_p = scale(pixel_button(220 // k, 64 // k, pal, pressed=True), k)
    img.paste(panel, (px, py), panel)
    d.text((px + 28, py + 22), "Panel title", font=_font(project, 30, ui.get("titleFont", "")), fill=text_c)
    d.text((px + 28, py + 66), "Body text in the chosen font.", font=f_body, fill=muted)
    img.paste(btn, (px + 28, py + 120), btn)
    img.paste(btn_p, (px + 28, py + 120 + 72), btn_p)
    d.text((px + 260, py + 138), "normal", font=f_small, fill=text_c)
    d.text((px + 260, py + 210), "pressed", font=f_small, fill=text_c)

    hero = scale(_blob(pal, "primary" if "primary" in pal.tokens else next(iter(pal.tokens))), k * 2)
    img.paste(hero, (440, py + 20), hero)
    d.text((440, py + 20 + hero.height + 8), f"1 art px = {k * 2} px here", font=f_small, fill=muted)
    for i, name in enumerate(list(pal.ramps)[:3]):             # one bevelled tile per material ramp
        rr = pal.ramps[name] if len(pal.ramps[name]) >= 4 else ramp(pal.ramps[name][0])
        tile = new(16, 16)
        bevel_rect(tile, (0, 0, 15, 15), rr[len(rr) // 2], rr[-2], rr[1])
        for (tx, ty) in ((4, 5), (10, 3), (7, 10), (12, 11)):
            tile.putpixel((tx, ty), rr[1])
        tile = scale(tile, k * 2)
        img.paste(tile, (640 + i * (tile.width + 16), py + 20))
        d.text((640 + i * (tile.width + 16), py + 28 + tile.height), name, font=f_small, fill=muted)
    out = project / "Design" / "style-sample.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    return out


# ----------------------------------------------------------------------------- CLI

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("sample"); s.add_argument("project")
    r = sub.add_parser("ramp"); r.add_argument("base"); r.add_argument("-n", type=int, default=5)
    c = sub.add_parser("check"); c.add_argument("png", nargs="+"); c.add_argument("--project")
    c.add_argument("--scale", type=int)
    sc = sub.add_parser("scale"); sc.add_argument("png"); sc.add_argument("k", type=int); sc.add_argument("-o")
    sh = sub.add_parser("sheet"); sh.add_argument("out"); sh.add_argument("frames", nargs="+")
    sh.add_argument("--cols", type=int); sh.add_argument("--project")
    sh.add_argument("--upscale", type=int, default=4, help="store k x nearest (default 4, sharp bilinear)")
    m = sub.add_parser("meta"); m.add_argument("asset"); m.add_argument("--project")
    m.add_argument("--filter", choices=["Point", "Bilinear", "Trilinear", "Anisotropic"])
    a = ap.parse_args(argv)

    if a.cmd == "sample":
        print(sample(Path(a.project)))
    elif a.cmd == "ramp":
        print(" ".join(hexstr(x) for x in ramp(a.base, a.n)))
    elif a.cmd == "check":
        pal = None
        if a.project and (Path(a.project) / "Design" / "palette.json").exists():
            pal = Palette.load(Path(a.project) / "Design" / "palette.json")
        bad = 0
        for p in a.png:
            issues = check(Path(p), pal, a.scale)
            bad += bool(issues)
            print(f"{p}: " + ("ok" if not issues else "\n  " + "\n  ".join(issues)))
        return 1 if bad else 0
    elif a.cmd == "scale":
        img = scale(Image.open(a.png).convert("RGBA"), a.k)
        out = Path(a.o or a.png)
        img.save(out)
        print(out)
    elif a.cmd == "sheet":
        frames = [(Path(f).stem, Image.open(f).convert("RGBA")) for f in a.frames]
        sheet, sprites = pack_sheet(frames, a.cols)
        g = save_texture(Path(a.out), sheet, Path(a.project) if a.project else None, upscale=a.upscale,
                         sprites=sprites)
        print(f"{a.out} guid {g}, {len(sprites)} slices")
    elif a.cmd == "meta":
        print(write_meta(Path(a.asset), Path(a.project) if a.project else None, filter=a.filter))
    return 0


if __name__ == "__main__":
    sys.exit(main())
