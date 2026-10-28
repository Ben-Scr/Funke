# UI art for Funke games

Ground truth: `Funke-Engine/src/Gui/NineSlice.hpp`, `Gui/GuiRenderer.cpp` (9-slice/tiled drawing), `Serialization/SceneSerializerDeserialize.cpp` (Image keys), `Graphics/Text/Font.cpp` + `FontManager.cpp` (fonts), `Project/FunkeProject.cpp` (UI scale). Proven pipelines: MarbleSky (`Tools/generate_art.py`, `ui_kit.py`, `ui2.py`, `Planning/Style-Guide.md` → UI), Mindcraft (`Tools/pixel_ui.py`, `ui_kit.py`, `DesignLanguage.md` §4–5). Widget behaviour (events, layout groups): `funke-game-ui`.

## The kit: one generator, one family

Generate the whole UI kit from `Design/palette.json` in one script (`Tools/art_ui.py`), and render a contact sheet of every piece in every state (`Tools/out/ui_kit.png`) for review:

| Piece | Use | States |
|---|---|---|
| `Panel` | screens, dialogs, cards | — |
| `Well` / `Inset` | recessed lists, slots, input fields, slider tracks | — |
| `Button` (primary, secondary, danger) | actions | normal, pressed (+ disabled tint) |
| `Pill` | HUD counters (coins, time, lives), tags | — |
| `IconButton` (round or square) | pause, settings, close, back | normal, pressed |
| `Toggle` / `Switch` track + knob | settings | off, on |
| `Slider` track, fill, knob | volume, sensitivity | — |
| `Card` | level select, shop items | locked, unlocked, selected |
| `Ribbon` / header | titles on panels, result screens | — |
| Icons (white on transparent, tinted by the Image colour) | play, pause, settings, back, lock, star, coin, heart, sound on/off | — |

## Two styles, two recipes

**Smooth (vector) UI** for flat, rounded and soft looks (MarbleSky, Erdwerk, Funkenfall):
- `pixelkit.rounded_panel(w, h, fill, radius, outline, outline_w, lip, lip_colour, gradient)` draws 4× supersampled with LANCZOS down to the exact size it is shown at, with a vertical gradient (top a little lighter), an outline ring and a bottom lip about 10 % of the height. Save with `upscale=1, filter="Bilinear"`: the edge colours are bled into transparent pixels, which prevents dark fringes.
- **Tintable pieces are drawn white/grey** and coloured by the `Image` colour (one sprite, many colours). **Material buttons** whose lip, contour and highlight must keep their own hues are baked once per colour (primary, positive, danger, gold, neutral).
- All contour, highlight and lip pixels lie inside the 9-slice border, and only a flat or vertical-gradient centre stretches. For a pill with round ends, the border equals the radius and `borderScale = rectHeight / spriteHeight`.

**Pixel UI** for pixel-art games (Mindcraft):
- Draw at art size with `pixelkit.pixel_panel` / `pixel_button(pressed=…)` / `bevel_rect` (1 px ink outline with cut corners, 1 px light top/left and dark bottom/right bevel, recess = reversed bevel, raised button = light top line + lighter band + darker lip).
- Save 4× nearest + Bilinear (`pixelkit.save_texture` default), with `border` in art px (the save multiplies by 4).
- Use `"imageType": 2` (Tiled) for frames whose edges have structure (planks, rivets), because stretching smears them. Use `1` (Sliced) for plain edges.
- `borderScale` = UI units per art px ÷ 4 (e.g. 1 art px = 2 UI units → `0.5`). The border then scales with the canvas, because `borderScale` is multiplied by the resolved UI scale.
- States as tints: hover ×1.16, pressed ×0.78, disabled ×0.5. Selection is a separate frame sprite drawn over the item.

## Image 9-slice in scene JSON

```json
"Image": { "textureAsset": "7300000000000000501", "imageType": 1, "border": [12, 12, 12, 12], "borderScale": 0.5,
           "r": 1, "g": 1, "b": 1, "a": 1 }
```
- `imageType` Simple=0 / Sliced=1 / Tiled=2. `border` is `[left, bottom, right, top]` in **texture** px (all 0 → taken from the texture's `.meta` `border`). `borderScale` defaults to 1. These keys are written only when the type isn't Simple. C# names: `Image.Type`, `Image.Border` (Vector4), `Image.BorderScale`.
- The corners show at `border × borderScale × UI scale`. Borders wider than the rect shrink proportionally.
- `Image` defaults to `filterMode` 1 (Bilinear) and calls `SetFilter` on the resolved texture. Set `"filterMode": 4` to follow the `.meta` (including a Point import); explicit overrides may affect other components sharing that texture handle.
- Button states: `transitionMode` ColorSwap=0 (tint `normal/hovered/pressed/disabled` colours) or SpriteSwap=1 (`normalSprite`, `pressedSprite`, … GUIDs). Baked pressed sprites need SpriteSwap, and a "press down" also moves the label (a small `ButtonDepth` script or an `.anim`).

## Text

- Fonts are `.ttf`/`.otf` only (no bitmap fonts). Glyphs are baked for **ASCII + Latin-1** (German umlauts work; Polish, Turkish, Cyrillic, typographic quotes, en-dashes and ellipsis don't).
- **Ship your fonts in `Assets/Fonts/`** with their licence (OFL fonts from Google Fonts: e.g. a rounded sans for body, a display or pixel font for titles), and set `"defaultFontAsset"` in `project.json`. Don't rely on engine built-in fonts: their set is changing.
- **Shadow/outline:** the Text Renderer's own effects, in text pixels (Font Size units): `"outlineWidth": 2` + `outlineR/G/B/A`, `"shadowX": 2, "shadowY": -2` + `shadowR/G/B/A` (C#: `OutlineWidth`, `OutlineColor`, `ShadowOffset`, `ShadowColor`). Use the ink token at ~85 % alpha; pixel fonts look crisp with 1 font pixel (whole multiples of the design size). No duplicate label behind the text (engines before 2026-10-19 needed one).
- Pixel fonts are only near-crisp at whole multiples of their design size (`fontSize × UI scale`), and the atlas is sampled linearly. Snap sizes to multiples (16, 32, 48 for a 16 px design grid).
- Hierarchy on the reference resolution: title 56–72, headings 36–44, body/buttons 28–34, small 22–26 (phones: nothing under ~26). Contrast ≥ 4.5:1 against the fill.
- `TextRenderer.sortOrder` must be higher than the panel's `Image` sortOrder, or the text draws behind it (e.g. panel 15, text 17, button 20, label 21).

## UI scale and layout

- `project.json`: `uiReferenceWidth`/`uiReferenceHeight` (1920×1080 default; 1200×1200 + `"uiScaleMode": "Fit"` works for PC and portrait phones), `uiScaleMode` `MatchWidthOrHeight` | `Fit` | `Fill`, `uiScaleMatch`. The resulting scale is rarely a whole number, which is why pixel UI uses sharp bilinear.
- `RectTransform2D` sizes are absolute reference units and anchors don't stretch. `"fillWidth": true` / `"fillHeight": true` span one parent axis; `"fillParent": true` spans both (the safe area for roots). `ignoreSafeArea` allows edge backgrounds to extend to the screen. Corner HUD items anchor and pivot on the same corner.
- A spacing grid (8 or 12 units), outer margin ≥ 48, one primary action per screen, touch targets ≥ 96 units on phones.

## Motion

Panels pop in (`Funke.Tween` `ScaleTo` 0.9 → 1, `Ease.OutBack`, 0.18 s), screens fade through a full-screen overlay, buttons press down on `OnClickDown`, counters count up, stars on the result screen appear one after another with a pop and a sound. Put the durations in the design language's motion section.

## Review

1. Contact sheet of the kit (all pieces, all states) → inspect it with `view_image`.
2. In-game screenshots of every screen at the reference size **and** at a different aspect (portrait phone for mobile games) → check crisp edges, text contrast, nothing overlapping, the same density everywhere.
3. `pixelkit.py check` on pixel UI textures (`--scale 4`).
