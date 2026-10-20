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

**AI image generation (illustrations, many varied sprites).** The user runs the generator (e.g. Codex `$imagegen`), and you write the brief and the importer. What worked in Mindcraft:
- The brief has one exact output path and size, a shared style paragraph repeated verbatim in every brief, an exact grid with one element per cell, ≥ 40 px of pure magenta `#FF00FF` between elements, no text, no drop shadows or glow halos, no magenta inside elements, and a 1-big-pixel ink outline (it is used to measure the pixel size). Long items lie diagonally. A mockup comes first and serves as the reference for every later sheet. Each brief ends with a "Check:" line.
- The importer keys out magenta (and the magenta fringe), measures the big-pixel size from the outline, re-pixelates each cell to the target grid (most frequent colour per cell, ignoring a 1/5 margin, alpha by majority), snaps colours to the palette, adds the outline in code, and **falls back to the hand-made generator** when a sheet is missing. That way the build never depends on generated files.
- AI output drifts in pixel size and has soft edges. Draw rectangular UI frames in code (using the mockup's colours) and use AI only for icons and organic shapes. Regenerate a failed image once, then fix it in code.
- Cache every generation with its prompt (`ArtSource/ai/<name>.png` + `.json`) and never regenerate silently. Never claim an AI sheet exists before you have checked the folder.

**Asset packs.** CC0 packs (Kenney, etc.) only with the user's OK, with the licence file next to them. Re-palette them (`pixelkit check` shows the colours that are off), and keep their pixel density or redraw them to yours. Never mix two packs' styles.

## Saving and importing

The default that works at any scale in Funke is **sharp bilinear**: draw at native size, save the PNG **4× nearest-neighbour upscaled**, and import it with `"filter": "Bilinear"`. `pixelkit.save_texture()` does exactly that by default (`upscale=4, filter="Bilinear"`) and writes the `.meta` first. Why:

- Funke has no pixel snapping, and UI and camera scales are rarely whole numbers. Plain `Point` filtering then gives uneven pixel widths and shimmer while scrolling, whereas bilinear on a 4× texture only blurs a quarter of an art pixel at each edge.
- **Named slices are inset by half a texel** (`SpriteUVResolver.cpp`) to stop bleeding. On a 1× Point sheet that squeezes every frame by one texel and makes the outer pixel rows half-width. At 4× the inset is 1/8 of an art pixel, which is invisible.
- `Point` stays fine for whole-number scales with a pixel-snapped camera, or for one-PNG-per-sprite (whole textures aren't inset). Point never uses mips, so art shown smaller than 1:1 aliases.

`.meta` for a sheet (written by `pixelkit.save_texture(..., sprites=…)`, rects in texture px from the **top-left**, pivot 0..1 from the slice's **bottom-left**):
```json
{ "AssetGUID": "7300000000000000001", "uuid": "7300000000000000001", "kind": "Texture",
  "import": { "filter": "Bilinear", "wrapU": "Clamp", "wrapV": "Clamp" },
  "sprites": [ { "name": "slime_idle_0", "rect": [0, 0, 64, 64], "pivot": [0.5, 0.0] } ] }
```
- `filter` is `Point` | `Bilinear` | `Trilinear` | `Anisotropic` (case-sensitive), and `wrapU/V` is `Repeat` | `Clamp` | `Mirror` | `Border`. There are no mip, PPU or sRGB keys. Textures are uploaded as UNORM, so the PNG's hex values reach the screen unchanged.
- Without an `import` block a `SpriteRenderer` samples with Point and an `Image` with Bilinear. **A `filterMode` other than 4 on any SpriteRenderer/Image changes the filter of the shared texture for every user.** Leave `filterMode` out (SpriteRenderer default 4 = use the `.meta`) and let the `.meta` decide.
- Write your own `.meta` (GUID) **before** running `funke sprite slice` or saving the PNG while an Editor is open, because both roll a random GUID otherwise. Never copy `.meta` files.
- **Seams between tiles** (thin dark lines at every tile border) mean a slice is bigger than its art (a transparent column) or a neighbour in the atlas bleeds in. `pixelkit.pack_sheet` keeps each slice at its frame's own size and extrudes edge pixels by 1 art px. Check the slice rects in the `.meta` (64×64 for a 16 px tile saved 4×) and zoom into an in-game screenshot at a tile border.
- Foot pivots (`[0.5, 0]`) for characters standing on the ground, centre pivots for projectiles and pickups.
- Alpha is binary (0/255) for pixel art. `pixelkit.check` flags soft edges. Smooth art needs its transparent RGB bled outward (`pixelkit.bleed`, done automatically for upscale=1 non-Point saves): Funke blends straight alpha, so dark fringes appear otherwise.

## In the scene

```json
"SpriteRenderer": { "textureAsset": "7300000000000000001", "spriteName": "slime_idle_0", "pixelsPerUnit": 64 },
"SpriteAnimator2D": { "frames": ["slime_idle_0", "slime_idle_1", "slime_idle_2", "slime_idle_1"], "fps": 6, "loop": true, "playOnStart": true }
```
- `pixelsPerUnit` = art px per tile × upscale (16 px tiles saved 4× → 64). The slice then keeps its native size and sits on its pivot, with `Transform2D` scale 1. Flip with `flipX` (it mirrors around the pivot) instead of drawing left-facing frames.
- `SpriteAnimator2D` plays one frame list (C# can't swap the list). For idle/run/jump/hurt use an `Animator` with `.anim` clips whose `SpriteRenderer.Texture` track has discrete keys `"<textureGuid>|slice|slime_run_0"` and an `.animctrl` with parameters (`funke-make-a-game` → `scene-json.md` → Animation).
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
Every generator reads `Design/palette.json`, writes through `pixelkit.save_texture` (atomic, only on change, stable GUIDs) and renders a contact sheet into `Tools/out/` that you open with the Read tool before running the game.
