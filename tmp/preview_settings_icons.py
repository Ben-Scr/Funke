from pathlib import Path
import hashlib
import importlib.util
import re

from PIL import Image, ImageChops, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("icons", ROOT / "scripts/assets/generate_editor_icons.py")
icons = importlib.util.module_from_spec(spec)
spec.loader.exec_module(icons)
new_names = ["monitor", "image", "flag", "hammer", "systems", "physics"]
rows = [
    ("Display", "aspect_ratio", "monitor"),
    ("Graphics", "post_processing", "image"),
    ("Branding", "pen", "flag"),
    ("Build", "launch_standalone", "hammer"),
    ("Editor", "settings", "settings"),
    ("Systems", "grid", "systems"),
    ("Physics", "rotation", "physics"),
]
before = {p: hashlib.sha256(p.read_bytes()).digest()
          for root in (icons.GENERAL, icons.FILE_ICONS) for p in root.rglob("*.png")
          if p.parent.name not in new_names}
icons.write_family("glyph", new_names, icons.GENERAL, icons.SIZES)
assert all(hashlib.sha256(p.read_bytes()).digest() == digest for p, digest in before.items())
for name in new_names:
    for size in icons.SIZES:
        with Image.open(icons.GENERAL / name / f"{name}_{size}.png") as img:
            assert img.mode == "RGBA" and img.size == (size, size)
            alpha = img.getchannel("A")
            low, high = alpha.getextrema()
            assert low == 0 and high >= 200, (name, size, (low, high))
            left, top, right, bottom = alpha.point(lambda a: 255 if a > 16 else 0).getbbox()
            assert 0 < left < right < size and 0 < top < bottom < size, (name, size, (left, top, right, bottom))
panel = (ROOT / "Funke-Editor/src/Systems/ImGuiEditorLayerPanels.cpp").read_text()
for label, old, new in rows:
    assert re.search(r'\{ "' + label + r'", "[^"]+", "' + new + r'" \}', panel)

out = ROOT / "output/settings-icons-2026-10-28"
out.mkdir(parents=True, exist_ok=True)
font_path = str(ROOT / "Funke-Runtime/FunkeAssets/Fonts/IBMPlexSansCondensed-Regular.ttf")

def rgb(values):
    return tuple(round(v * 255) for v in values)

def native_icon(name, px, tint):
    source_size = next(size for size in icons.SIZES if size >= px)
    with Image.open(icons.GENERAL / name / f"{name}_{source_size}.png") as source:
        icon = source.convert("RGBA").resize((px, px), Image.Resampling.BILINEAR)
    return ImageChops.multiply(icon, Image.new("RGBA", icon.size, tint + (255,)))

def sidebar(px, dark, old=False):
    row_h = round(px * 2.4)
    width = round(px * 11.8)
    bg = rgb((.110, .114, .125) if dark else (.949, .953, .961))
    text = rgb((.922, .925, .937) if dark else (.118, .125, .145))
    muted = rgb((.545, .557, .592) if dark else (.447, .463, .502))
    accent = rgb((.357, .553, .937) if dark else (.184, .420, .859))
    active = rgb((.616, .737, .969) if dark else (.184, .420, .859))
    sheet = Image.new("RGBA", (width, 7 * row_h), bg + (255,))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype(font_path, px)
    for i, (label, previous, replacement) in enumerate(rows):
        y = i * row_h
        selected = label == "Physics"
        if selected:
            strength = .14 if dark else .10
            fill = tuple(round(a * strength + b * (1 - strength)) for a, b in zip(accent, bg))
            draw.rounded_rectangle((0, y, width - 1, y + row_h - 1), radius=px / 3, fill=fill)
            inset = round(px * .6)
            draw.rounded_rectangle((0, y + inset, max(1, round(px * 2 / 15)) - 1, y + row_h - inset), radius=1, fill=accent)
        x = round(px * .8)
        sheet.alpha_composite(native_icon(previous if old else replacement, px, active if selected else muted), (x, y + round((row_h - px) / 2)))
        draw.text((x + px + round(px * .6), y + row_h / 2), label, font=font, fill=text, anchor="lm")
    return sheet

comparison = Image.new("RGBA", (599, 320), "#14161A")
draw = ImageDraw.Draw(comparison)
font = ImageFont.truetype(font_path, 15)
for x, label, dark, old in [(12, "Vorher", True, True), (211, "Neu · Dark", True, False), (410, "Neu · Light", False, False)]:
    draw.text((x, 14), label, font=font, fill="#EBECEF")
    comparison.alpha_composite(sidebar(15, dark, old), (x, 44))
comparison.save(out / "sidebar-comparison.png")

qa = Image.new("RGBA", (960, 568), "#14161A")
draw = ImageDraw.Draw(qa)
x = 12
for px, dark in ((11, True), (15, False), (19, True), (30, False)):
    preview = sidebar(px, dark)
    draw.text((x, 12), f"{px}px · {'Dark' if dark else 'Light'}", font=font, fill="#EBECEF")
    qa.alpha_composite(preview, (x, 44))
    x += preview.width + 12
qa.save(out / "sidebar-size-qa.png")

sizes = Image.new("RGBA", (850, 710), "#1C1D20")
draw = ImageDraw.Draw(sizes)
for row, name in enumerate(new_names):
    y = 20 + row * 112
    draw.text((12, y + 36), name, font=font, fill="#EBECEF")
    for col, px in enumerate((11, 15, 16, 19, 23, 30, 32, 64)):
        icon = native_icon(name, px, rgb((.545, .557, .592)))
        x = 110 + col * 90
        sizes.alpha_composite(icon, (x, y))
        draw.text((x, y + 74), str(px), font=font, fill="#EBECEF")
sizes.save(out / "glyph-size-qa.png")
print(f"Validated 24 new RGBA assets; {len(before)} existing PNGs unchanged; all category paths resolve.")
print(out)
