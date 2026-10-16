"""Draws the editor's component icons (audio, particles, scripts, lights, 2D/3D physics, animator).

Flat, colored, round-cap strokes like the existing camera/light_sun icons. Each icon is drawn at
512 px and downsampled to the shipped sizes into
Funke-Runtime/FunkeAssets/Textures/Editor/General/<name>/<name>_<size>.png.

Usage: python scripts/assets/generate_component_icons.py [--only name,name]
"""
import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Funke-Runtime" / "FunkeAssets" / "Textures" / "Editor" / "General"
SIZES = (16, 32, 64, 128)
S = 512  # supersampled canvas
W = 40   # main stroke width at 512 px

PINK = (224, 123, 180, 255)
ORANGE = (240, 164, 75, 255)
GREEN = (124, 196, 127, 255)
YELLOW = (242, 207, 99, 255)
YELLOW_SOFT = (242, 207, 99, 110)
BLUE = (93, 169, 233, 255)
BLUE_DARK = (62, 128, 186, 255)
BLUE_LIGHT = (140, 196, 242, 255)
GREY = (190, 190, 190, 255)


def line(d, pts, color, width=W):
    d.line(pts, fill=color, width=width, joint="curve")
    r = width / 2
    for x, y in (pts[0], pts[-1]):
        d.ellipse((x - r, y - r, x + r, y + r), fill=color)


def polyline_closed(d, pts, color, width=W):
    d.line(pts + [pts[0]], fill=color, width=width, joint="curve")
    r = width / 2
    for x, y in pts:
        d.ellipse((x - r, y - r, x + r, y + r), fill=color)


def circle(d, cx, cy, r, color, width=None):
    box = (cx - r, cy - r, cx + r, cy + r)
    if width is None:
        d.ellipse(box, fill=color)
    else:
        d.ellipse(box, outline=color, width=width)


def star4(d, cx, cy, r, color):
    k = r * 0.28
    d.polygon([(cx, cy - r), (cx + k, cy - k), (cx + r, cy), (cx + k, cy + k),
               (cx, cy + r), (cx - k, cy + k), (cx - r, cy), (cx - k, cy - k)], fill=color)


def iso_cube_points(cx, cy, s):
    """Corners of an isometric cube centred at (cx, cy): top, upper-right, lower-right, bottom, lower-left, upper-left, centre."""
    h = s * 0.5
    q = s * 0.29
    return {
        "top": (cx, cy - h), "ur": (cx + h * 0.87, cy - q), "lr": (cx + h * 0.87, cy + q),
        "bottom": (cx, cy + h), "ll": (cx - h * 0.87, cy + q), "ul": (cx - h * 0.87, cy - q), "c": (cx, cy),
    }


# ── icons ───────────────────────────────────────────────────────────────────

def audio_source(d):
    # Two beamed eighth notes.
    line(d, [(196, 120), (196, 360)], PINK)
    line(d, [(364, 88), (364, 328)], PINK)
    d.polygon([(176, 100), (384, 50), (384, 130), (176, 180)], fill=PINK)
    circle(d, 150, 372, 62, PINK)
    circle(d, 318, 340, 62, PINK)


def particles(d):
    star4(d, 214, 282, 150, ORANGE)
    star4(d, 382, 136, 78, ORANGE)
    circle(d, 392, 362, 34, ORANGE)
    circle(d, 110, 112, 26, ORANGE)
    circle(d, 300, 444, 22, ORANGE)


def script(d):
    # Page with a folded corner and </> on it.
    page = [(120, 56), (318, 56), (412, 150), (412, 456), (120, 456)]
    polyline_closed(d, page, GREEN)
    line(d, [(318, 56), (318, 150), (412, 150)], GREEN)
    line(d, [(232, 242), (178, 300), (232, 358)], GREEN, 34)
    line(d, [(300, 242), (354, 300), (300, 358)], GREEN, 34)


def light_point(d):
    # Light bulb with rays.
    circle(d, 256, 216, 120, YELLOW)
    d.rectangle((206, 300, 306, 372), fill=YELLOW)
    line(d, [(212, 410), (300, 410)], YELLOW, 34)
    line(d, [(226, 456), (286, 456)], YELLOW, 34)
    for a in (-150, -110, -70, -30):
        rad = math.radians(a)
        line(d, [(256 + math.cos(rad) * 162, 216 + math.sin(rad) * 162),
                 (256 + math.cos(rad) * 214, 216 + math.sin(rad) * 214)], YELLOW, 28)


def light_spot(d):
    # Lamp housing with a cone of light below it.
    d.polygon([(206, 150), (306, 150), (474, 470), (38, 470)], fill=YELLOW_SOFT)
    d.rounded_rectangle((176, 52, 336, 168), radius=30, fill=YELLOW)
    line(d, [(256, 52), (256, 20)], YELLOW, 30)
    line(d, [(206, 150), (38, 470)], YELLOW, 26)
    line(d, [(306, 150), (474, 470)], YELLOW, 26)


def collider_box_2d(d):
    polyline_closed(d, [(96, 96), (416, 96), (416, 416), (96, 416)], GREEN)
    for x, y in ((96, 96), (416, 96), (416, 416), (96, 416)):
        d.rounded_rectangle((x - 40, y - 40, x + 40, y + 40), radius=12, fill=GREEN)


def collider_circle_2d(d):
    circle(d, 256, 256, 180, GREEN, W)
    circle(d, 256, 256, 30, GREEN)


def collider_polygon_2d(d):
    pts = [(256 + math.cos(math.radians(-90 + i * 72)) * 196, 270 + math.sin(math.radians(-90 + i * 72)) * 196) for i in range(5)]
    polyline_closed(d, pts, GREEN)
    for x, y in pts:
        circle(d, x, y, 40, GREEN)


def rigidbody_2d(d):
    circle(d, 300, 256, 150, BLUE)
    circle(d, 260, 214, 44, BLUE_LIGHT)
    for y, x0 in ((176, 60), (256, 40), (336, 60)):
        line(d, [(x0, y), (x0 + 72, y)], BLUE, 32)


def collider_box_3d(d):
    p = iso_cube_points(256, 256, 400)
    polyline_closed(d, [p["top"], p["ur"], p["lr"], p["bottom"], p["ll"], p["ul"]], GREEN)
    line(d, [p["ul"], p["c"], p["ur"]], GREEN)
    line(d, [p["c"], p["bottom"]], GREEN)


def collider_sphere_3d(d):
    circle(d, 256, 256, 190, GREEN, W)
    d.arc((66, 196, 446, 316), 0, 180, fill=GREEN, width=30)
    d.arc((66, 196, 446, 316), 180, 360, fill=(124, 196, 127, 120), width=22)


def collider_capsule_3d(d):
    d.rounded_rectangle((146, 52, 366, 460), radius=110, outline=GREEN, width=W)
    d.arc((146, 106, 366, 176), 0, 180, fill=GREEN, width=26)
    d.arc((146, 336, 366, 406), 0, 180, fill=GREEN, width=26)


def collider_mesh_3d(d):
    a, b, c, e, f = (256, 52), (452, 196), (380, 444), (132, 444), (60, 196)
    m = (256, 286)
    polyline_closed(d, [a, b, c, e, f], GREEN)
    for v in (a, b, c, e, f):
        line(d, [m, v], GREEN, 26)
    for v in (a, b, c, e, f, m):
        circle(d, v[0], v[1], 30, GREEN)


def rigidbody_3d(d):
    p = iso_cube_points(296, 256, 340)
    d.polygon([p["top"], p["ur"], p["c"], p["ul"]], fill=BLUE_LIGHT)
    d.polygon([p["ul"], p["c"], p["bottom"], p["ll"]], fill=BLUE)
    d.polygon([p["c"], p["ur"], p["lr"], p["bottom"]], fill=BLUE_DARK)
    for y, x0 in ((196, 34), (276, 14), (356, 34)):
        line(d, [(x0, y), (x0 + 64, y)], BLUE, 30)


def diamond(d, cx, cy, r, color):
    d.polygon([(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)], fill=color)


def animator(d):
    # Keyframe diamonds on a timeline with the curve they drive.
    curve = [(96 + i * 3.2, 320 - math.sin(i / 100 * math.pi) * 230) for i in range(101)]
    line(d, curve, PINK, 34)
    line(d, [(56, 420), (456, 420)], PINK, 30)
    for x in (96, 256, 416):
        diamond(d, x, 420, 52, PINK)


ICONS = {
    "audio_source": audio_source,
    "particles": particles,
    "script": script,
    "light_point": light_point,
    "light_spot": light_spot,
    "collider_box_2d": collider_box_2d,
    "collider_circle_2d": collider_circle_2d,
    "collider_polygon_2d": collider_polygon_2d,
    "rigidbody_2d": rigidbody_2d,
    "collider_box_3d": collider_box_3d,
    "collider_sphere_3d": collider_sphere_3d,
    "collider_capsule_3d": collider_capsule_3d,
    "collider_mesh_3d": collider_mesh_3d,
    "rigidbody_3d": rigidbody_3d,
    "animator": animator,
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--only", default="")
    args = parser.parse_args()
    only = {n for n in args.only.split(",") if n}
    for name, draw_fn in ICONS.items():
        if only and name not in only:
            continue
        canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        draw_fn(ImageDraw.Draw(canvas))
        folder = OUT / name
        folder.mkdir(parents=True, exist_ok=True)
        for size in SIZES:
            canvas.resize((size, size), Image.LANCZOS).save(folder / f"{name}_{size}.png", optimize=True)
        print(f"wrote {name}")


if __name__ == "__main__":
    main()
