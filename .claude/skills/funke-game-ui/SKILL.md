---
name: funke-game-ui
description: "Design and implement Funke game UI: integrate HUDs, menus and shops with the game's visual language; turn gameplay state into readable visual feedback; build widgets, layout and C# event wiring. Use when game UI looks foreign, generic or text-heavy, or when working on RectTransform, Image, Button, Slider, Toggle, InputField, Dropdown, ScrollRect, Interactable, UIPointer, safe areas or UI layout groups. Also covers the UIComponents.cs and UIPointer.cs contracts and UIEventSystem implementation. Script lifecycle goes to funke-game-scripting, inspector fields to funke-inspector-ruleset, asset production to funke-game-art. This is game UI, not the Funke Editor or Launcher skin."
---

# Funke Game UI

Funke UI is **ECS UI**: a UI element is an entity carrying a `RectTransform` plus UI components (`Image`, `Button`, `Slider`, …). You lay it out in the Editor and drive behavior from C# by subscribing to each component's `event`s. UI widgets and layout types live in **`namespace Funke.UI`** (`using Funke.UI;`). The native side that turns pointer input into events is `Funke-Engine/src/Systems/UIEventSystem.{hpp,cpp}`. Source of truth: `Funke-ScriptCore/Source/Funke/UI/UIComponents.cs`.

## Make the UI belong to the game

Read the project's design language and inspect the actual gameplay art or a current game screenshot before designing visible UI. For a new game, establish this alongside the art direction in `funke-make-a-game` pre-production. **The HUD is part of the game's visual experience, not a separate panel of labelled variables.** Apply these principles to the affected screens; a small UI fix does not require redesigning the whole game.

- **Derive the UI from the game.** Carry its shape language, palette roles, edge treatment, materials, lighting and level of detail into frames, icons, typography and feedback. Matching colours alone is not enough. A screen-space overlay can belong just as well as an object in the world; do not force a literal in-world interface. Do not import Funke's Editor/Launcher skin or a generic web dashboard unless that is the requested direction.
- **Design for what the player needs to perceive.** For each important HUD value, identify the player decision it supports, then choose how to show amount, change, urgency or readiness at a glance. Use fitting icons, fills, segments, silhouettes, position, colour and restrained motion where they communicate more directly than a label and number. Keep exact numbers and short labels where precision or learning matters. Do not turn every value into a decorative instrument or assume one widget suits every game.
- **Give icons a connection to gameplay.** Reuse recognizable silhouettes and visual cues from the actual objects, resources or abilities they represent; simplify them for the displayed size. Maintain one icon family and consistent meaning across world, HUD and menus. Avoid unrelated stock symbols, emoji or differently styled images as finished assets. Pair unfamiliar icons with concise labels or hints; colour must not be the only signal.
- **Compose around the action.** Keep the playfield and important threats visible. Group related information, establish a clear priority, and place it where the player can glance without losing the action. Quiet persistent information can become more prominent when its state changes. Avoid boxing every number into its own card or surrounding the game with explanatory text.
- **Make feedback express state.** Changes in shape, fill, emphasis or short animation should correspond to real game events and remain understandable after the animation ends. Define applicable normal, critical, inactive, selected and interaction states in the same visual family. Read-only indicators should look like indicators, not draggable or clickable controls. Decoration and continuous motion must not compete with gameplay.
- **Keep creativity consistent with the brief.** A request for a normal, appealing game style permits deliberate, polished choices; it does not demand a special theme. Do not add an unrelated retro, futuristic, industrial or other strong treatment merely to make the UI distinctive. Flat, illustrated, dimensional and minimal interfaces can all work when they fit the game. Image-generated UI assets must use the same visual references as the game art; keep changing text and values as live UI, not baked into pictures.

Record the important HUD mappings and shared visual choices in `Design/DesignLanguage.md` (or the project's equivalent). Implement only the components needed for the requested game, using the built-in widgets below for behavior and `funke-game-art` for asset production.

### Keep unnecessary information out of the UI

Every persistent element needs a current player-facing purpose: a decision, an actionable state or essential navigation. Being available in code or mentioned in the brief is not a reason to display it. Leave space empty when no information is needed.

- Do not automatically add permanent keybind legends, control lists, instruction footers, descriptions of obvious actions, feature summaries or repeated explanations to the HUD or menus. Do not replace unnecessary text with an equally unnecessary row of icons.
- Teach unfamiliar actions with a brief contextual or first-use hint when needed; remove the hint once it has served its purpose. Put the complete control reference in an on-demand help/controls view when needed. A relevant interaction prompt may appear while its action is available. Honor explicitly requested persistent hints and accessibility needs, but do not use them as a reason to add a blanket instruction overlay by default.
- Keep debug values, internal state names, implementation notes and development status out of ordinary player screens. Put diagnostics in developer tools or an explicitly enabled debug overlay. Show scores, resources and other game information where they support play; do not display every tracked variable just because it exists.
- Preserve labels needed to understand unfamiliar icons, choices, costs or consequences. Prefer removing redundant information over shrinking it into tiny text or hiding essential meaning behind decoration.

### Review in the game scene

Inspect the affected UI **over actual gameplay**, at its final display size and supported aspect ratios, including busy backgrounds and important state changes. Check whether icons and indicators communicate without reading every label, whether exact information remains legible, and whether the composition feels like the same game as the artwork. Check menus and shops against that same family when they are in scope. For pixel art, inspect the aligned grid, outlines and thin details at final size. A kit contact sheet or attractive isolated mockup is not proof that the HUD fits in play. Fix mismatches before repeating the design across screens; state clearly if only a mockup could be inspected.

Also inspect ordinary play after first-use hints have disappeared. For each remaining label, icon and panel, identify what the player currently needs it for; remove redundant instructions, keybind lists and diagnostics. Confirm contextual prompts disappear when no longer relevant and essential information remains understandable.

## Layout: `RectTransform`

Every UI entity has a `RectTransform` (a `Component`). Screen-space UI needs no world transform. A world-space canvas root combines `RectTransform` with either `Transform2D` or `Transform3D`; its children use RectTransforms. Key members:

```csharp
Vector2 AnchorMin, AnchorMax;     // 0..1 of parent rect; positions the element (see the size note below)
Vector2 Pivot;                    // 0..1 local pivot
Vector2 AnchoredPosition;         // offset from the anchor
Vector2 SizeDelta;                // the element's absolute size (× scale)
Vector2 Position;                 // resolved position
Vector2 Size;                     // resolved size
float   Rotation, LocalRotation;
Vector2 Scale, LocalScale;
```
**Size comes only from `SizeDelta` (× scale); anchors never stretch.** A "stretch" anchor (`AnchorMin (0,0)`, `AnchorMax (1,1)`) with `SizeDelta (0,0)` is a zero-sized rect, not a full-screen one: children laid out relative to it collapse onto one point (`UILayoutSystem.cpp`: the parent's resolved size doesn't propagate). Give every container, panel and dim overlay an explicit size, e.g. the reference resolution `1920×1080` or larger for a dim layer — or set `"fillParent": true` (both axes) or `"fillWidth"`/`"fillHeight"` on the RectTransform in the scene JSON (scene-only; the C# `RectTransform` has no such properties), which makes the rect span its parent along that axis (the screen's safe area for a root). Root canvases and full-screen menus/HUDs with edge-anchored children should fill both axes, otherwise their corner buttons sit inside a 16:9 box on wide phones; a full-width top/bottom bar uses `fillWidth` plus `ignoreSafeArea`.

Anchors are the core idea: anchor a HUD element to a screen corner so it tracks resolution. The project's reference resolution and scaling come from `project.json` (`uiReferenceWidth/Height`, `uiScaleMode`, `uiScaleMatch`) — see `funke-project-structure`. Because canvases have a fixed reference size, a game that also ships to phones (2:1 and wider) or resizable windows should use `uiScaleMode: "Fit"`, otherwise corner elements of a 16:9 canvas get cut off. Top-level UI is laid out inside the safe area (notches, rounded corners); full-screen backgrounds and fades set `"ignoreSafeArea": true` (scene JSON only, like the fill keys).

**Layout groups** auto-arrange children: `HorizontalLayoutGroup`, `VerticalLayoutGroup`, `GridLayoutGroup` (`CellSize`, `Spacing`, `Constraint`, `ConstraintCount`, padding/alignment). Add one to a container entity and its child RectTransforms are positioned for you.

## World-space canvases

Add `Transform2D` or `Transform3D` to the canvas root alongside `RectTransform2D` in scene JSON. At scale 1, 100 UI units equal one world unit. The root's world transform positions and rotates the canvas; child layout stays in UI units. Do not add a world transform to every child. 3D canvases are depth-tested against opaque meshes, do not write depth, and render before scene post-processing/MSAA resolve; screen-space UI renders above them. Pointer events project through the camera onto the canvas. See `Github-Docs/Documentation/Engine-Core/World-Space-UI.md` for limitations and JSON examples.

## Display: `Image`

```csharp
Vector4 Color;                    // RGBA tint
Texture? Texture;                 // sprite/background; null = solid color
string  SpriteName;               // slice of Texture from its .meta; "" = whole texture
int     SortingOrder, SortingLayer;
ImageType Type;                   // Simple | Sliced (9-slice) | Tiled (edges + middle repeat)
Vector4 Border;                   // Sliced/Tiled: unstretched edges in texture px, X left, Y bottom, Z right, W top; all 0 = the texture's .meta border
float   BorderScale;              // UI units per border pixel (Tiled: also the tile size)
Material? Material;               // .material with a `@target UI` or `Sprite2D` shader; null = standard shader
```
Use `Image` for backgrounds, panels, icons. For rounded buttons and panels use **one** small rounded sprite with `Type = Sliced` and its corner radius as Border instead of generating a sprite per size (scene JSON: `"imageType": 1, "border": [24, 24, 24, 24]`). Better: store the border once with the texture (`funke sprite border Assets/Textures/Panel.png --border 24`, or `--slice <name>` for a sprite-sheet slice) and leave the Image's border at 0, so every Image using it slices the same way.

### Preserve texture shape when resizing

Before wiring a textured button, slider, bar or panel, decide which parts may stretch and which must keep their proportions. **Do not resize a complete decorated texture as a plain `Simple` image to fit arbitrary widget dimensions.**

- Use **9-slice** for resizable frames and tracks: choose borders from the actual artwork so corners, outlines, rounded end caps, bevels and shadows remain inside the protected regions. Only the intended edge/centre regions stretch. Use **Tiled** when repeating texture detail would otherwise smear; keep icons and unique decorations as separate, proportionally sized elements.
- Configure both the asset's border metadata (or an explicit Image border) **and** the rendered Image's `Sliced`/`Tiled` mode. A sprite-sheet crop only selects an image; it does not enable 9-slice resizing. Border metadata alone also does not change a `Simple` Image into a sliced one. Check the scene/prefab's actual Image settings, including child graphics and button state sprites.
- Build sliders from separate **track, fill and handle** visuals. Slice resizable track/fill artwork where appropriate; preserve the handle's aspect ratio and intended size instead of stretching the entire slider illustration. Keep the fill inside the track. At very small fill amounts, use a suitable clipped or plain inner fill rather than crushing decorative end caps; at zero, no residual fill should remain. Nine-slicing alone does not solve every fill shape.
- Choose `BorderScale` and a minimum widget size that preserve the intended border thickness. Account for the borders in the final texture's pixel units, including any whole-number asset upscale. Do not let opposing borders consume the entire widget and then rely on automatic shrinking to hide a bad layout. State textures must use compatible dimensions and slice geometry so the frame does not jump on hover or press.
- **Inspect the rendered variants:** narrow and wide buttons (including long labels), applicable interaction states, and sliders at zero, low, middle and full values, at supported UI scales. Look for stretched corners, oval handles, uneven outlines, smeared texture detail, seams and fill escaping the track. Confirm the actual result; the presence of slicing settings is not proof of correct rendering.

Asset preparation and scene keys: `funke-game-art` → `references/ui-art.md`.

**UI text:** labels use `Funke.TextRenderer` from the root `Funke` namespace (`using Funke;`). The same component renders world and UI text. Access it with `GetComponent<TextRenderer>()`; see `funke-game-scripting` for its properties.

## Interactivity: `Interactable` (the base)

`Interactable` is the low-level pointer layer most widgets build on. Subscribe to its events or poll its state:

```csharp
event Action<Interactable>? Hovered;
event Action<Interactable>? Clicked;
event Action<Interactable>? Pressed;     // continuous while held
event Action<Interactable>? MouseDown;   // rising edge
event Action<Interactable>? MouseUp;
event Action<Interactable>? FocusEnter, FocusExit;

bool IsInteractable { get; set; }
bool Focusable { get; set; }
bool IsHovered, IsClicked, IsPressed, IsMouseDown, IsMouseUp;   // poll-this-frame
bool IsFocused { get; set; }
```

## Widgets and their events

All widgets are `Component`s you `GetComponent<T>()` and wire up. The events are the contract:

| Widget | Events | Key properties |
|---|---|---|
| **`Button`** | `Clicked`, `ClickDown`, `ClickUp`, `Pressed` (all `Action<Button>`) | `TargetGraphic` (Entity), per-state sprites `NormalSprite/HoveredSprite/PressedSprite/DisabledSprite/FocusedSprite`, `WasClicked` |
| **`Toggle`** | `ValueChanged` (`Action<bool>`) | `IsOn`, `IsReadOnly`, `CheckmarkEntity`, `ValueChangedThisFrame` |
| **`Slider`** | `ValueChanged` (`Action<float>`) | `Value`, `MinValue`, `MaxValue`, `WholeNumbers`, `IsReadOnly`, `NormalizedValue`, `FillEntity`, `HandleEntity` |
| **`CircularSlider`** | `ValueChanged` | `Value`, `StartAngleDegrees`, `SweepDegrees`, `Clockwise`, `RingThickness`, `RingSegments` |
| **`InputField`** | `ValueChanged` (`Action<string>`), `Select`, `Deselect` | `Text`, `PlaceholderText`, `IsFocused`, `CharacterLimit`, `SubmittedThisFrame`, `TextEntity` |
| **`Dropdown`** | `SelectedIndexChange` | `SelectedIndex`, `IsOpen`, `AddOption/RemoveOption/SetOptions/ClearOptions()` |
| **`ScrollRect`** | `ValueChanged` | `Content`, `Viewport`, `Horizontal`, `Vertical`, `MovementType`, `Inertia`, scrollbar refs |
| **`Scrollbar`** | `ValueChanged` | `Value`, `Size`, `Direction`, `HandleEntity` |
| **`Mask`** | — | `ShowMaskGraphic` (clips children to this rect) |

For exact members beyond these, read `UIComponents.cs` — it's the authority and changes as UI matures.

## Wiring UI from a script

The usual pattern: an `EntityScript` on (or near) the UI element gets the component and subscribes in `OnStart`. **Unsubscribe in `OnDestroy`** to avoid dangling handlers across hot reloads / scene reloads.

```csharp
using Funke;
using Funke.UI;

public class PauseMenu : EntityScript
{
    private Button? m_Resume;
    private Slider? m_Volume;

    public override void OnStart()
    {
        m_Resume = Entity.FindByName("ResumeButton")?.GetComponent<Button>();
        m_Volume = Entity.FindByName("VolumeSlider")?.GetComponent<Slider>();

        if (m_Resume != null) m_Resume.Clicked += OnResume;
        if (m_Volume != null) m_Volume.ValueChanged += OnVolumeChanged;
    }

    private void OnResume(Button _)        => Time.TimeScale = 1f;   // unpause
    private void OnVolumeChanged(float v)  => Log.Info($"Volume = {v}");

    public override void OnDestroy()
    {
        if (m_Resume != null) m_Resume.Clicked -= OnResume;
        if (m_Volume != null) m_Volume.ValueChanged -= OnVolumeChanged;
    }
}
```

Polling instead of events is fine for simple cases: `if (button.WasClicked) …` / `if (toggle.ValueChangedThisFrame) …` in `OnUpdate`.

## Pointer queries: `UIPointer` (static)

To know what the mouse is over (only enabled `Interactable` entities are tracked):

```csharp
Entity? hovered = UIPointer.HoveredEntity;                 // front-most interactable under the pointer, or null
if (UIPointer.TryGetHoveredEntity(out Entity? e)) { … }
```
Useful for "is the cursor over UI?" gating so world clicks don't fire through the HUD.

## Recipes

- **Pause menu:** a UI scene loaded additively (`SceneManager.LoadScene("Pause", LoadSceneMode.Additive)`); set `Time.TimeScale = 0` to freeze gameplay while the menu's own `OnUpdate` still runs on unscaled time.
- **HUD that tracks resolution:** anchor each element to a screen corner via `RectTransform.AnchorMin/Max`; subscribe to `Window.Resized` only if you need manual relayout.
- **Settings screen:** `Slider.ValueChanged` → apply audio/graphics; persist with a `GlobalSystem` or Data Asset (`funke-assets-and-content`).
- **Don't click through UI:** before handling a world click, check `UIPointer.HoveredEntity == null`.
- **Modal popup (settings, pause, confirm):** root entity (disabled until opened) → a full-screen dim `Image` with an `Interactable` (`interactable: true`, `focusable: false`) and an explicit oversized `SizeDelta` → the panel and its widgets above it. Only `Interactable` entities hit-test, so a dim layer without one lets clicks fall through to the menu buttons behind it. Show the cursor while a popup is open and hide it again during gameplay (`Cursor` API, `funke-game-scripting`).
- **Translated UI:** one `Assets/Localization/<code>.json` per language (`{"meta": {"code": "de", "displayName": "Deutsch"}, "strings": {"menu.play": "Spielen"}}`; `en` is the fallback). Static labels get a `LocalizedText` component (`Key`) next to their `TextRenderer` and update on every switch; Button labels, Dropdown `Options` and InputField placeholders are set from code with `Localization.Get(key)` and refreshed in a `Localization.LanguageChanged` handler. A language picker lists `Localization.AvailableLanguages` and calls `Localization.SetLanguage(code)`, which also remembers the choice for the next start. Fonts render ASCII + Latin-1 only.
- **Slider fill inside a rounded frame:** the engine sizes `Fill` to the slider's own rect. Make the slider rect the *inner* track (inset from the artwork) and draw the background/frame as larger children, or the square fill pokes out at the ends.

## Pitfalls

- **Lists of buttons in a `ScrollRect` scroll by touch:** a press on a row button (anything but a Slider, Scrollbar, InputField or nested ScrollRect) becomes a scroll drag after ~14 units of movement and its click is cancelled, so phone lists can be built from plain `Button` rows. For long lists, author a fixed pool of rows and recycle them from the content offset (`Content` RectTransform `AnchoredPosition.Y`).
- **Always unsubscribe** event handlers in `OnDestroy` — handlers attached to widgets survive longer than you expect across scene/hot reloads.
- **UI events only fire in Play mode** (like all scripts).
- **`Interactable` must be enabled** for a widget to receive pointer events and to appear in `UIPointer`. An `Image` alone never blocks the pointer.
- **Missing `using Funke.UI;`** is the usual cause of "type `Slider`/`Toggle`/`Button` not found" compile errors.
- **Layout collapsed / everything in one spot:** a parent with stretch anchors and `SizeDelta (0,0)`. Give it an explicit size (see Layout).
- UI elements need a `RectTransform`, not a world `Transform2D`; mixing them up means layout/anchoring won't work.
- For inspector-exposed UI references on your scripts (e.g. `public Button? ResumeButton;`), see `funke-inspector-ruleset`.
