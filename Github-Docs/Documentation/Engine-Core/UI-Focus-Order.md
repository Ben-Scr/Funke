# UI drawing, pointer focus and keyboard navigation

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
