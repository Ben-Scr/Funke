---
name: funke-game-art
description: "Create or fix Funke game art, including pixel sprites, tiles, animation sheets, palettes, fonts and nine-slice UI artwork."
---

# Game art for Funke games

Use the project's design language to keep sprites, tiles, UI and animation at a consistent density and scale.

## 0. Start from the design language

Read `Design/DesignLanguage.md` and `Design/palette.json` in the project. They fix the art style, pixel density, palette tokens, typography, UI shapes and light direction. If they don't exist, run the pre-production step first (`funke-make-a-game` → `references/pre-production.md`): it asks the user, or decides alone when the user said so, and writes both files. Don't invent colours, sizes or fonts per asset. When the design language doesn't cover something, add the decision to the document first and then use it.

## 1. Pick the production approach per asset family

| Approach | Good for | How |
|---|---|---|
| **Text-grid sprites** (`pixelkit.from_grid`) | characters, enemies, items, icons, small props up to ~32 px | Draw each sprite as a text grid with palette letters, in a generator. Every pixel is placed deliberately, it diffs well, and it is easy to recolour and animate frame by frame. |
| **Procedural with rules** (`pixelkit` helpers + PIL) | UI frames and buttons, tiles with structure (bricks, planks, grass edges), gradients, glows, particle sprites, palette swatches | Shapes plus ramps plus bevels, never raw noise. Write the generator once and regenerate whenever the palette changes. |
| **AI image generation** (use the available `imagegen` skill/tool) | painted backgrounds, illustrations, many varied sprites | Write a precise brief (`references/pixel-art.md` → AI briefs). Inspect the output, recover the native grid per element and align it to the project's grid and palette before use. |
| **CC0 asset packs** (Kenney, etc.) | fast prototypes, fonts, sound | Only with the user's OK. Keep `LICENSE.txt` next to the files, then re-palette the pack so it matches the rest. |
| **Built-in shapes** (`Square`, `Circle`, … tinted) | the very first playable greybox | Placeholders only. Replace them before calling the game done. |

When an asset family would take more than an hour, or the user cares about the look, show the plan and **one sample** (one character with a frame or two, one tile, one panel with a button) before generating the rest. It costs one regenerate; rejecting a whole family later costs days.

## 2. Rules that make pixel art look intentional

Use one native grid, a shared light direction, hard alpha edges and readable silhouettes. Place texture in clusters and shade materials with palette ramps. Give hazards, pickups and checkpoints consistent colours across sprites, effects and HUD icons.

When resizing existing art, recover the native grid per element and reconstruct thin details on aligned cells. Prefer whole-number nearest-neighbour upscaling. For outlines, hue shifts, tile seams, animation timing and AI briefs, use [the pixel art guide](references/pixel-art.md).

## 3. Rules that make UI look like a game, not a debug panel

Derive panels, buttons, wells and tooltips from one frame family. Enable slicing on the actual `Image` and keep icons and handles proportional. Match interactive states and motion to the game's art. Keep critical feedback legible while animating.

Use [UI art](references/ui-art.md) for border order and scale, font sizes, contrast, layout and motion. Use [game UI](../funke-game-ui/SKILL.md) for widget behavior and testing states at different sizes and fill amounts.

## 4. Into Funke

- **Files:** `Assets/Textures/<Family>/…` (`Characters`, `Environment`, `Items`, `UI`, `VFX`), fonts in `Assets/Fonts/` with their licence. One generator per family in `Tools/` (`Tools/art_characters.py`, `Tools/art_ui.py`), all reading `Design/palette.json`, and one `Tools/build_art.py` that runs them all.
- **Write the `.meta` before the PNG** (`pixelkit.save_texture` does both, atomically and only on change). An open Editor imports a new PNG immediately and gives it a random GUID. If the scene already references your planned GUID, the sprite renders magenta or empty and logs no error.
- **Choose filtering for the target view:** `Point` with whole-number screen scaling and an aligned camera preserves hard pixels. For fractional UI/camera scales, the helper defaults to native art stored 4× nearest-upscaled with `Bilinear`; this reduces filtering softness and the effect of half-texel slice insets, but is not pixel-perfect at arbitrary scales. Inspect both the native art and the actual final-size result. Smooth UI art: `upscale=1`, Bilinear, colour-bled edges.
- **Let `.meta` own the filter:** SpriteRenderer defaults to `filterMode: 4` (Default); explicitly set `Image.filterMode: 4` too when it should follow the import setting, because Image defaults to Bilinear. Non-default component filters call `SetFilter` on the resolved texture and can affect other users sharing its handle.
- **Sheets:** pack frames with `pixelkit.pack_sheet` and save them with `sprites=` (named slices go into the `.meta`, rects scaled with the upscale), or slice with `funke sprite slice <png> --grid 64x64` after writing your own `.meta`. Reference a slice with `spriteName`, and animate with an `Animator` `.anim` clip using discrete `SpriteRenderer.Texture` keys.
- **Size in the world:** `SpriteRenderer.pixelsPerUnit` = art px per tile × upscale (16 px tiles saved 4× → 64) with `Transform2D` scale 1. Every sprite keeps its native pixel size and pivot, so one density holds by construction.

## 5. Verify

1. `python <skill>/scripts/pixelkit.py check Assets/Textures/Characters/*.png … --project . --scale 4` (pixel art saved 4×) reports colours outside the palette, soft alpha edges and a wrong pixel grid.
2. Open every generated sheet with `view_image`, at native and actual final size plus an integer enlargement. Check thin details, outlines, symmetry, silhouette and transparency; lint cannot judge these.
3. Take an **in-game** screenshot (`funke-make-a-game` → `references/testing.md`) at the target resolution and judge it there. Lighting, ambient light, UI scale and filtering change how art looks. Compare it with `Design/style-sample.png`.
   Check for shimmer during movement, unreadable labels, overlaps and leftover placeholders.

## Scripts

`scripts/pixelkit.py` — importable library + CLI (needs Pillow):

| Function / command | Use |
|---|---|
| `Palette.load(path)`, `pal["primary"]`, `pal["grass.3"]`, `pal.ramp_of(token)` | palette tokens and ramps (every token gets a derived 5-step ramp) |
| `ramp(hex, n)` / `pixelkit.py ramp "#4C8C4A"` | hue-shifted ramp dark → light |
| `from_grid(text, legend, pal)`, `mirror_x`, `outline(img, colour)` | text-grid sprites |
| `bevel_rect`, `pixel_panel`, `pixel_button(pressed=…)` | pixel-art frames and buttons (9-slice border 3, +lip at the bottom) |
| `rounded_panel(w, h, fill, radius, outline, outline_w, lip, …, gradient)` | smooth UI shapes, 4× supersampled |
| `scale(img, k)` | nearest-neighbour upscale |
| `pack_sheet(frames)` / `pixelkit.py sheet` | sprite sheet + slices for the `.meta`; every slice keeps its own size and gets its edge pixels extruded by 1 art px (no seams between tiles under Bilinear) |
| `save_texture(path, img, project, filter)` / `write_meta` / `pixelkit.py meta` | meta first, then PNG, atomic, keeps existing GUIDs |
| `pixelkit.py sample <project>` | `Design/style-sample.png` for approval |
| `pixelkit.py check <png…> --project P --scale k` | pixel-art lint |
