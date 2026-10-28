---
name: funke-game-art
description: Use when CREATING OR FIXING THE ART of a Funke game — pixel art sprites, tiles, character animation frames, sprite sheets, items and icons, UI art (panels, buttons, 9-slice frames, HUD pills, cards), fonts, palettes, art direction and design language, or AI-generated / imported art that must fit the game. Triggers on "pixel art", "sprite sheet", "tileset", "icon", "UI looks bad / cheap / flat", "make it look better", "art style", "palette", "design language", "9-slice", "font", "replace placeholder art", "inconsistent look", "blurry pixel art", "AI image", "asset pack". Gives the production approach (design language first, one pixel density, a sample before rollout), the rules that make pixel art and UI look intentional, the Funke import side (sharp-bilinear saving, .meta slices, Image 9-slice keys, pixelsPerUnit, fonts) and pixelkit.py (palette ramps, text-grid sprites, pixel and rounded frames, sheets, metas, style sample, lint). Scene wiring and game code go to funke-make-a-game.
---

# Game art for Funke games

Art is where agent-made games most often fall flat. Each asset can be fine on its own and the game still looks cheap, because the assets don't agree with each other: three pixel densities, a blurry upscale next to crisp pixels, a palette chosen file by file, the default font on a pixel-art menu, flat grey rectangles as buttons. This skill gets every asset to follow **one** design language, makes the agent show a sample before it produces a whole asset family, and provides tools that keep the rules mechanical.

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

1. **One density.** Every sprite, tile and UI art pixel uses the same pixel size on screen. A 16 px tile game has 16 px tiles, characters drawn on that grid, icons on that grid, and UI art scaled by a whole number. Never mix a 32 px item into a 16 px world, and never resize pixel art with a smooth filter.
2. **Palette ramps with hue shift.** Each material has a 4–5 step ramp from dark to light. Shadows lean toward blue/violet, highlights toward warm yellow. `pixelkit.ramp("#4C8C4A")` or the ramps in `palette.json`. No colours outside the palette (`pixelkit.py check` lists them).
3. **One light direction** (default: top-left) for every sprite, tile and UI bevel. Raised things are light on the top/left and dark on the bottom/right; recessed things are the reverse.
4. **Hard edges.** Alpha is 0 or 255, without anti-aliased fringes (they turn into blurry halos in game). Dither or use ramp steps instead of soft gradients inside sprites.
5. **Readable silhouettes and outlines.** Characters and pickups get a dark outline (ink token, or a darker ramp step per side for a softer look) so they read against any background. Tiles usually don't get one. Decide once, and write it in the design language.
6. **No pillow shading, no noise.** Shade from the light direction, not from the edges inward. Texture means placed clusters (cracks, planks, blades of grass), not random noise. Random per-pixel noise is the most common sign of generated art.
7. **Signature colours.** Each thing the player must learn (hazard, pickup, checkpoint, enemy type) keeps one colour everywhere: sprite, particles, UI icon.
8. **Animation:** 2–4 frames for idle/bob, 4–8 for walk/run, held keys on impact. Squash and stretch on landing and jumping, a white hit flash, and frames that stay on the grid.
9. **Resizing existing art:** recover its native grid before changing size. Resample or reconstruct individual elements on aligned cells; use whole-number nearest-neighbour upscaling where possible. Do not downscale a finished sheet by an arbitrary non-integer factor. Nearest-neighbour alone does not prove that outlines, symmetry, thin details or the silhouette survived.

Details, frame counts, tile edge rules, the AI brief template and import settings: `references/pixel-art.md`.

## 3. Rules that make UI look like a game, not a debug panel

1. **Every interactive element is a sprite with depth:** an outline, a highlight on top, a lip or shadow at the bottom, with baked **normal** and **pressed** textures (the pressed one without lip, moved down). Flat rounded rectangles with a colour swap look like a prototype.
2. **9-slice everything that stretches.** Panels, buttons and bars use `Image` sliced mode, so the corners stay crisp at any size. Bake all detail into the borders and keep the centre flat or a simple vertical gradient.
3. **One frame family:** panel, well (recessed list or slot), card, button, pill, tooltip, all built by the same generator from the same tokens.
4. **Text:** at most two fonts (title + body). A pixel font only at whole multiples of its design size. Contrast at least 4.5:1 against its fill. Body text on the 1080p reference no smaller than ~26 units on phones. Text needs a higher `sortOrder` than its panel.
5. **Layout:** a spacing grid (8 or 12 units), consistent outer margins, one primary action per screen, touch targets ≥ 96 reference units on phones.
6. **Motion:** panels pop in (scale 0.9 → 1 with a little overshoot, 0.15–0.25 s), buttons press down, numbers count up. A static UI feels dead.

Engine keys (Image `Type`/borders/`BorderScale`, sprite states on buttons), pixel-perfect UI scaling, fonts and HUD recipes: `references/ui-art.md`.

## 4. Into Funke

- **Files:** `Assets/Textures/<Family>/…` (`Characters`, `Environment`, `Items`, `UI`, `VFX`), fonts in `Assets/Fonts/` with their licence. One generator per family in `Tools/` (`Tools/art_characters.py`, `Tools/art_ui.py`), all reading `Design/palette.json`, and one `Tools/build_art.py` that runs them all.
- **Write the `.meta` before the PNG** (`pixelkit.save_texture` does both, atomically and only on change). An open Editor imports a new PNG immediately and gives it a random GUID. If the scene already references your planned GUID, the sprite renders magenta or empty and logs no error.
- **Choose filtering for the target view:** `Point` with whole-number screen scaling and an aligned camera preserves hard pixels. For fractional UI/camera scales, the helper defaults to native art stored 4× nearest-upscaled with `Bilinear`; this reduces filtering softness and the effect of half-texel slice insets, but is not pixel-perfect at arbitrary scales. Inspect both the native art and the actual final-size result. Smooth UI art: `upscale=1`, Bilinear, colour-bled edges.
- **Let `.meta` own the filter:** SpriteRenderer defaults to `filterMode: 4` (Default); explicitly set `Image.filterMode: 4` too when it should follow the import setting, because Image defaults to Bilinear. Non-default component filters call `SetFilter` on the resolved texture and can affect other users sharing its handle.
- **Sheets:** pack frames with `pixelkit.pack_sheet` and save them with `sprites=` (named slices go into the `.meta`, rects scaled with the upscale), or slice with `funke sprite slice <png> --grid 64x64` after writing your own `.meta`. Reference a slice with `spriteName`, and animate with an `Animator` `.anim` clip using discrete `SpriteRenderer.Texture` keys.
- **Size in the world:** `SpriteRenderer.pixelsPerUnit` = art px per tile × upscale (16 px tiles saved 4× → 64) with `Transform2D` scale 1. Every sprite keeps its native pixel size and pivot, so one density holds by construction.
- **9-slice UI:** `"Image": {"imageType": 1, "border": [l, b, r, t], "borderScale": …}` (Tiled = 2 for structured pixel frames). Fonts ship in `Assets/Fonts/` and cover ASCII + Latin-1 only; outlines and drop shadows are Text Renderer keys (`outlineWidth`, `shadowX/Y` + colors), no duplicate label. Details: `references/ui-art.md`.

## 5. Verify

1. `python <skill>/scripts/pixelkit.py check Assets/Textures/Characters/*.png … --project . --scale 4` (pixel art saved 4×) reports colours outside the palette, soft alpha edges and a wrong pixel grid.
2. Open every generated sheet with `view_image`, at native and actual final size plus an integer enlargement. Check thin details, outlines, symmetry, silhouette and transparency; lint cannot judge these.
3. Take an **in-game** screenshot (`funke-make-a-game` → `references/testing.md`) at the target resolution and judge it there. Lighting, ambient light, UI scale and filtering change how art looks. Compare it with `Design/style-sample.png`.
4. Check every screen for: one density, crisp pixels (no blur, no shimmer when moving), text readable on its background, nothing overlapping, no placeholder shapes left.

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
