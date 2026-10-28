"""Generate Funke's flat editor icon system from vector-like Pillow paths.

The script deliberately keeps preview and asset writing separate:

    python scripts/assets/generate_editor_icons.py --preview all
    python scripts/assets/generate_editor_icons.py --write

Generated sizes are rendered from the 24 unit geometry independently. Custom
raster icons are preserved and loaded from the shipped PNGs for previews.
The preview command only writes contact sheets under tmp/ and never touches assets.
"""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[2]
GENERAL = ROOT / "Funke-Runtime" / "FunkeAssets" / "Textures" / "Editor" / "General"
FILE_ICONS = ROOT / "Funke-Runtime" / "FunkeAssets" / "Textures" / "Editor" / "FileIcons"
PREVIEW_DIR = ROOT / "tmp"
SIZES = (16, 32, 64, 128)
FILE_SIZES = (16, 32, 64, 128, 256)
CUSTOM_FILE_ICONS = {
    "file_anim", "file_audio", "file_bin", "file_cpp", "file_cs", "file_dataasset",
    "file_fallback", "file_font", "file_header", "file_json", "file_model", "file_prefab", "file_scene",
    "file_shader", "file_txt", "file_xml", "folder_zip",
}
CUSTOM_COMPONENT_ICONS = {
    "animator", "audio_source", "camera", "collider_box_2d", "collider_box_3d",
    "collider_capsule_3d", "collider_circle_2d", "collider_mesh_3d", "collider_polygon_2d",
    "collider_sphere_3d", "light_point", "light_spot", "light_sun", "particles",
    "prefab", "prefab_missing", "rigidbody_2d", "rigidbody_3d", "script",
}
CUSTOM_GLYPHS = {"open_folder"}
FILE_256_NAMES = {
    "file_anim", "file_audio", "file_bin", "file_cpp", "file_cs", "file_dataasset",
    "file_fallback", "file_font", "file_header", "file_json", "file_model", "file_scene", "file_shader",
    "file_txt", "file_xml", "folder_zip",
}
SUPERSAMPLE = 4

GREY = "#BEBEBE"
WHITE = "#F8F8F8"
PHYSICS = "#7CC47F"
CODE = "#7CC47F"
LIGHT = "#F2CF63"
AUDIO = "#E07BB4"
EFFECT = "#F0A44B"
ANIMATION = "#A87CF0"
CAMERA = "#5DA9E9"
PREFAB = "#5DA9E9"
PREFAB_MISSING = "#F0B000"
STATUS = {"info": "#3890F0", "success": "#58C080", "warning": "#F0B000", "error": "#E03830"}
FILE_COLORS = {
    "file_cs": PHYSICS, "file_cpp": "#3E80BA", "file_header": "#3E80BA",
    "file_shader": "#8A86E8", "file_anim": ANIMATION,
    "file_audio": AUDIO, "file_prefab": PREFAB, "file_scene": "#3E80BA",
    "file_dataasset": "#4CB8A8", "file_json": "#F2B33D", "file_xml": "#8A86E8",
    "file_font": "#F26B66", "file_txt": "#A0A4AD", "file_bin": "#A0A4AD",
    "file_fallback": "#A0A4AD", "file_model": "#F0A44B",
}

GLYPHS = [
    "arrow_down", "arrow_left", "arrow_right", "arrow_up", "aspect_ratio", "bounding_box",
    "caret", "circle_dot", "clear", "copy", "delete", "download", "entity", "eye_closed",
    "eye_open", "file_manager", "grid", "launch_standalone", "link", "lock_closed", "lock_open",
    "minus", "move", "pause", "pen", "pin", "play", "plus", "post_processing",
    "redo", "reset", "rotation", "save", "scale", "search", "settings", "settings_gear",
    "step_forward", "stop", "undo", "zoom_in", "zoom_out", "open_folder",
    "monitor", "image", "flag", "hammer", "systems", "physics",
]
COMPONENTS = [
    "animator", "audio_source", "camera", "collider_box_2d", "collider_box_3d", "collider_capsule_3d",
    "collider_circle_2d", "collider_mesh_3d", "collider_polygon_2d", "collider_sphere_3d", "light_point",
    "light_spot", "light_sun", "particles", "prefab", "prefab_missing", "rigidbody_2d", "rigidbody_3d", "script",
]
STATUSES = ["info", "success", "warning", "error"]
FILES = list(FILE_COLORS) + ["folder_zip"]


class Canvas:
    """24 unit drawing surface with round-capped, round-joined strokes."""

    def __init__(self, size: int):
        self.size = size
        self.scale = size * SUPERSAMPLE / 24.0
        self.image = Image.new("RGBA", (size * SUPERSAMPLE, size * SUPERSAMPLE), (0, 0, 0, 0))
        self.draw = ImageDraw.Draw(self.image)

    def p(self, point):
        return tuple(round(v * self.scale) for v in point)

    def width(self, units=2):
        return max(1, round(units * self.scale))

    def line(self, points, color, width=2):
        pts = [self.p(p) for p in points]
        w = self.width(width)
        self.draw.line(pts, fill=color, width=w, joint="curve")
        radius = w / 2
        for x, y in (pts[0], pts[-1]):
            self.draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=color)

    def poly(self, points, color, width=2, closed=True):
        pts = list(points)
        if closed:
            pts.append(pts[0])
        self.line(pts, color, width)

    def rect(self, box, color, width=2, radius=2, fill=None):
        b = self.p((box[0], box[1])) + self.p((box[2], box[3]))
        self.draw.rounded_rectangle(b, radius=round(radius * self.scale), fill=fill, outline=color, width=self.width(width))

    def circle(self, center, radius, color, width=2, fill=None):
        x, y = center
        b = self.p((x - radius, y - radius)) + self.p((x + radius, y + radius))
        self.draw.ellipse(b, fill=fill, outline=color, width=self.width(width))

    def arc(self, box, start, end, color, width=2):
        b = self.p((box[0], box[1])) + self.p((box[2], box[3]))
        self.draw.arc(b, start, end, fill=color, width=self.width(width))

    def fill_poly(self, points, color):
        self.draw.polygon([self.p(p) for p in points], fill=color)

    def finish(self):
        return self.image.resize((self.size, self.size), Image.Resampling.LANCZOS)


def cube(c: Canvas, color: str, x=12, y=12, r=7):
    top, upper_right, lower_right, bottom, lower_left, upper_left, center = (
        (x, y-r), (x+r*.86, y-r*.45), (x+r*.86, y+r*.45), (x, y+r),
        (x-r*.86, y+r*.45), (x-r*.86, y-r*.45), (x, y),
    )
    c.poly([top, upper_right, lower_right, bottom, lower_left, upper_left], color)
    c.line([upper_left, center, upper_right], color)
    c.line([center, bottom], color)


def chevron(c: Canvas, direction: str, color: str):
    points = {"left": [(14, 5), (7, 12), (14, 19)], "right": [(10, 5), (17, 12), (10, 19)],
              "up": [(5, 14), (12, 7), (19, 14)], "down": [(5, 10), (12, 17), (19, 10)]}[direction]
    c.line(points, color)


def glyph(c: Canvas, name: str):
    q = GREY
    if name == "monitor":
        c.rect((3, 4, 21, 16), q, radius=2)
        c.line([(12, 16), (12, 20)], q)
        c.line([(8, 20), (16, 20)], q)
    elif name == "image":
        c.rect((3, 4, 21, 20), q, radius=2)
        c.circle((8, 9), 1.3, q, fill=q)
        c.line([(4, 17), (10, 12), (14, 16), (17, 13), (20, 16)], q)
    elif name == "flag":
        c.line([(5, 20), (5, 4)], q)
        c.poly([(5, 4), (12, 4), (14, 6), (20, 6), (20, 15), (13, 15), (11, 13), (5, 13)], q)
    elif name == "hammer":
        c.poly([(4, 17), (12, 9), (15, 12), (7, 20)], q)
        c.poly([(9, 6), (12, 3), (21, 12), (18, 15)], q)
    elif name == "systems":
        c.line([(12, 9), (12, 12)], q)
        c.line([(6, 15), (6, 12), (18, 12), (18, 15)], q)
        c.rect((9, 3, 15, 9), q, radius=1)
        c.rect((3, 15, 9, 21), q, radius=1)
        c.rect((15, 15, 21, 21), q, radius=1)
    elif name == "physics":
        c.circle((8, 7), 4, q)
        c.line([(17, 4), (17, 16)], q)
        c.line([(14, 13), (17, 16), (20, 13)], q)
        c.line([(4, 20), (20, 20)], q)
    elif name.startswith("arrow_"):
        chevron(c, name.split("_")[1], q)
    elif name in {"plus", "minus"}:
        c.line([(6, 12), (18, 12)], q)
        if name == "plus": c.line([(12, 6), (12, 18)], q)
    elif name == "clear":
        c.line([(7, 7), (17, 17)], q); c.line([(17, 7), (7, 17)], q)
    elif name == "caret":
        c.line([(12, 4), (12, 20)], q); c.line([(8, 4), (16, 4)], q); c.line([(8, 20), (16, 20)], q)
    elif name == "search":
        c.circle((10.5, 10.5), 5.5, q); c.line([(14.5, 14.5), (19, 19)], q)
    elif name in {"zoom_in", "zoom_out"}:
        c.circle((10, 10), 5.5, q); c.line([(14.3, 14.3), (19, 19)], q); c.line([(7.5, 10), (12.5, 10)], q)
        if name == "zoom_in": c.line([(10, 7.5), (10, 12.5)], q)
    elif name == "circle_dot":
        c.circle((12, 12), 8, q); c.circle((12, 12), 1.3, q, fill=q)
    elif name == "grid":
        c.rect((4, 4, 20, 20), q, radius=1)
        for a in (9.33, 14.66): c.line([(a, 4), (a, 20)], q); c.line([(4, a), (20, a)], q)
    elif name == "bounding_box":
        c.rect((5, 5, 19, 19), q, radius=1)
        for x, y in ((5,5),(19,5),(19,19),(5,19)): c.circle((x,y), .8, q, fill=q)
    elif name == "aspect_ratio":
        c.line([(5, 9), (5, 5), (9, 5)], q); c.line([(15, 5), (19, 5), (19, 9)], q)
        c.line([(19, 15), (19, 19), (15, 19)], q); c.line([(9, 19), (5, 19), (5, 15)], q)
    elif name in {"play", "step_forward"}:
        c.fill_poly([(7, 5), (7, 19), (18, 12)], q)
        if name == "step_forward": c.line([(19, 6), (19, 18)], q)
    elif name == "pause":
        c.rect((6, 5, 10, 19), q, width=0, radius=1, fill=q); c.rect((14, 5, 18, 19), q, width=0, radius=1, fill=q)
    elif name == "stop":
        c.rect((5, 5, 19, 19), q, width=0, radius=2, fill=q)
    elif name in {"undo", "redo", "reset", "rotation"}:
        if name == "undo": c.arc((5, 5, 19, 19), 35, 285, q); c.line([(6, 9), (6, 5), (10, 5)], q)
        elif name == "redo": c.arc((5, 5, 19, 19), 215, 465, q); c.line([(18, 15), (18, 19), (14, 19)], q)
        elif name == "reset": c.arc((4, 4, 20, 20), -55, 250, q); c.line([(17, 5), (20, 5), (20, 8)], q)
        else: c.arc((5, 5, 19, 19), 0, 300, q); c.line([(16, 5), (19, 5), (19, 8)], q)
    elif name == "settings":
        # A compact cog, deliberately heavier than a sunburst at 16 px.
        c.circle((12, 12), 5.9, q, fill=q)
        for a in range(0, 360, 45):
            rad = math.radians(a)
            c.line(
                [(12 + math.cos(rad) * 5.1, 12 + math.sin(rad) * 5.1),
                 (12 + math.cos(rad) * 8.2, 12 + math.sin(rad) * 8.2)],
                q,
                3.2,
            )
        c.circle((12, 12), 2.5, (0, 0, 0, 0), fill=(0, 0, 0, 0))
    elif name == "settings_gear":
        c.circle((12, 12), 7, q, fill=q); c.circle((12, 12), 2.5, (0,0,0,0), fill=(0,0,0,0))
        for a in range(0, 360, 45):
            rad=math.radians(a); c.line([(12+math.cos(rad)*6,12+math.sin(rad)*6),(12+math.cos(rad)*9,12+math.sin(rad)*9)], q, 3)
    elif name == "pen":
        c.poly([(6, 18), (8, 18), (18, 8), (16, 6)], q); c.line([(6,18),(5,20)],q)
    elif name == "pin":
        c.line([(12, 4), (12, 20)], q); c.poly([(8, 5), (16, 5), (15, 12), (9, 12)], q)
    elif name == "copy":
        c.rect((8, 4, 19, 16), q, radius=2); c.rect((5, 8, 16, 20), q, radius=2)
    elif name == "delete":
        c.rect((7, 8, 17, 20), q, radius=1); c.line([(5, 6), (19, 6)], q); c.line([(10, 4), (14, 4)], q)
    elif name == "download":
        c.line([(12, 4), (12, 15)], q); c.line([(8, 11), (12, 15), (16, 11)], q); c.line([(5, 19), (19, 19)], q)
    elif name == "save":
        c.rect((5, 4, 19, 20), q, radius=2); c.rect((8, 4, 15, 10), q, radius=0); c.rect((8, 14, 16, 20), q, radius=1)
    elif name == "file_manager":
        c.line([(4, 9), (10, 9), (12, 6), (20, 6), (20, 19), (4, 19), (4, 9)], q)
    elif name == "open_folder":
        c.line([(4, 19), (6, 8), (11, 8), (13, 6), (20, 6), (18, 19), (4, 19)], q)
    elif name == "launch_standalone":
        c.rect((4, 5, 17, 18), q, radius=1); c.line([(12, 12), (20, 4)], q); c.line([(15, 4), (20, 4), (20, 9)], q)
    elif name == "link":
        c.arc((3, 8, 14, 17), 300, 120, q); c.arc((10, 7, 21, 16), 120, 300, q); c.line([(9, 14), (15, 10)], q)
    elif name in {"lock_closed", "lock_open"}:
        c.rect((5, 10, 19, 20), q, radius=2)
        if name == "lock_closed": c.arc((8, 4, 16, 14), 180, 360, q)
        else: c.arc((8, 4, 17, 14), 185, 335, q)
    elif name.startswith("eye_"):
        c.fill_poly([(3,12),(8,7),(16,7),(21,12),(16,17),(8,17)], q)
        c.circle((12,12),3,(0,0,0,0),fill=(0,0,0,0))
        if name == "eye_closed": c.line([(4,5),(20,19)],q)
    elif name == "entity": cube(c, q)
    elif name == "move":
        c.line([(4,12),(20,12)],q); c.line([(12,4),(12,20)],q)
        for pts in ([(4,12),(7,10),(7,14)],[(20,12),(17,10),(17,14)],[(12,4),(10,7),(14,7)],[(12,20),(10,17),(14,17)]): c.line(pts,q)
    elif name == "scale":
        c.line([(5,9),(5,5),(9,5)],q); c.line([(15,5),(19,5),(19,9)],q); c.line([(5,15),(5,19),(9,19)],q); c.line([(19,15),(19,19),(15,19)],q)
    elif name == "post_processing":
        c.circle((12,12),7,q); c.line([(12,5),(12,19)],q); c.line([(5,12),(19,12)],q)
    else:
        c.circle((12,12),7,q)


def component(c: Canvas, name: str):
    color = {"animator": ANIMATION, "audio_source": AUDIO, "camera": CAMERA, "particles": EFFECT,
             "prefab": PREFAB, "prefab_missing": PREFAB_MISSING, "script": CODE,
             "light_point": LIGHT, "light_spot": LIGHT, "light_sun": LIGHT}.get(name, PHYSICS)
    if name == "collider_box_2d":
        c.rect((5,5,19,19),color,radius=1)
        for p in ((5,5),(19,5),(19,19),(5,19)): c.circle(p,.75,color,fill=color)
    elif name == "collider_circle_2d": c.circle((12,12),7,color); c.circle((12,12),.8,color,fill=color)
    elif name == "collider_polygon_2d":
        pts=[(12+math.cos(math.radians(-90+i*72))*7,12+math.sin(math.radians(-90+i*72))*7) for i in range(5)]; c.poly(pts,color)
        for p in pts: c.circle(p,.7,color,fill=color)
    elif name == "rigidbody_2d": c.rect((8,8,16,16),color,radius=1); c.arc((3,5,18,20),120,260,color); c.line([(4,9),(6,6),(9,6)],color)
    elif name == "collider_box_3d": cube(c,color)
    elif name == "collider_sphere_3d": c.circle((12,12),8,color); c.arc((4,8,20,16),0,180,color); c.arc((4,8,20,16),180,360,color)
    elif name == "collider_capsule_3d": c.rect((8,4,16,20),color,radius=4); c.line([(8,8),(16,8)],color); c.line([(8,16),(16,16)],color)
    elif name == "collider_mesh_3d":
        pts=[(12,4),(20,9),(17,19),(7,19),(4,9)]; c.poly(pts,color)
        for p in pts: c.line([(12,12),p],color,1.5); c.circle(p,.7,color,fill=color)
        c.circle((12,12),.7,color,fill=color)
    elif name == "rigidbody_3d": cube(c,color,13,12,6); c.arc((2,5,17,20),120,260,color); c.line([(3,9),(5,6),(8,6)],color)
    elif name == "animator":
        c.line([(4,17),(9,10),(14,14),(20,6)],color)
        for p in ((4,17),(9,10),(14,14),(20,6)): c.fill_poly([(p[0],p[1]-1),(p[0]+1,p[1]),(p[0],p[1]+1),(p[0]-1,p[1])],color)
    elif name == "audio_source":
        c.line([(8,6),(8,17)],color); c.line([(15,4),(15,15)],color); c.line([(8,6),(15,4)],color); c.circle((6,18),2,color); c.circle((13,16),2,color)
    elif name == "script":
        c.rect((6,4,18,20),color,radius=1); c.line([(10,10),(8,12),(10,14)],color,1.5); c.line([(14,10),(16,12),(14,14)],color,1.5)
    elif name == "camera":
        c.rect((4,8,17,17),color,radius=2); c.circle((10.5,12.5),3,color); c.line([(17,10),(21,7),(21,18),(17,15)],color)
    elif name == "particles":
        for x,y,r in ((12,12,2),(6,7,1),(18,6,1),(19,17,1),(7,18,1)): c.circle((x,y),r,color,fill=color)
    elif name == "light_sun":
        c.circle((12,12),4,color)
        for a in range(0,360,45):
            r=math.radians(a); c.line([(12+math.cos(r)*6,12+math.sin(r)*6),(12+math.cos(r)*9,12+math.sin(r)*9)],color)
    elif name == "light_point":
        c.circle((12,10),4,color); c.line([(9,16),(15,16)],color); c.line([(10,19),(14,19)],color)
        for a in (210,270,330):
            r=math.radians(a); c.line([(12+math.cos(r)*6,10+math.sin(r)*6),(12+math.cos(r)*8,10+math.sin(r)*8)],color)
    elif name == "light_spot":
        c.rect((8,4,16,8),color,radius=1); c.line([(9,8),(4,20)],color); c.line([(15,8),(20,20)],color); c.line([(4,20),(20,20)],color,1)
    elif name == "prefab": cube(c,color)
    elif name == "prefab_missing": cube(c,color); c.line([(16,5),(20,9)],PREFAB_MISSING); c.line([(20,5),(16,9)],PREFAB_MISSING)


def status(c: Canvas, name: str):
    color = STATUS[name]
    if name == "warning":
        c.fill_poly([(12,3),(21,20),(3,20)],color); c.line([(12,9),(12,15)],WHITE,1.6); c.circle((12,17.5),.9,WHITE,fill=WHITE)
    else:
        c.circle((12,12),9,color,fill=color)
        if name == "info": c.line([(12,10),(12,17)],WHITE,1.8); c.circle((12,6.5),1,WHITE,fill=WHITE)
        elif name == "success": c.line([(7,12),(10.5,15.5),(17,8.5)],WHITE,2)
        else: c.line([(8,8),(16,16)],WHITE,2); c.line([(16,8),(8,16)],WHITE,2)


def file_icon(c: Canvas, name: str):
    if name in {"open_folder", "folder_zip"}:
        amber="#F2B33D"; c.fill_poly([(3,8),(9,8),(11,6),(20,6),(21,19),(3,19)],amber); c.line([(12,9),(12,17)],WHITE,1.4)
        if name == "folder_zip":
            for y in (10,13,16): c.circle((12,y),.55,WHITE,fill=WHITE)
        return
    color=FILE_COLORS[name]
    c.fill_poly([(5,3),(15,3),(20,8),(20,21),(5,21)],color)
    c.fill_poly([(15,3),(20,8),(15,8)],(255,255,255,64))
    if name == "file_audio":
        c.line([(9,10),(9,16)],WHITE,1.2); c.line([(14,8),(14,14)],WHITE,1.2); c.circle((8,17),1.3,WHITE,fill=WHITE); c.circle((13,15),1.3,WHITE,fill=WHITE)
    elif name == "file_anim": c.line([(8,16),(11,11),(15,14),(17,9)],WHITE,1.1)
    elif name == "file_prefab": c.rect((9,9,16,16),WHITE,width=1,radius=.5)
    elif name == "file_scene": c.rect((8,10,17,16),WHITE,width=1,radius=.5); c.line([(8,10),(12.5,7),(17,10)],WHITE,1)
    elif name == "file_dataasset": c.arc((9,9,16,13),0,360,WHITE,1); c.line([(9,11),(9,16),(16,16),(16,11)],WHITE,1); c.arc((9,14,16,18),0,180,WHITE,1)
    elif name == "file_shader": c.fill_poly([(12,8),(16,12),(12,16),(8,12)],WHITE)
    elif name in {"file_cs","file_xml"}: c.line([(10,9),(7,12),(10,15)],WHITE,1); c.line([(14,9),(17,12),(14,15)],WHITE,1)
    elif name == "file_json": c.line([(10,8),(8,10),(8,14),(10,16)],WHITE,1); c.line([(14,8),(16,10),(16,14),(14,16)],WHITE,1)
    elif name == "file_font": c.line([(9,16),(12,8),(15,16)],WHITE,1); c.line([(10.5,13),(13.5,13)],WHITE,1)
    elif name == "file_bin": c.line([(8,10),(8,15)],WHITE,1); c.line([(11,10),(13,10),(13,15),(11,15)],WHITE,1); c.line([(16,10),(16,15)],WHITE,1)
    elif name == "file_fallback": c.line([(11,10),(13,10),(14,12),(12,13),(12,15)],WHITE,1); c.circle((12,17),.7,WHITE,fill=WHITE)
    else:
        for y in (10,13,16): c.line([(8, y),(17,y)],WHITE,1)


def is_custom(name: str, family: str):
    return (family == "file" and name in CUSTOM_FILE_ICONS
            or family == "component" and name in CUSTOM_COMPONENT_ICONS
            or family == "glyph" and name in CUSTOM_GLYPHS)


def render(name: str, family: str, size: int):
    if is_custom(name, family):
        root = FILE_ICONS if family == "file" else GENERAL
        path = root / name / f"{name}_{size}.png"
        with Image.open(path) as image:
            if image.size != (size, size):
                raise ValueError(f"Unexpected icon size: {path}")
            return image.convert("RGBA")
    c = Canvas(size)
    if family == "glyph": glyph(c, name)
    elif family == "component": component(c, name)
    elif family == "status": status(c, name)
    else: file_icon(c, name)
    return c.finish()


def tint(image: Image.Image, color: str) -> Image.Image:
    alpha=image.getchannel("A")
    result=Image.new("RGBA", image.size, color)
    result.putalpha(alpha)
    return result


def preview(names, family: str, title: str, destination: Path):
    columns=6; cell_w=205; cell_h=132; header=44; rows=math.ceil(len(names)/columns)
    sheet=Image.new("RGBA", (columns*cell_w+24, header+rows*cell_h+16), "#1D1F23")
    d=ImageDraw.Draw(sheet); font=ImageFont.load_default()
    d.text((12,12), title, fill="#E6E8EC", font=font)
    for i,name in enumerate(names):
        x=12+(i%columns)*cell_w; y=header+(i//columns)*cell_h
        d.rounded_rectangle((x,y,x+186,y+116),radius=6,fill="#2A2C30")
        icon_family = ("status" if name in STATUSES else "file") if family == "status-files" else family
        main=render(name,icon_family,64)
        if icon_family == "glyph" and name not in CUSTOM_GLYPHS: main=tint(main,"#EBECEF")
        sheet.alpha_composite(main,(x+14,y+10))
        small=render(name,icon_family,16)
        dark=tint(small,"#EBECEF") if icon_family=="glyph" and name not in CUSTOM_GLYPHS else small
        light=tint(small,"#1E2025") if icon_family=="glyph" and name not in CUSTOM_GLYPHS else small
        sheet.alpha_composite(dark,(x+92,y+22));
        light_panel=Image.new("RGBA",(32,32),"#F2F3F5"); light_panel.alpha_composite(light,(8,8)); sheet.alpha_composite(light_panel,(x+124,y+14))
        d.text((x+8,y+88),name,fill="#BFC3CA",font=font)
    destination.parent.mkdir(parents=True,exist_ok=True); sheet.save(destination)


def write_family(family: str, names: list[str], root: Path, sizes: tuple[int, ...]):
    for name in names:
        if is_custom(name, family):
            print(f"preserved custom {family} icon: {name}")
            continue
        folder=root/name; folder.mkdir(parents=True,exist_ok=True)
        for size in sizes: render(name,family,size).save(folder/f"{name}_{size}.png",optimize=True)
        print(f"wrote {family}: {name}")


def write_assets():
    write_family("glyph", GLYPHS, GENERAL, SIZES)
    write_family("component", COMPONENTS, GENERAL, SIZES)
    write_family("status", STATUSES, GENERAL, SIZES)
    for name in FILES:
        if name in CUSTOM_FILE_ICONS:
            print(f"preserved custom file icon: {name}")
            continue
        file_sizes = FILE_SIZES if name in FILE_256_NAMES else SIZES
        write_family("file", [name], FILE_ICONS, file_sizes)

    # This icon historically also has a 256 px preview variant in General.
    write_family("glyph", ["open_folder"], GENERAL, (256,))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--preview",choices=("all","glyphs","components","status-files"))
    parser.add_argument("--write",action="store_true",help="Write shipped PNGs after preview approval.")
    args=parser.parse_args()
    if args.preview:
        if args.preview in ("all","glyphs"): preview(GLYPHS,"glyph","Glyphs — dark 64px | dark/light 16px",PREVIEW_DIR/"editor-icons-preview-glyphs.png")
        if args.preview in ("all","components"): preview(COMPONENTS,"component","Components — dark 64px | dark/light 16px",PREVIEW_DIR/"editor-icons-preview-components.png")
        if args.preview in ("all","status-files"): preview(STATUSES+FILES,"status-files","Status & files — dark 64px | dark/light 16px",PREVIEW_DIR/"editor-icons-preview-status-files.png")
    if args.write: write_assets()
    if not args.preview and not args.write: parser.error("use --preview or --write")


if __name__ == "__main__": main()
