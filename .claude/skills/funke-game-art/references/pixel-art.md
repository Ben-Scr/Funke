# Pixel art for Funke games

Ground truth for the engine side: `Funke-Engine/src/Assets/AssetRegistry.hpp` (texture `.meta`), `Graphics/SpriteQuad.cpp` (sprite size), `Graphics/SpriteUVResolver.cpp` (slice UVs), `Graphics/TextureManager.cpp` (filters). Proven pipelines: Mindcraft (`C:\Users\benso\Index\Minecraft\Tools\pixel_*.py`, `ai_*.py`, `DesignLanguage.md`), SurvivalSiege (Unity sheets).

## Decide the grid first

Write these into `Design/DesignLanguage.md` → "Scale and pixel rules" before drawing anything:

| Decision | Typical | Note |
|---|---|---|
| Tile size | 16 px (retro, readable on phones) or 32 px (more detail) | one world unit = one tile |
| Character size | 16×16 / 16×24 (16 px tiles), 32×32 / 32×48 (32 px tiles) | same pixel grid as the tiles |
| Item/icon size | same as the tile, or exactly half | never a third density |
| Screen px per art px (k) | 3–4 at 1080p for 16 px tiles, 2 for 32 px | sets `Camera2D.orthoSize` |
| UI art px | the same k as the world, or a whole fraction of it | a UI art pixel looks as big as a world art pixel |
| Outline | 1 px dark outline on characters/items, none on tiles | or a "selective outline": darker ramp step per side |
| Light | from the top-left | bevels, highlights and shadows all agree |
| Colours per sprite | 3–5 per material ramp, 8–16 per sprite | palette in `Design/palette.json` |

## Drawing techniques for an agent

**Text grids (best for sprites up to ~32 px).** Every pixel is a decision, and the result is reviewable as text:

```python
import sys; sys.path.insert(0, r"<skill>/scripts"); import pixelkit as pk
pal = pk.Palette.load(P / "Design/palette.json")
LEGEND = {"k": "ink", "1": "slime.0", "2": "slime.1", "3": "slime.2", "4": "slime.3", "w": "#FFFFFF"}
IDLE_0 = """
....kkkkkkkk....
..kk33333333kk..
.k334433333333k.
k33344333333333k
k33333w3333w333k
k3333kk3333kk33k
k22333333333322k
.k222222222222k.
..kkkkkkkkkkkk..
"""
frame = pk.from_grid(IDLE_0, LEGEND, pal)
```
- Draw symmetric sprites half-way and mirror them (`pk.mirror_x`). Write each animation frame as its own grid, or derive frames by shifting rows (bob = move the body 1 px down, squash = remove a row and widen it).
- Use letters for ramp steps (`1`–`5` dark → light) so recolouring a whole enemy family means changing only the legend.

**Rule-based procedural (tiles, UI, props with structure).** Build from ramps and shapes, never from per-pixel random noise: planks are rectangles with a highlight line and a shadow line plus 2–3 placed knots, and bricks are offset rows with a mortar ramp step. Grass is a top band with a hand-made blade pattern repeated with offsets. Use seeded randomness only to *choose* between hand-made variants. Tiles must be seamless: check a 3×3 repeat of every tile in a contact sheet.

**AI image generation (illustrations, many varied sprites).** Use the available `imagegen` skill/tool for generation and image edits. The following brief and import techniques are useful when preparing generated sprites:
- The brief specifies the intended size, a shared style paragraph, one element per cell with enough transparent padding, no text or glow halos, and a consistent one-art-pixel outline. If a keyed background is specifically needed, keep its colour out of the elements. Long items lie diagonally. A mockup comes first and serves as the reference for every later sheet. Each brief ends with a "Check:" line.
- Prefer an actual transparent background when supported. For existing keyed sheets, recover the native grid from each element before removing the key colour or reconstructing cells. Preserve the silhouette and thin features; a majority-colour reduction can erase them. Do not arbitrarily shrink a finished sheet. Use aligned cells and whole-number nearest-neighbour upscales, then inspect the actual final-size assets, including outlines, symmetry and transparency.
- AI output can drift in pixel size and have soft edges. Code-generated rectangular frames are useful for exact UI geometry. Use the image-editing tool for generated-image revisions unless the user explicitly requests another editing method; inspect the result after each revision.
- Cache every generation with its prompt (`ArtSource/ai/<name>.png` + `.json`) and never regenerate silently. Never claim an AI sheet exists before you have checked the folder.

**Asset packs.** CC0 packs (Kenney, etc.) only with the user's OK, with the licence file next to them. Re-palette them (`pixelkit check` shows the colours that are off), and keep their pixel density or redraw them to yours. Never mix two packs' styles.

## Saving and importing

`pixelkit.save_texture()` defaults to **sharp bilinear**: draw at native size, save the PNG **4× nearest-neighbour upscaled**, and import it with `"filter": "Bilinear"` (`upscale=4, filter="Bilinear"`). It writes the `.meta` first. This is a rendering tradeoff for fractional scales, not proof of pixel accuracy:

- Funke has no built-in pixel snapping. At fractional scales, `Point` can give uneven pixel widths and shimmer while scrolling; bilinear on a 4× texture limits the source-space filtering footprint but still needs checking at the intended screen size.
- **Named slices are inset by half a texel** (`SpriteUVResolver.cpp`) to stop bleeding. At 4× storage the inset is 1/8 of an art pixel per edge. Its visible effect depends on the destination scale; check edge rows and thin details in the final-size sprite.
- `Point` stays fine for whole-number scales with a pixel-snapped camera, or for one-PNG-per-sprite (whole textures aren't inset). Point never uses mips, so art shown smaller than 1:1 aliases.

`.meta` for a sheet (written by `pixelkit.save_texture(..., sprites=…)`, rects in texture px from the **top-left**, pivot 0..1 from the slice's **bottom-left**):
```json
{ "AssetGUID": "7300000000000000001", "uuid": "7300000000000000001", "kind": "Texture",
  "import": { "filter": "Bilinear", "wrapU": "Clamp", "wrapV": "Clamp" },
  "sprites": [ { "name": "slime_idle_0", "rect": [0, 0, 64, 64], "pivot": [0.5, 0.0] } ] }
```
- `filter` is `Point` | `Bilinear` | `Trilinear` | `Anisotropic` (case-sensitive), and `wrapU/V` is `Repeat` | `Clamp` | `Mirror` | `Border`. There are no mip, PPU or sRGB keys. Textures are uploaded as UNORM; final screen colours still depend on tint, filtering, lighting and post-processing.
- Without an `import` block a `SpriteRenderer` samples with Point and an `Image` with Bilinear. A non-default component `filterMode` calls `SetFilter` on the resolved texture and can affect components sharing its handle. Leave SpriteRenderer at its default 4 and set `Image.filterMode: 4` explicitly when the `.meta` should decide.
- Write your own `.meta` (GUID) **before** running `funke sprite slice` or saving the PNG while an Editor is open, because both roll a random GUID otherwise. Never copy `.meta` files.
- **Seams between tiles** (thin dark lines at every tile border) mean a slice is bigger than its art (a transparent column) or a neighbour in the atlas bleeds in. `pixelkit.pack_sheet` keeps each slice at its frame's own size and extrudes edge pixels by 1 art px. Check the slice rects in the `.meta` (64×64 for a 16 px tile saved 4×) and zoom into an in-game screenshot at a tile border.
- Foot pivots (`[0.5, 0]`) for characters standing on the ground, centre pivots for projectiles and pickups.
- Alpha is binary (0/255) for pixel art. `pixelkit.check` flags soft edges. Smooth art needs its transparent RGB bled outward (`pixelkit.bleed`, done automatically for upscale=1 non-Point saves): Funke blends straight alpha, so dark fringes appear otherwise.

## In the scene

```json
"SpriteRenderer": { "textureAsset": "7300000000000000001", "spriteName": "slime_idle_0", "pixelsPerUnit": 64 },
"Animator": { "clipAsset": "7300000000000000002" }
```
- `pixelsPerUnit` = art px per tile × upscale (16 px tiles saved 4× → 64). The slice then keeps its native size and sits on its pivot, with `Transform2D` scale 1. Flip with `flipX` (it mirrors around the pivot) instead of drawing left-facing frames.
- `clipAsset` points to the GUID of a looping `.anim` clip whose `SpriteRenderer.Texture` track has discrete keys `"<textureGuid>|slice|slime_idle_0"`, `"<textureGuid>|slice|slime_idle_1"`, etc. For idle/run/jump/hurt, assign an `.animctrl` through `controllerAsset` and use parameters to switch clips (`funke-make-a-game` → `scene-json.md` → Animation).
- Frame counts and timing: idle 2–4 frames at 4–6 fps, walk/run 4–8 at 10–12 fps, attack anticipation (1–2 frames held) → strike (1 frame) → recovery, hurt = 1 white-flash frame + knockback, death 4–6 frames or a particle burst.
- Camera: `orthoSize = screenHeight / (2 · tilePx · k)`. See `funke-make-a-game` → `references/2d-games.md` for the pixel camera script and position rounding.
- `project.json` `graphics`: keep `antiAliasing` Off or MSAA (FXAA blurs pixel art). For a deliberately low-res world use `renderScale` 0.5 + `upscaleFilter` Point; the UI stays at full resolution. `PostProcessing2D.pixelated` fakes a low-res look but doesn't replace real pixel art.

## Generator layout

```
Tools/
├── build_art.py          # runs all art generators; one command after any palette change
├── art_characters.py     # text-grid sprites + animation frames -> sheets with slices
├── art_tiles.py          # tiles + 3x3 seam contact sheet in Tools/out/
├── art_items.py          # items/icons
├── art_ui.py             # frames, buttons, icons (see ui-art.md)
└── out/                  # contact sheets for review (not in Assets/)
```
Every generator reads `Design/palette.json`, writes through `pixelkit.save_texture` (atomic, only on change, stable GUIDs) and renders a contact sheet into `Tools/out/` that you open with `view_image` before running the game.
