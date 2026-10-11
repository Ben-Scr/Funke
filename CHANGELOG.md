# Changelog

Release notes describe user-visible compatibility, reliability, and workflow changes. A public tag is accepted only when its exact version has a section here.

## [2026.1.0-beta.1]

First public beta. Windows x64 only; see `Github-Docs/Documentation/Known-Limitations.md`.

### New

- **Triggers:** colliders with **Sensor** enabled now raise `OnTriggerEnter2D`, `OnTriggerStay2D` and `OnTriggerExit2D` (previously sensor overlaps were never reported). `Collision2D.IsTrigger` tells the two apart.
- **Saving data:** `PlayerPrefs` (typed key/value settings) and `SaveData` (any object as JSON, per named slot). Both write atomically and save automatically on quit and when Play stops.
- **Gamepads from C#:** `Input.IsGamepadConnected`, `GetGamepadButton/Down/Up`, `GetGamepadAxis`, `GetLeftStick`, `GetRightStick`, `GetGamepadTrigger`, plus `GamepadButton` / `GamepadAxis`. `Input.GetAxis()` falls back to the first gamepad's stick or D-pad.
- **Collision layers:** every collider has **Layer** and **Collides With** checkbox fields (16 layers in the Inspector, 64 from code). Raycasts and overlap queries accept a `layerMask`; `PhysicsLayers.Mask(...)` builds one.
- **Collider settings from C#:** `IsSensor`, `Layer`, `CollisionMask`, `Friction` and `Bounciness` on `BoxCollider2D`, `CircleCollider2D` and `PolygonCollider2D` (shared `Collider2D` base class).
- **Sprite animation:** the **Sprite Animator 2D** component flips through sprite-sheet slices at a set frame rate (loop, play on start). Scripts control it with `SpriteAnimator2D.Play/Pause/Stop`, `Frame` and `FramesPerSecond`.
- **Undo/redo for structural edits:** create, delete, duplicate, paste, cut, reparent, reorder, add/remove component or script, rename, enable/disable, Static and prefab unpack. The selection survives undo/redo.
- **Crash recovery:** unsaved scenes are copied to `.index/Recovery` every 30 seconds and on an unexpected error; the next start offers Restore or Discard.
- **Starter project:** new projects contain a small playable example (Player, Coin, `PlayerController.cs`, `Coin.cs`).
- **Launcher prerequisite check:** missing .NET 10 SDK or MSVC Build Tools are shown with a download button.
- Documentation: *Your First Game* walkthrough and *Known Limitations* page.

### Changed

- **Index is now open source under the MIT License.** It can be used, modified and redistributed freely, including in commercial games; the previous custom license with a mandatory visible credit no longer applies.

- **`Application.PersistentDataPath` and `TemporaryCachePath` are per game:** `%LOCALAPPDATA%\<Company>\<Name>` instead of a folder shared by every Index game. Data written to the old `%LOCALAPPDATA%\Index` folder is not moved.
- **Play no longer saves scenes.** Unsaved edits are snapshotted privately and are still unsaved after Stop.
- The collider **Layer Mask** number field is now a **Layer** checkbox dropdown.

### Fixed

- An exception during a frame used to close the editor without warning; it now keeps unsaved scenes for recovery and explains what happened.
- A last-opened scene that cannot be read no longer leaves the editor with no scene; an empty unsaved scene opens and the file is left untouched.
- Exceptions in UI event handlers, coroutines and several other script entry points no longer terminate the process; they are logged to the Console.
- Errors from `OnApplicationQuit`, `OnApplicationPaused` and `OnFocusChanged` handlers now appear in the Console instead of being lost.
- Crash dumps are also written for `std::terminate`, `abort()`, pure virtual calls and invalid CRT parameters.
- A game started without the .NET 10 Runtime shows a message with a download link.
- `Gizmo.DrawLine(start, direction, distance)` no longer throws `NotImplementedException`.
- Release reliability, packaging, and data-safety fixes.
