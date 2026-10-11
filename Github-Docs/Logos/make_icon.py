import sys
from pathlib import Path
from PIL import Image, ImageDraw

ROW_COLORS = {-3: "#FFD166", -2: "#FFB55E", -1: "#FF9757", 0: "#FF7A52", 1: "#F25C54", 2: "#D94452", 3: "#B83250"}
REMOVED = (2, -1)   # cell that "escapes"
ESCAPED = (3, -3)   # where it lands (still inside the 7x7 box)
TRAIL = (2.5, -2.0)  # centre of the half-size trail square, in cell units


def hex_rgba(h, a=255):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def render(size):
    pad = max(1, round(size * 0.06))
    step = (size - 2 * pad) // 7
    gap = step // 8
    origin = (size - step * 7) // 2  # top-left of the 7x7 grid, centred
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def cell(i, j, color):
        x = origin + (i + 3) * step
        y = origin + (j + 3) * step
        d.rectangle([x, y, x + step - gap - 1, y + step - gap - 1], fill=color)

    for j in range(-3, 4):
        for i in range(-3, 4):
            if abs(i) + abs(j) <= 3 and (i, j) != REMOVED:
                cell(i, j, hex_rgba(ROW_COLORS[j]))
    cell(*ESCAPED, hex_rgba(ROW_COLORS[REMOVED[1]]))

    if size >= 32:
        half = max(1, (step - gap) // 2)
        cx = origin + (TRAIL[0] + 3) * step + (step - gap) / 2
        cy = origin + (TRAIL[1] + 3) * step + (step - gap) / 2
        x0, y0 = round(cx - half / 2), round(cy - half / 2)
        d.rectangle([x0, y0, x0 + half - 1, y0 + half - 1], fill=hex_rgba(ROW_COLORS[REMOVED[1]], 115))
    return img


out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
render(1024).save(out / "icon_1024.png")
render(256).save(out / "icon.png")
ico_sizes = [256, 128, 64, 48, 32, 24, 16]
imgs = [render(s) for s in ico_sizes]
imgs[0].save(out / "icon.ico", format="ICO", sizes=[(s, s) for s in ico_sizes], append_images=imgs[1:])

# preview sheet on dark + light backgrounds
sheet = Image.new("RGBA", (40 + sum(s + 24 for s in ico_sizes), 2 * 300), (0, 0, 0, 0))
for row, bg in enumerate([(30, 30, 34, 255), (243, 241, 236, 255)]):
    ImageDraw.Draw(sheet).rectangle([0, row * 300, sheet.width, row * 300 + 299], fill=bg)
    x = 20
    for s, im in zip(ico_sizes, imgs):
        sheet.alpha_composite(im, (x, row * 300 + (300 - s) // 2))
        x += s + 24
sheet.save(out / "preview.png")
print(Image.open(out / "icon.ico").info["sizes"])
