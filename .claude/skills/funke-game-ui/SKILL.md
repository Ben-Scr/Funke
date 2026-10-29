---
name: funke-game-ui
description: Use when building GAME UI with the Funke engine — menus, HUDs, pause and settings screens, buttons, sliders, toggles, text input, dropdowns, scroll views, layout, and wiring their behavior from C#. Triggers on "UI", "menu", "HUD", "pause menu", "settings screen", "Button", "Slider", "Toggle", "InputField", "Dropdown", "ScrollRect", "Image", "RectTransform", "anchors / pivot", "fillParent / safe area", "HorizontalLayoutGroup / VerticalLayoutGroup / GridLayoutGroup", "Interactable", "Clicked / ValueChanged", "UIPointer", "click through UI", or work in `Funke-ScriptCore/Source/Funke/UI/` or `Funke-Engine/src/Systems/UIEventSystem.{hpp,cpp}`. Funke UI is component-based (RectTransform plus UI components on entities, namespace Funke.UI) with C# Action events you subscribe to; the source of truth is `UIComponents.cs` and `UIPointer.cs`. Script lifecycle is in `funke-game-scripting`, inspector fields in `funke-inspector-ruleset`, UI art in `funke-game-art`.
---

# Funke Game UI

Funke UI is **ECS UI**: a UI element is an entity carrying a `RectTransform` plus UI components (`Image`, `Button`, `Slider`, …). You lay it out in the Editor and drive behavior from C# by subscribing to each component's `event`s. UI widgets and layout types live in **`namespace Funke.UI`** (`using Funke.UI;`). The native side that turns pointer input into events is `Funke-Engine/src/Systems/UIEventSystem.{hpp,cpp}`. Source of truth: `Funke-ScriptCore/Source/Funke/UI/UIComponents.cs`.

## Layout: `RectTransform`

Every UI entity has a `RectTransform` (a `Component`) instead of relying on world `Transform2D`. Key members:

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
