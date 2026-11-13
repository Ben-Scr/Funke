# Editor

The **Funke Editor** is the visual tool where you build your game: arrange entities, edit their components, design scenes, test in Play mode, and build a finished game. This page is a tour of the editor and its core workflows.

If you just want the short version: the editor is a separate app that opens your project. You build scenes in the **Hierarchy** and **Viewport**, edit selected objects in the **Inspector**, manage files in the **Asset Browser**, and press **Play** to test.

Progress windows display **Elapsed: MM:SS** (or **HH:MM:SS** after an hour) beneath the progress bar and stage. This applies to builds, script compilation, package operations, asset conversion, imports, deletions, and asynchronous scene opening. The timer continues through stage changes and resets when a new operation's window opens. Windows uses a native progress window; Linux and macOS show the same timing information in the editor overlay.

---

## Editor, engine, and runtime

It helps to know how the pieces relate:

- The **Editor** (`Funke-Editor`) is an authoring tool. It embeds the engine and adds visual tools on top. It does *not* ship with your game.
- The **Runtime** (`Funke-Runtime`) is the slim executable that *runs* your finished game using the scenes and assets you authored. See [Startup Processes](../Engine-Core/Startup-Processes.md).
- The **Launcher** (`Funke-Launcher`) is the small app you use to **create** and **open** projects.

So the flow is: **Launcher → create/open a project → Editor → build your game → Runtime runs it.**

---

## Opening a project

You create and open projects from the **Launcher**:

- **New Project** — pick a template, a name and a location; the launcher copies the template's scene and scripts, sets up the C# script project and compiles the scripts once.

Set **Launcher > Settings > Projects > Default company** once to prefill **Company** whenever you create a project. The default is saved per user and survives launcher restarts. You can change or clear Company in **New Project** without changing the default. The dialog's value is saved as the project's `buildCompany`, including when using a custom template, and can later be changed in the editor's **Project Settings**. Changing the default does not modify existing or duplicated projects.

### Project templates

| Template | Contents |
|---|---|
| **Empty 2D** | A Camera 2D. |
| **Empty 3D** | A Camera 3D and a Directional Light. |

A template is a folder in `templates/` (inside `EngineSDK/` for the packaged Launcher):

```
templates/MyStudio.Platformer/
  template.json
  preview.png          (optional)
  Assets/Scenes/SampleScene.scene
  Assets/Scripts/*.cs
```

```json
{
  "id": "MyStudio.Platformer",
  "displayName": "Platformer",
  "description": "Shown on the template card.",
  "category": "2D",
  "sortOrder": 50,
  "modules": [ "Funke.2D" ],
  "startupScene": "SampleScene"
}
```

Everything under `Assets/` is copied into the new project. Scenes are written as JSON and converted to the project's asset format. In `.cs`, `.json`, `.md`, `.txt`, `.meta` and `.dataasset` files, `{{ProjectName}}` becomes the project name and `{{ProjectIdentifier}}` a C#-safe version of it. `modules` is written to the project's `project.json` and selects the project's [engine modules](../Engine-Core/Engine-Modules.md); a project with only `"Funke.3D"` opens the Editor View in 3D mode ([3D](../Engine-Core/3D.md)). Each category in use becomes a filter tab in the dialog.

A template may also contain:

| Entry | Effect on new projects |
|---|---|
| `project.json` | Project settings (window and build settings, UI scaling, splash screen, graphics, physics rate, defines, packages, build scene list, asset format, ...). Name, location and engine version come from the new project; `lastOpenedScene` is reset to the startup scene and an `executableName` equal to the old project name is dropped. |
| `BuildProfiles/` | Replaces the default `Windows` / `Linux` / `macOS` build profiles. |
| `NativeScripts/Source/` | Native C++ scripts. |
| `Packages/<Name>/` | Project-local packages. |

#### Your own templates

Right-click a project in the Launcher and choose **Save as Template...**. Enter a name, an optional description and a category (prefilled with `2D` or `3D`). The Launcher copies the project's `Assets/` (folder structure, scenes, scripts, `.meta` files and all other assets), its `project.json`, `BuildProfiles/`, `NativeScripts/Source/`, project-local packages and the editor preview image (as `preview.png`) into `%LOCALAPPDATA%\Funke\Templates\<Name>\` and writes a `template.json` with the id `User.<Name>`. Build output (`bin`, `obj`, `build`) and `.git` are skipped. The template reflects what is saved on disk, so save the project in the editor first.

The template then appears in **New Project** with a **Custom** chip. Right-click its card to show its folder or delete it; deleting a template does not affect projects created from it. **Open templates folder** in the dialog opens `%LOCALAPPDATA%\Funke\Templates`; folders copied in there by hand are picked up the next time the dialog opens.

#### The Templates page

**Templates** in the Launcher sidebar lists custom templates and the built-in ones, each with its `preview.png` (or a colored tile). Search and the category chips filter the list. **From Project** saves one of your projects as a template; **New Project** on a card (or a double-click) opens the create dialog with that template selected.

The `...` menu (or a right-click) of a custom template offers:

| Action | What it does |
|---|---|
| **Edit...** | Changes name, description, category and the source project. The content stays as it is. |
| **Open Source Project** | Opens the project the template was saved from in the editor. |
| **Update from Source Project...** | Replaces the template's content (assets, project settings, build profiles, native scripts, local packages, preview) with the source project's current state on disk. Id, name, description and category stay. |
| **Show in Explorer** / **Delete Template** | As on the cards in the create dialog. |

To change what a template contains: **Open Source Project**, edit and save the project, then **Update from Source Project...**. The source project is stored as `sourceProjectPath` in `template.json`; if the project has moved, set the new folder under **Edit...**.
- **Open** — pick an existing project; the launcher prepares it and opens the editor.

Each project tile shows a preview image of the open scene, which the editor writes to `.funke/thumbnail.png` shortly after the project opens and on every save. It is rendered through the scene's Camera 3D if it has one (with sprites and UI on top), otherwise through its main Camera 2D. A project without either keeps the colored tile with its initials.

A project is a folder with an `project.json` (its settings: scenes, window size, packages, build options) plus folders for your `Assets`, scripts, and scenes.

---

## The panels

The editor is a set of dockable panels. The main ones:

| Panel | What it's for |
|-------|---------------|
| **Hierarchy** (Entities) | The tree of all entities in the loaded scene(s). Create, rename, parent, reorder, and delete entities here. |
| **Inspector** | Shows and edits the components of whatever you've selected. This is where most editing happens. |
| **Editor View** (Viewport) | The interactive 2D or 3D workspace, according to the project's modules. Move the camera, select objects, and drag gizmos to position them. |
| **Game View** | A preview of the game exactly as the player will see it (active during Play mode). The aspect dropdown picks a fixed ratio, Free Aspect, or **Project (…)**, which follows the build Aspect Ratio from Project Settings. |
| **Asset Browser** | Your project's files — sprites, scenes, prefabs, scripts. Create assets, drag them into scenes, slice sprites. See [Assets](../Assets/Assets.md). |
| **Console** | Engine and script log output (info, warnings, errors). |
| **Profiler** | Real-time performance timings per subsystem. |
| **Package Manager** | Browse and install [packages](../Packages/Writing-Packages.md). |
| **Project Settings** | Display, graphics, branding (icon/splash), build, and system settings. |
| **Build** | Configure and produce a runnable game. |

A **toolbar** at the top has the playback controls (Play / Pause / Step / Stop), and a **menu bar** has Save, project tools, preferences, and more.

The **Log** panel opens an available source location at every displayed severity, including Trace and Info. Double-click the row or select it and click **Open source file** in its details; both use the configured external editor and the same file/line. Expanded details show **No source location available** when the source is absent, missing locally, or malformed. Such messages remain selectable and copyable. The existing Core-log display filter is unchanged.

Normal C# `Log` string/object overloads and script `Print`, native logging macros, and C++ script `Print`/`FNK_NATIVE_LOG_*` carry the caller's file and line. Exceptions keep the actual stack-trace throw site. Structured locations take precedence; compiler diagnostics and older text-only output use the text parser. Supported forms include Windows drive paths, quoted paths, project-relative paths with spaces, `file.cs(12,3)` and exception `file.cs:line 12`. Missing files are never replaced by a recursive same-filename match. Relative paths resolve against the working, project, script and asset directories.

**Collapse** merges consecutive matching messages only when severity, source identity and line also match. Owned source strings remain valid after managed or native script reload. Source resolution happens when the message enters the panel, with a bounded cache (1,024 paths, two-second expiry and project-change invalidation); drawing rows does not repeat filesystem searches. A message whose source appears later needs to be emitted again. Windows Release verification covered actual VS Code navigation, failed compiler diagnostics, background-thread logs, Collapse, and successful reload while playing. Other IDE/platform launchers were not re-certified by that check.

The **Layout** dropdown at the top right switches between saved layout presets. **Default** restores the standard arrangement: Hierarchy on the left, Inspector on the right, Editor View and Game View in the middle, Asset Browser and Console below. **Manage layouts...** opens Editor Preferences › Layouts, where you save the current arrangement as a preset or delete presets. The editor keeps your layout in its user data folder (`%LOCALAPPDATA%\Funke\Editor\imgui.ini` on Windows) and falls back to the default when that file docks none of the main panels.

Layout presets appear as responsive cards with miniatures of their saved docking arrangement. **Load** restores a preset; **Active** marks the last preset saved or loaded in this editor session and can be clicked to restore it again. The trash button opens a deletion confirmation. **Open in Explorer** (Windows), **Open in Finder** (macOS), or **Open in File Manager** (Linux) opens the preset folder, creating it if needed. Hover over the button to see the folder path. Right-click the **Layouts** tab and choose the circular-arrow icon to restore the default arrangement while keeping saved presets intact. Miniatures show docked panels, not scene contents or floating windows; presets without readable docking data remain loadable and show **Preview unavailable**.

**Save current layout** is the primary action. Presets use `.ini` files in the editor's `Layouts` user-data folder. Names must contain 1–64 bytes, exclude filename separators and reserved characters, and cannot end in a space or dot. Saving checks the atomic write result; a failed overwrite preserves the existing preset and keeps the entered name and dialog available for retry. Failed loading or deletion also shows an error. Loading applies the saved docking state at the start of the next frame. Long names wrap within the list, keeping their actions reachable in narrow windows.

Presets restore **tab groups**, their tab order and selected tab, and which tool windows are open. For example, open Project Settings and drag its title/tab onto the **centre docking target** of Inspector to make them clickable tabs sharing the same area. Animation can share a tab group with Project Assets in the same way. Dropping on an edge creates a split instead. Save the layout after arranging the tabs; loading it reopens the saved tools and closes tools saved as closed. This also applies to custom C# editor windows and to the layout restored at editor startup. Asset selections and tool contents are not part of a layout. Older presets without open-window state retain the current tool visibility; open the desired tools and save the preset again to include it.

**Editor Preferences › Behavior › UI Editing** controls UI alignment guides and the snap distance shown while guides are enabled. Search for `UI Editing`, `alignment`, or `snap` to find it. The **Editor Camera** category configures the **3D editor camera**, including orbit/fly navigation, movement, zoom, and FOV; its tab context menu restores its defaults. Search accepts `editor camera`, `camera`, `orbit`, `fly`, and `fov`. Both groups persist across editor restarts; their existing `AlignmentGuidesEnabled`, `AlignmentSnapThreshold`, and `Camera3D` settings remain compatible.

**Editor Preferences > Gizmos > Gizmo Colors** customizes the color and opacity of built-in 2D physics colliders (box, circle, polygon), particle emitters and their direction indicators, camera frames, sprite outlines and halos, UI rect outlines, and text margins. Changes apply immediately and persist across projects and editor restarts in `EditorPreferences.json` under `GizmoColors`, independently of the editor theme. Existing preferences retain the previous default colors. Script and package gizmos retain their authored colors. Search for `gizmo`, `collider`, `particle`, or the corresponding overlay name to find these settings.

Preferences search covers all categories. Clearing it restores category navigation and its separate scroll areas. The tab bar omits the redundant tab-list popup; overflow arrows still expose every category when the window is narrow or text is enlarged.

Right-click any preferences tab, then click the circular-arrow icon in its context menu to restore only that tab's defaults. The menu names the affected tab, and the icon's tooltip explains the action. Individual settings have no reset buttons. Clear an active search to access the tabs.

| Tab | Restored defaults |
|---|---|
| Gizmos | Default colors and opacity for all built-in component gizmos listed above. |
| Appearance | System theme, default custom palette, bundled font, 100% text scale, and default hierarchy row size. A font change requests an editor restart. |
| Scripting | First detected external editor and script compilation / Play behavior defaults. |
| Layouts | Default docking arrangement; saved presets are retained. |
| Behavior | File extensions, deletion confirmations, auto-save, external scene reloading, and UI alignment guides / snap distance. |
| Editor Camera | All 3D editor camera navigation, movement, look, and zoom preferences. |

**Editor Preferences › Appearance › Theme** offers **System Default** (follows the Windows light/dark setting), **Dark**, **Light**, and **Custom**. **Custom** starts from the active palette and lets you change every color. Older saved `Glass` and `GlassLight` settings load as **Dark** and **Light**, respectively.

Select a theme using the editor-preview tiles: **Light**, **Dark**, **System**, or **Custom**. The blue outline and radio indicator mark the active choice; changes apply and save immediately. The tiles wrap into two columns (or one in very narrow panels) and support keyboard navigation. System shows a split light/dark preview, while Custom previews your saved colors and opens **Custom Colors** when selected.

---

## Core workflows

### Creating scripts and data assets

In the Asset Browser's current folder, right-click and open **Create**:

| Menu | Templates |
|---|---|
| **C# Scripting** | EntityScript, SceneSystem, GlobalSystem, Empty C# Script, Component, Native Component, DataAsset |
| **C++ Scripting** | C++ Script |
| **Data Assets** | Instances of compiled DataAsset classes, grouped by their menu paths |

**Native Component** generates a `.cs` declaration and belongs under C#. **C++ Script** requires an open project; its disabled tooltip explains this requirement. Creation uses the selected folder, name and collision handling, asset registration, and file undo/redo. **DataAsset** under C# creates the class; **Data Assets** creates a `.dataasset` instance. See [Data Assets](../Assets/Data-Assets.md).

Undo and redo feedback uses a mirrored arrow pair with theme-aware text tint. Its texture size follows the displayed icon size and viewport DPI. For shared Launcher/Editor glyphs and maintenance, see [Interface Icons](Interface-Icons.md).

### Picking colors from the screen

On Windows, every editor color window includes a pipette button labeled **Pick screen color**. Activate it, then click any visible screen pixel to copy its RGB color. A swatch and hex value preview the sampled color. **Escape** or **right-click** cancels without changing the value. Sampling keeps the field's existing alpha; Inspector edits support the usual undo/redo. The pipette is currently disabled on Linux and macOS.

### Keyboard shortcuts

Editor controls support **Tab / Shift+Tab** focus navigation. In Preferences the order is search, category tabs, then the selected category's controls; scroll areas are traversed directly. The focus outline remains visible at larger text scales and in narrow windows.

| Focused editor control | Keys | Behavior |
|---|---|---|
| Slider | Left / A; Right / D | Decrease/increase; integers move by one, floats use a step based on range and displayed precision. Held keys repeat at ImGui's navigation rate and clamp to the field limits. |
| Slider | Enter or Ctrl-click | Edit the numeric value as text. Tab focuses the slider without entering this mode. Text editing and modifier shortcuts retain priority. |
| Closed dropdown | Enter | Open with the current selection highlighted. |
| Open dropdown | Up / W; Down / A | Move to the previous/next enabled item, scrolling it into view and stopping at the list ends. W/A is intentional. |
| Open dropdown | Enter; Escape | Commit the highlighted item; or cancel and return focus to the dropdown. Clicking outside cancels an uncommitted highlight. |

Search fields consume letters as text. Disabled or unfocused controls ignore these aliases. Slider changes use the existing save/change path; Preferences and Project Settings save immediately. Inspector drag fields retain their numeric editing and Undo/Redo behavior. These bindings apply to editor controls; game UI has its own focus system. The editor camera only accepts fly keys while its viewport owns the appropriate look interaction.

Verification on Windows Release: six ImGui behavior tests (88 assertions) cover slider Tab versus numeric entry, single-step/disabled/clamped changes, Game View isolation, empty/single/disabled/long dropdowns, scrolling, cancellation and search input. An isolated running editor additionally verified Preferences traversal in both directions at 100% and at 150% in a 760-pixel-wide window, saved float/integer slider values, controlled repeats, mouse/keyboard dropdown selection, Inspector numeric edit/Undo/Redo, and return to Editor View. The live 3D editor camera matrix stayed unchanged during slider and dropdown key sequences. Other platforms were not run in this verification.

The engine and editor select their shortcuts for the platform they are built for. A macOS build uses **Cmd** for standard editing shortcuts and provides alternatives to function keys; changing a game's active build profile does not change the editor's keyboard controls.

| Action | Windows / Linux | macOS |
|---|---|---|
| Save | Ctrl+S | Cmd+S |
| Undo / redo | Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) | Cmd+Z / Cmd+Shift+Z |
| Cut / copy / paste | Ctrl+X / Ctrl+C / Ctrl+V | Cmd+X / Cmd+C / Cmd+V |
| Duplicate | Ctrl+D | Cmd+D |
| Rename selected entity or asset | F2 | Return |
| Delete selected entity or asset | Delete | Cmd+Backspace (the Mac Delete key) |
| Open selected asset or folder | Enter | Cmd+Down |
| Go to parent asset folder | Backspace | Cmd+Up |
| Command palette | Ctrl+K | Cmd+K |
| Toggle Profiler | Ctrl+F6 | Cmd+Shift+P |

Selection shortcuts apply to the focused panel. While editing a name or other text, Return confirms the text instead of starting another rename. For shortcuts in a built game, see [Test Builds and Debug Tools](Testing.md#runtime-keyboard-shortcuts).

During Play mode (including Pause), the command palette and Profiler shortcut are disabled. Menu commands that open editor tool windows, change layouts or reload the editor are also disabled until Stop. An open command palette closes when play begins. Already-open panels remain available for inspection; Play/Pause/Stop controls and runtime debug controls remain available.

### Selecting and editing an entity

1. Click an entity in the **Hierarchy** (or click it directly in the **Viewport**).
2. The **Inspector** shows its components.
3. Edit values — move it, change its sprite, tweak script fields, toggle it on/off.

The Inspector's header (icon, name, enabled switch and options) stays at the top while the components below scroll. The name field renames as you type; the whole edit is one undo step, and **Esc** restores the old name.

Components are listed with the transform first, then the built-in components in a fixed order, then scripts. To rearrange them, drag a component's **four-dot handle** or header onto the upper/lower half of another header to insert before/after its section, or use **Move Up** / **Move Down** in its **...** menu. A thick accent bar with round endpoints marks the insertion position. Scripts and C# components move like any other component. **Transform 2D, Rect Transform 2D and Transform 3D** stay on top and have no drag handle or drag operation. The order is saved with the entity (in scenes and prefabs, also as a prefab override), applies to every selected entity, and is one undo step. Components added later appear at the bottom. Dragging another component's header or handle can also drop a reference into reference fields.

The scene is marked **unsaved** (a dot/asterisk) as soon as you change something.

### Adding components

In the Inspector, click **Add Component**. A searchable list appears with every available component — built-in ones *and* anything your [packages](../Packages/Writing-Packages.md) added. Components that would conflict with what the entity already has are blocked, so you can't create invalid combinations.

### Building the hierarchy

In the **Hierarchy** panel:

- **Right-click** empty space to create entities — empty objects, sprites, physics bodies, UI elements, particles, cameras, and so on.
- **Drag** one entity onto another to make it a **child** (it keeps its on-screen position).
- **Drag** to reorder siblings.
- Select and press **Delete** (**Cmd+Backspace** on macOS) to remove an entity and its children.

Children follow their parent's transform — see [ECS](../Engine-Core/ECS.md#hierarchy-parents-and-children).

### Working in the Viewport

In **2D mode**, the **Editor View** provides these controls; see [3D Editor View](../Engine-Core/3D.md#editor) for 3D navigation and picking:

- **Pan** with the middle mouse button, **zoom** with the scroll wheel.
- **Focus** the camera on the selected entity (frames it in view).
- **Click** a sprite to select it; hold **Ctrl/Shift** (**Cmd/Shift** on macOS) to multi-select.
- With **Gizmos** enabled, drag the on-screen handles to **move, rotate, and scale** the selected entity.

There are also display toggles (draw mode, post-processing on/off) to help you see what you're doing. The draw mode works in the 2D and the 3D Editor View:

- **Default** draws the scene normally.
- **Triangle** draws sprites and meshes as black triangle edges only, including edges hidden behind other meshes.
- **Mixed** draws the normal scene with the triangle edges on top; in 3D only the visible edges.

The spark-shaped **Post Processing** button toggles effects in the 2D or 3D **Editor View** only. Its highlight and `Post-processing: On` / `Post-processing: Off` tooltip show the current state without changing the toolbar size. The toggle does not change the Game View, saved camera effects, or scene data.

#### View Settings

The shaded-sphere **Rendering** popup provides separate 2D/3D preview settings for lighting, shadows, fog, skybox, custom material shaders (including individual shader exclusions), particles, UI, and grid visibility. See [Editor View Rendering](View-Rendering.md) for applicability and persistence. These switches affect the Editor View only.

The gizmo modes float over the top-left corner of the Editor View. The view options — 3D toggle, draw mode, Gizmos, Post Processing and Grid Snap (with its settings arrow) — float over its top-right corner. If you never use some of them, hide them:

- Open **Edit › View**, or **right-click** either floating toolbar.
- Untick any element to hide it. **Gizmo Modes** hides the left toolbar; **Show View Options** hides the whole right one.
- The circular-arrow icon restores the default toolbar and shows everything again.

The choice is saved per user (in the editor preferences, not in the project), so it survives restarts and applies to every project. Hidden actions are still in the command palette (**Ctrl+K**, or **Cmd+K** on macOS): *Gizmo Mode: None / Move / Rotate / Scale / Rect*, *Toggle 3D Editor View*, *Draw Mode: …*, *Toggle Gizmos*, *Toggle Post Processing in Editor View*, *Toggle Grid Snap*, *Reset Editor View Toolbar*.

The **Game View** toolbar (aspect ratio, VSync, Stats, Logs) has the same kind of menu in **Edit › View** (or a right-click on the toolbar): hide single elements or the whole toolbar, and **Reset Game View Toolbar** brings everything back. The command palette keeps *Toggle Game View Stats*, *Toggle Game View Logs*, *Toggle Game View VSync* and *Toggle Game View Toolbar*.

### Play, Pause, Stop

The toolbar controls let you test the game inside the editor:

- **Play** — snapshots all loaded scenes privately (`.funke/PlayMode`) and starts the game, so pressing Play never writes your `.scene` files. Scripts and systems begin running. The **Game View** shows the result.
- **Pause** — freezes the game so you can inspect it; the editor stays responsive.
- **Step** — while paused, advance a single frame (great for debugging).
- **Stop** — ends play and **restores the snapshot**, discarding anything that changed during play. Scenes that had unsaved edits before Play are still marked unsaved afterwards.

That last point is important: changes you make to entities *while playing* are intentionally thrown away when you stop, script fields edited in the Inspector included. See [Scenes › Play mode](../Engine-Core/Scenes.md#play-mode-in-the-editor).

### Saving

Save the current scene from the menu (or with the usual save shortcut). Scenes are written to `.scene` files in your project. The editor can also **auto-save** at an interval you set in preferences.

### Undo and redo

**Ctrl+Z** / **Ctrl+Y** (**Cmd+Z** / **Cmd+Shift+Z** on macOS, or **Edit › Undo/Redo**) work from every editor panel and floating tool window. A text field keeps its own undo while you type; shortcuts do not affect Funke while another application has focus.

Scene history covers inspector and script fields, gizmo moves/rotations/scales, creating, deleting, duplicating, pasting and cutting entities, reparenting and reordering in the Hierarchy, adding/removing components and scripts, component paste/reset, renaming, enabling/disabling, the Static flag, unpacking prefabs and reverting prefab overrides. SceneSystem additions, removal, ordering, enabled state and fields are also undoable.

Each loaded scene keeps its own undo and redo branches when you switch scenes or enter and leave Prefab Mode. A successful Play/Stop cycle restores those branches with the edit scene, including a pending redo. Play takes private snapshots of all loaded scenes, including clean ones, so external file edits cannot change the history's baseline. Closing, replacing or reloading a scene discards its history; closing Prefab Mode discards the prefab's history. Gameplay changes are discarded on Stop.

Moving entities between loaded scenes records a linked undo step in both histories. Undo newer edits in either scene first; the move can be undone only while both histories retain the matching step.

The **Asset Browser** has its own history for recorded file actions such as creating, importing, renaming, moving, duplicating and deleting assets, including companion metadata. Focus the Asset Browser to route Undo/Redo to that history. It lasts while the project is open and refuses to overwrite files that changed externally since the recorded operation. Arbitrary edits in external programs are not recorded.

### Crash recovery

Every 30 seconds the editor keeps a private copy of scenes with unsaved changes in `<project>/.funke/Recovery`. If the editor closes unexpectedly, the next time you open the project it offers to **Restore** those changes (the current file is kept as `<scene>.scene.bak`) or **Discard** them. Your scene files are only touched when you choose Restore. If the last opened scene can't be read, the editor opens an empty unsaved scene instead and leaves the broken file alone.

Logs are written to `%LOCALAPPDATA%\Funke\Logs\<application>.log` and crash dumps to `%LOCALAPPDATA%\Funke\CrashDumps\`. Please attach both when reporting a bug.

---

## Prefabs

A **prefab** is a reusable, saved entity. In the editor you can:

- **Create** a prefab from an entity (it becomes a `.prefab` asset).
- **Edit** a prefab in a dedicated mode that opens just that entity tree.
- **Instantiate** it by dragging the `.prefab` into a scene — each copy is an *instance* that can have its own overrides.

Saving changes to a prefab updates every instance (except the parts an instance has overridden). See [Scenes › Prefabs](../Engine-Core/Scenes.md#prefabs).

---

## Building your game

When you're ready to ship, open the **Build** panel:

1. Choose a **build profile** (target platform and render backend).
2. Check the **scene list**. Every scene in your project is listed automatically and included by default; untick a scene to leave it out of the build. The first ticked scene is the **startup** scene. Drag a row's **four-dot handle** or scene name to reorder it. The upper/lower half of a row inserts before/after that row; the thick accent bar marks the exact position. Dropping in the empty area below the list moves a scene to the end. You can also right-click a row to use **Set as startup scene** / **Move up** / **Move down**. **Add Open Scene** adds the scene you have open, or ticks it again.
3. Pick an **output folder** and click **Build** (or **Build & Run**).

The editor compiles your scripts, stages the **runtime executable**, prunes and converts the referenced **assets**, and writes the configuration the game needs to start. The result is a folder you can run or distribute. Assets are loose by default; **Build Profiles › Asset Packaging** can pack them into `.fbundle` files with optional LZ4 or Zstandard compression. See [Asset bundles](Building-and-Shipping.md#asset-bundles).

Build profiles can be managed separately (create/duplicate/delete) so you can keep, say, a Windows debug profile and a release profile side by side.

For release prerequisites and the clean-machine checklist, see [Building and Shipping](Building-and-Shipping.md).

---

## Summary

- The **Editor** authors your game; the **Runtime** runs it; the **Launcher** creates/opens projects.
- Build scenes with the **Hierarchy** and **Viewport**, edit with the **Inspector**, manage files in the **Asset Browser**.
- **Add Component** respects conflicts; the **Viewport** gizmos move/rotate/scale entities.
- **Play/Pause/Step/Stop** test the game in-editor; stopping restores the private pre-Play scene snapshots.
- **Prefabs** are reusable entities; the **Build** panel produces a distributable game.

## Related pages

- [ECS](../Engine-Core/ECS.md) — entities and components you edit.
- [Scenes](../Engine-Core/Scenes.md) — scenes, prefabs, and Play mode.
- [Assets](../Assets/Assets.md) — the Asset Browser and how assets are stored.
- [Scripting](../Scripting/Scripting.md) — the code behind your entities.
- [Writing Packages](../Packages/Writing-Packages.md) — extend the editor with new components.
- [Building and Shipping](Building-and-Shipping.md) — produce and validate a distributable build.
