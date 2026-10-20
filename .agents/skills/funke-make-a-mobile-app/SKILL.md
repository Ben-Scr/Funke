---
name: funke-make-a-mobile-app
description: Use when building a MOBILE APP (not a game) with the Funke engine — an Android/iOS phone app of screens, lists, tabs, forms and settings. Triggers on "make a music player / notes app / to-do app in Funke", "build an Android app", "add a settings page / tab bar / bottom sheet", "the list doesn't scroll on the phone", "UI too small on the sides", "install the APK on my phone", "the app crashes on the phone". Covers the workflow (generated JSON scene → C# scripts → APK → device test), the architecture that works (one scene, Router with tabs/pages/overlays, pooled virtual lists), portrait layout (width-driven scale, safe area), polish (rounded and 9-sliced images, icons, cover art), platform features (permissions, background audio, keyboard, back button) and device profiling with scripts/device.py. Ships ui_gen.py, C# UI templates and an example app. Games use funke-make-a-game; widgets funke-game-ui; C# API funke-game-scripting; UI art funke-game-art.
---

# Making a Mobile App with Funke

Funke is a 2D game engine, but its ECS UI (RectTransform + Image/TextRenderer/Button/ScrollRect/InputField…) plus C# scripting is enough for real phone apps. This skill is the playbook distilled from building a Spotify-style music player end to end: what to generate, which architecture survives, and the traps that only show up on a device.

**You build the app like `funke-make-a-game` builds a game: write the scene as JSON with a generator script, keep C# for behaviour, verify by running.** Never hand-place hundreds of UI entities in the Editor.

## Workflow

1. **Create the project**: `bin\Release-windows-x86_64\Funke-CLI\funke.exe create <Name> --path <dir>`. In this engine repo the user's apps go to `C:\Users\benso\Index\Projects`; agent test projects go under `bin-int\_agent\` with `--no-register` (without `--path`: `%USERPROFILE%\Funke\Projects`). Delete the starter `SampleScene.scene` and `GameScript.cs`.
2. **Copy the kit** from this skill's `templates/`:
   - `ui_gen.py` → `<project>/Tools/` (scene kit), `gen_example.py` as the starting point for `Tools/gen_scene.py`.
   - `Scripts/UI/*.cs` → `<project>/Assets/Scripts/UI/` (Ui, Label, IBackHandler, FitHeight, Router, VirtualList, ActionSheet, InputDialog, Toast).
   - `Scripts/Example/Notes.cs` only as reference; it is the example app's logic.
3. **Fonts**: put OFL fonts in `Assets/Fonts/` and set `ui.FONT_REGULAR` / `ui.FONT_BOLD = ui.meta("Fonts/…")` in the generator. Inter Regular/SemiBold/Bold are on this machine under `C:\Users\benso\source\repos\External\Avalonia\src\Avalonia.Fonts.Inter\Assets\` (OFL; also ship the license text, outside `Assets/`). Bold weights matter for modern app looks.
4. **Write the generator** (`Tools/gen_scene.py`): theme colours → `ui.camera` → `ui.root_canvas` (Router + app scripts) → one `ui.page` per tab/page → `ui.tab_bar` → overlays (`action_sheet`, `input_dialog`, `toast`) → `s.save()` → `ui.mobile_project_settings(scene, android_options)`. Run it after every layout change; GUIDs and entity uuids are stable.
5. **Scripts**: one small script per page/overlay, data in plain static classes or services (see Architecture).
6. **Compile**: `dotnet build <Name>.csproj -c Release`. The project compiles against `<project>/Packages/Funke-ScriptCore/`, which the editor, the runtime and every `funke` command refresh (dll + pdb) from the engine build when they load the project. After changing engine C# APIs, rebuild ScriptCore and load the project once (e.g. `funke info`) before a plain `dotnet build`.
7. **Desktop check**: `scripts\dev\Run.ps1 -App Runtime -Project <dir> -Seconds 4 -Shots 1` (portrait 540×1000 window). Add env-var test hooks (`<APP>_SCREEN=settings` opens a page, `<APP>_FOLDER=…` points at test data) so every screen can be screenshotted without clicking. With the project open in an editor, `funke editor play --size 1080x2400` lays the app out for a phone screen; then `funke editor screenshot shot.png`, `funke editor tap 540 1800 --px`, `funke editor drag 0.5 0.7 0.5 0.3` (scrolls lists), `funke editor key Esc` (back) — no rebuild, no window focus (*Editor > Editor Bridge*).
8. **APK**: `funke.exe build --platform Android --release` from the project folder (needs the `Funke.Platform.Android` package and an SDK/NDK; output `Builds/Android/<Name>.apk`). On failure rerun with `--verbose`; the real compiler error is in the `Build log:` file it names.
9. **Device**: `python <skill>/scripts/device.py install|launch|shot|tap|mem|log|crash|heap …` (see Device testing). Ask before touching the user's phone.

## Project settings (done by `mobile_project_settings`)

- `mobileOrientation: Portrait`, `gameViewAspect: 9:16`, desktop window 540×1000.
- **`uiScaleMode: MatchWidthOrHeight`, `uiScaleMatch: 0`** (width-driven). The canvas is always 1080 units wide, edge to edge; height varies per phone. `Fit` shrinks the whole UI on phones whose safe area is shorter than the reference height — the UI then floats with side margins.
- `showRuntimeStats/showRuntimeLogs: false` — those overlays are debug layers, and they are what F6/F7 toggle.
- Android profile `platformOptions`: `permissions` ("READ_MEDIA_AUDIO, READ_EXTERNAL_STORAGE:32" — bare names get `android.permission.`, `:N` = maxSdkVersion) and `backgroundAudio: "true"` (declares the foreground media service). Without a keystore the APK is debug-signed (fine for sideloading).
- The project has its own copy of `FunkeAssets/` (splash icon, fonts, localization) made at `funke create`. After engine branding changes, diff it against `Funke-Runtime/FunkeAssets` and copy the newer files, or the APK keeps the old splash logo.

## Layout rules

- **Sizes come only from `SizeDelta`** — anchors position, they never stretch. `fillWidth` / `fillHeight` / `fillParent` span the parent; use them for pages, bars and full-width rows.
- The root canvas (`fillParent`) is the **safe area**. Backgrounds and gradients that should reach under the status/navigation bar set `ignoreSafeArea` (and are oversized, e.g. 6000×6000).
- `top(x, y, w, h)` hangs a rect from the parent's top edge (y ≤ 0 goes down); `bottom(...)` stands it on the bottom edge (tab bar, mini player, toasts).
- **Vertical stretch**: a list from y=Top down to the tab bar gets `FitHeight(Top, Bottom)`, which sets its height from the canvas each frame. Anything else that depends on the screen height must be computed the same way (`Ui.CanvasHeight`).
- Reserve bottom space for the tab bar (~156) and anything docked above it (mini player ~136); hide/show docked bars and grow lists accordingly.
- Sorting-order bands keep layering sane: background 0, pages 1–99, docked bars 110–130, full-screen overlays 200+, sheets 300+, dialogs 340+, toasts 400.

## Architecture that works

- **One scene.** `Canvas` holds `Router` (+ an app `Theme` script for swap-in textures). Tabs are `Canvas/Pages/<Tab>`; pushed pages (detail, settings) are siblings there; overlays (`NowPlaying`, `ActionSheet`, `InputDialog`) sit directly under `Canvas` above the pages.
- **Router**: `SelectTab(i)` (tab buttons bind it with an int arg), `Push(page)` / `Pop()` — the covered page is disabled so it cannot catch taps — and `Back()` on Escape (= Android back): overlays first (`IBackHandler`), then the page stack, then the first tab.
- **Everything starts enabled in the scene**; the Router/overlay scripts hide themselves in `OnStart`. A page that starts disabled may not have run `OnAwake`/`OnStart` when you first call into it. Set `Instance` in `OnAwake`, and let a page rebuild once more at the end of its own `OnStart` in case it was told to open earlier.
- **Enable order doesn't matter.** A child set to `IsEnabled = true` while its parent is off remembers it and turns on with the parent (like Unity's `activeSelf`), so rows can be filled before or after showing the sheet.
- **Find parts by path** (`Ui.Child(Entity, "Header/Title")`) instead of wiring dozens of entity fields; wire only textures as script fields (`"texture"` type with the asset GUID).
- **`Label`** caches text and colour; setting `TextRenderer.Text` every frame re-lays out text and crosses the interop boundary for nothing.
- **Only the components an entity needs.** Non-visual holders (an `AudioManager`, a background service, a data/state holder) get no `RectTransform`/`Transform2D`, and no `NameComponent` unless something finds them by name: omit `"name"` and the transform in the scene JSON. Keep names on UI parts you look up by path (`Ui.Child`).
- **Own components for shared data.** Per-row or per-item data that several scripts read (`TrackRow { int Index; }`, `Favourite`) fits a custom C# component: a class deriving `Component` (Inspector-editable, strings/references allowed, no lifecycle) or a blittable `IComponent` struct for many entities. Both are addable in the editor and via `AddComponent<T>`/`GetComponent<T>` (class) or `AddNativeComponent<T>`/`GetNativeComponent<T>` (struct); file name = type name. Details and limits: `funke-make-a-game` → `references/architecture.md` (*Custom C# components*).
- Data lives in static stores/services (`Library`, `Playlists`, `NoteStore`) with a `Changed` event; pages subscribe in `OnStart`, unsubscribe in `OnDestroy`, and rebuild lazily (`m_Dirty`).
- Work off the main thread with `Task.Run` (file scans, tag parsing, image decoding) and hand results back through a `ConcurrentQueue` drained in `OnUpdate`. Never call engine APIs from a worker — `Texture.DecodeImage` is the documented exception.

## Lists

- **`VirtualList<T>`**: author a pool of ~20 rows (`Content/Row0..`) per list; the script recycles them for the visible range and sizes `Content` (`HeaderHeight + count × RowHeight`). Thousands of items cost one screen of entities. A header (big cover, title, buttons) placed in `Content` above the rows scrolls away with the list.
- Rows are Buttons calling `OnRowClicked(slot)`; secondary row buttons ("…") pass their slot too and use `TryGetSlot`.
- **ScrollRect viewport must be set explicitly** (`"viewport": <its own uuid>`); left unset, the engine takes the first child — the content — and clamps scrolling to zero.
- Buttons inside a ScrollRect still scroll by touch: a press becomes a drag after ~14 units and the button click is cancelled (engine behaviour). Sliders, input fields and nested scroll rects keep their own press.
- Horizontal carousels: same pieces with `horizontal=True`, content anchored left, width = `left padding + count × stride`.
- Text has no auto-ellipsis: `Ui.Ellipsize(text, maxChars)` per label.

## Visual polish

- **Rounded rectangles**: one small rounded sprite as a 9-sliced Image (`"imageType": 1` = Sliced, `"border": [l, b, r, t]` in texture pixels, `"borderScale"`) covers every size; corners keep their radius while the middle stretches. `ui.rr(w, h, r)` still makes an exact-size texture (cached by size) when you want one per size. Pills: `r = h/2`. UI art in general: `funke-game-art`.
- **Icons**: white 128² PNGs drawn at 4× and downsampled (`ui.icon(name, draw)`), tinted by `Image` colour. Draw filled icons as **one closed shape** and cut holes with transparent fills — overlapping strokes leave visible seams.
- **Dark theme tokens** (Spotify-like): background `#121212`, surface `#282828`, tile `#2A2A2A`, text white, secondary `#B3B3B3`, accent `#1ED760`; pressed rows `white @ 8%`.
- **Gradients**: a 4×256 alpha ramp (`fade_tex`) tinted per page (cover colour for detail pages, black behind the tab bar).
- **Images from files/bytes**: `Texture.DecodeImage(bytes, maxSize, out w, out h)` (worker-safe) + `Texture.Create(w, h, pixels)` on the main thread. Keep an LRU of textures and `Dispose()` evicted ones (GPU memory isn't GC'd). The average colour of the decoded pixels makes a good tint.
- Placeholders: grey square + icon until art arrives, so rows never jump.

## Platform features

| Need | API |
|---|---|
| Read shared files (music, downloads) | Manifest via `permissions` option; `Application.RequestPermission("android.permission.READ_MEDIA_AUDIO")`, poll `GetPermissionState`. **If already granted, the state is `Granted` right after the request call** — check it immediately, or the UI waits forever. Fall back to `READ_EXTERNAL_STORAGE` when the first one is `Denied` (Android ≤ 12). Use `#if FUNKE_ANDROID`. |
| Play files outside the project | `AudioStream.Open(path)` (MP3/WAV/FLAC, streamed): `Play/Pause/Stop`, `Time` (seek), `Length`, `IsAtEnd`, `Volume`, `Loop`; `Dispose()`. |
| Keep playing in the background | `Audio.PlayInBackground = true` while playing, false when paused; profile option `backgroundAudio`; request `android.permission.POST_NOTIFICATIONS` (Android 13+) on first play so the notification is visible. Scripts keep ticking at ~10 Hz in the background (auto-advance works). |
| Notification / lock-screen / headset controls (music apps) | `MediaSession.SetNowPlaying(title, artist, album, length, coverRgba, w, h)` on track change, `SetPlaybackState(playing, position)` on play/pause/seek, drain `MediaSession.TryGetCommand` every frame. Keep `Audio.PlayInBackground` on while a track is loaded (also paused), or the notification's play button can't reach a backgrounded app. Decode the cover on a worker (`TagReader`-style bytes → `Texture.DecodeImage(…, 512, …)`). |
| Persist settings / data | `PlayerPrefs` (small values), `SaveData.Save/TryLoad(slot, obj)` (JSON, atomic). Caches you rebuild yourself (e.g. a scanned library) as files in `Application.PersistentDataPath`. |
| Text input | `InputField`; focusing it opens the soft keyboard, tapping anywhere else closes it; `SubmittedThisFrame` for Enter. Long-press selects a word and shows Android's Cut/Copy/Paste/Select-all toolbar (engine-side, nothing to wire). |
| Back button | Arrives as `KeyCode.Escape`; route through `Router.Back()`. |

## Device testing (`scripts/device.py`)

```
python device.py devices | install <apk> | launch <pkg> [--fresh] | shot out.png | tap X Y | key back
python device.py mem <pkg> [--seconds 30]      # Native Heap / Graphics / PSS over time
python device.py log <pkg> [--grep scan]       # app logcat (Funke logs carry tag "Funke")
python device.py crash                         # crash buffer: OOM aborts, native crashes
python device.py heap <pkg> <unstripped libmain.so>   # heapprofd: live native allocations by call stack
```

- **Ask before using the user's phone**, and look at the status bar first — if they're on a call, don't start audio.
- `tap` presses for 120 ms; `adb shell input tap` presses and releases within one engine frame and Funke never sees the click.
- Coordinates are device pixels; read them off a screenshot (Read the PNG, scale by its original size).
- Wireless debugging drops when the screen locks or the network changes; if `devices` is empty, ask the user to re-enable it or plug in USB.
- **Always watch memory for ~30 s** after launching a new build (`mem`). Native Heap growing steadily means a leak that will kill the app within a minute. The desktop rarely reproduces device-only leaks.
- `heap` needs a debuggable APK: temporarily add `android:debuggable="true"` to `platforms/Funke.Platform.Android/Template/AndroidManifest.xml`, rebuild, profile, then remove it again. The unstripped `libmain.so` is in `%LOCALAPPDATA%\Funke\PlatformCache\Android\arm64-v8a\player-<key>-Release-<modules>\` (e.g. `player-0c334137a2-Release-2d-3d`).
- Git Bash rewrites `/data/...` arguments into Windows paths; device.py avoids that (or prefix commands with `MSYS_NO_PATHCONV=1`).

## Pitfalls seen in practice

- **UI narrower than the screen on phones** → `Fit` scale and/or safe-area side insets. Use width matching; the engine's Android activity reports only bars and cutouts as insets (gesture zones are not obstructions).
- **App killed after ~20 s on Mali GPUs** → driver leak under Vulkan dynamic rendering; the engine disables it on Android. If a similar climb shows up, profile with `heap` before guessing.
- **Desktop screenshots show someone else's app** → other sessions share `bin-int\_agent\run\`; check the window title in the PNG.
- **`dotnet build -p:DefineConstants=FUNKE_ANDROID` poisons later desktop builds** (shared `obj/`); compile-check Android code with `-o <temp dir>` and rebuild desktop with `--no-incremental`.
- **New `.cs` file in `Funke-ScriptCore` silently missing from the DLL** → the csproj lists sources explicitly (`<Compile Include=…>`); add the file there.
- **Engine C# API added but the project doesn't see it** → rebuild ScriptCore, then load the project in the editor or any `funke` command so `Packages/Funke-ScriptCore` is refreshed; "interop mismatch" in the log means engine and ScriptCore were built from different binding layouts — rebuild both.
- **Android-only compile errors** (e.g. `std::from_chars` for floats is deleted in the NDK's libc++) only appear in the APK build's log file.
- Fonts render missing glyphs (CJK, emoji) as boxes; fine for titles from user files, but don't use such glyphs in UI strings.

## Related skills

`funke-game-ui` (widget members, events), `funke-game-scripting` (C# API), `funke-build-and-ship` (profiles, signing, store builds), `funke-make-a-game` (generator house rules), `funke-game-art` (icons, UI art, 9-slice frames, fonts, palettes), `funke-debug` (logs, crash dumps), `funke-engine-scripting` (adding a C# binding when an app needs a new engine API).
