"""Package ImageGen originals for the editor; never draw or reconstruct artwork."""

import argparse
import json
import math
import os
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
SOURCES = Path(__file__).resolve().parent / "sources" / "editor-imagegen"
DESTINATION = ROOT / "Funke-Runtime/FunkeAssets/Textures/Editor"


def prepare(source: Path, source_box=None) -> Image.Image:
    with Image.open(source) as original:
        if original.mode != "RGBA":
            raise ValueError(f"Missing transparency: {source}")
        alpha = original.getchannel("A")
        # Atlas cells are packaging boundaries; preserve ImageGen's original silhouette.
        if source_box is not None:
            alpha = alpha.crop(tuple(source_box))
    low, high = alpha.getextrema()
    if low != 0 or high < 250:
        raise ValueError(f"Expected transparent background and opaque strokes: {source}")
    bounds = alpha.point(lambda value: 255 if value > 8 else 0).getbbox()
    if bounds is None:
        raise ValueError(f"Empty icon: {source}")
    # Padding is standardized; the generated silhouette and its alpha stay intact.
    alpha = alpha.crop(bounds)
    icon = Image.new("RGBA", alpha.size, "white")
    icon.putalpha(alpha)
    return icon


def resize(icon: Image.Image, size: int) -> Image.Image:
    extent = max(1, round(size * 0.8))
    ratio = extent / max(icon.size)
    dimensions = tuple(max(1, round(side * ratio)) for side in icon.size)
    scaled = icon.resize(dimensions, Image.Resampling.LANCZOS)
    result = Image.new("RGBA", (size, size))
    result.alpha_composite(scaled, ((size - scaled.width) // 2, (size - scaled.height) // 2))
    return result


def preview(assets: list, output: Path):
    columns, cell_width, cell_height = 5, 210, 110
    sheet = Image.new("RGBA", (columns * cell_width, math.ceil(len(assets) / columns) * cell_height), "#202124")
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 12)
    for index, (asset, icon) in enumerate(assets):
        x, y = index % columns * cell_width, index // columns * cell_height
        draw.text((x + 8, y + 6), asset["id"], fill="#dddddd", font=font)
        for offset, background, foreground in ((0, "#202124", "#dddddd"), (105, "#f1f2f4", "#303238")):
            panel = Image.new("RGBA", (100, 64), background)
            for size, left in ((16, 10), (32, 44)):
                image = resize(icon, size)
                tinted = Image.new("RGBA", image.size, foreground)
                tinted.putalpha(image.getchannel("A"))
                panel.alpha_composite(tinted, (left, (64 - size) // 2))
            sheet.alpha_composite(panel, (x + offset, y + 28))
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--partial", action="store_true", help="Preview only the originals already available")
    parser.add_argument("--write", action="store_true", help="Export selected originals to runtime assets")
    parser.add_argument("--preview", type=Path, default=ROOT / "bin-int/_agent/imagegen-icons/contact.png")
    args = parser.parse_args()
    manifest = json.loads((SOURCES / "manifest.json").read_text(encoding="utf-8-sig"))
    assets, missing, ids = [], [], set()
    for asset in manifest["assets"]:
        if asset["id"] in ids:
            raise ValueError(f"Duplicate icon: {asset['id']}")
        ids.add(asset["id"])
        source = SOURCES / asset["source"]
        if not source.is_file():
            missing.append(asset["id"])
            continue
        provenance = json.loads(source.with_suffix(".json").read_text(encoding="utf-8"))
        if provenance["tool"] != "image_gen.imagegen" or not provenance.get("prompt"):
            raise ValueError(f"Missing generation provenance: {source}")
        assets.append((asset, prepare(source, asset.get("source_box"))))
    if missing and not args.partial:
        raise ValueError(f"Missing ImageGen originals: {', '.join(missing)}")
    if args.write:
        for asset, icon in assets:
            folder = DESTINATION / asset["group"] / asset["id"]
            folder.mkdir(parents=True, exist_ok=True)
            for size in asset["sizes"]:
                target = folder / f"{asset['id']}_{size}.png"
                # Readers may still map the old PNG while the editor is running.
                with tempfile.NamedTemporaryFile(dir=folder, suffix=".png", delete=False) as temporary:
                    temporary_path = Path(temporary.name)
                    resize(icon, size).save(temporary, format="PNG", optimize=True)
                try:
                    os.replace(temporary_path, target)
                finally:
                    temporary_path.unlink(missing_ok=True)
    preview(assets, args.preview)
    print(f"{'Exported' if args.write else 'Previewed'} {len(assets)} icons; missing {len(missing)}. Preview: {args.preview}")


if __name__ == "__main__":
    main()
