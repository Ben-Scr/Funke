# UI drawing, pointer focus and keyboard navigation

This describes the game's entity/component UI. Enable `Interactable.Focusable`
on participating controls (it defaults to false). Tab selects the next eligible
control; Shift+Tab selects the previous one, wrapping at either end. Enter or
Space activates the focused control. `Interactable.IsFocused` exposes selection.
In ColorSwap mode, focus uses `HoveredColor` unless `FocusedColor` has nonzero
alpha. In SpriteSwap mode, focus uses `FocusedSprite` when set, otherwise
`HoveredSprite`, then `NormalSprite` if both are unset. Disabled and pressed
states retain priority; None mode leaves visuals unchanged. Custom focus styles
remain optional, and no outline is added automatically.
Arrow keys also traverse controls, except while editing
an input field, where they move the caret, or adjusting a slider/open dropdown.

- Dropdowns: Enter/Space (gamepad A) opens the list. Up/Down previews an option;
  Home/End jumps to the first/last option. Enter/Space confirms, Escape (gamepad B)
  cancels without changing the selection or losing focus. Tab/Shift+Tab cancels
  the preview and moves focus. Moving the pointer returns the highlight to mouse
  hover. Empty/read-only dropdowns cannot be opened by keyboard.
- Sliders: Left/Right adjusts horizontal sliders; Up/Down adjusts vertical sliders.
  Reversed directions reverse the value change. Each press/deflection changes the
  value by 1% of the range, or 1 for whole-number sliders, clamped to the bounds.
  Home/End sets MinValue/MaxValue. Perpendicular arrows and Tab/Shift+Tab move focus.
  Enter/Space does not synthesize a pointer drag. Read-only sliders remain focusable
  but cannot be changed; an active pointer drag keeps control of the value.
- Circular sliders: Right/Up increases the value toward MaxValue; Left/Down
  decreases it toward MinValue. Clockwise and the arc angles only determine the
  handle's path. Steps, bounds, Home/End and read-only/drag protection match linear
  sliders; values do not wrap at the ring seam. Tab/Shift+Tab moves focus, while
  Enter/Space does not initiate a pointer drag. Ring focus is shown on the handle,
  including a handle with its own Interactable, using the hover fallback or custom
  focus style. Handle visuals and value-change events update even with the pointer
  outside the game surface.

Preview uses `OptionHoverColor`. Only confirmed dropdown selections and actual
slider changes enter the existing value-change event path; cancelling or reaching
a bound does not emit a change. Keyboard/gamepad input is ignored while another
editor panel owns game input or the developer console is open.

In the editor, Game View and its toolbar reserve navigation for the game:
Tab/Shift+Tab must not select editor controls while either has focus. Other
editor panels retain keyboard navigation, as do toolbar popups opened by mouse.

Pointer focus uses `UIEventSystem::PickInteractable`, the same picker as UI
events. Its canvas ordering, image/text sort order, masks, visibility and world
projection decide the frontmost interactive hit. Component insertion order does
not decide mouse focus. A non-focusable interactive overlay blocks pointer focus
on elements behind it; clicking it clears the previous keyboard focus.

Keyboard/gamepad traversal deliberately follows the shared `UIDrawOrder`
hierarchy traversal, filtered by `Focusable` and `Interactable`. Child order is
authored through the hierarchy; roots use the shared deterministic root ordering.
Changing image/text `SortOrder` affects the visual/pointer stack, not the tab
sequence. Put ordered controls under a common parent and arrange its children to
set that sequence. There is no separate persisted `TabIndex` field.

Disabled ancestors exclude their descendants from navigation and pointer hits.
When a target becomes disabled, non-interactable or non-focusable, the focus
system clears stale `Interactable.IsFocused` and `InputField.IsFocused` flags.
If multiple scripts request focus in one frame, the first eligible target in
the hierarchy traversal wins; registry component storage order is irrelevant.

Touch capture keeps its owner while the finger moves, but release clicks require
the current hit still to match that owner. A layout change moving the captured
element away therefore releases capture without clicking at its old position.

`UIHitTestTests`, `UIMultiTouchTests` and `UICanvasDrawOrderTests` cover hierarchy,
disabled parents, masks, multiple canvases, focus and capture. The GPU case
`GPU sprite buffer windows preserve alpha masks and all instances` includes the
visible repro: a green image with sort order 20 is drawn over red with sort order
10, despite reverse interactable insertion order; both pixels and the focus pick
must select green. Retained evidence is `gpu-final/windows/ui-focus-order-*`
under `artifacts/backlog-round8/`.

These checks exercise engine hit-testing and injected touch events on Windows;
they are not physical-device touch or mobile platform certification.
