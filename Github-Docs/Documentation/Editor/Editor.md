# Editor

The **Funke Editor** is the visual tool where you build your game: arrange entities, edit their components, design scenes, test in Play mode, and build a finished game. This page is a tour of the editor and its core workflows.

If you just want the short version: the editor is a separate app that opens your project. You build scenes in the **Hierarchy** and **Viewport**, edit selected objects in the **Inspector**, manage files in the **Asset Browser**, and press **Play** to test.

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
| **Editor View** (Viewport) | The interactive 2D canvas. Move the camera, select objects, and drag gizmos to position them. |
| **Game View** | A preview of the game exactly as the player will see it (active during Play mode). The aspect dropdown picks a fixed ratio, Free Aspect, or **Project (…)**, which follows the build Aspect Ratio from Project Settings. |
| **Asset Browser** | Your project's files — sprites, scenes, prefabs, scripts. Create assets, drag them into scenes, slice sprites. See [Assets](../Assets/Assets.md). |
| **Console** | Engine and script log output (info, warnings, errors). |
| **Profiler** | Real-time performance timings per subsystem. |
| **Package Manager** | Browse and install [packages](../Packages/Writing-Packages.md). |
| **Project Settings** | Display, graphics, branding (icon/splash), build, and system settings. |
| **Build** | Configure and produce a runnable game. |

A **toolbar** at the top has the playback controls (Play / Pause / Step / Stop), and a **menu bar** has Save, project tools, preferences, and more.

The **Layout** dropdown at the top right switches between saved layout presets. **Default** restores the standard arrangement: Hierarchy on the left, Inspector on the right, Editor View and Game View in the middle, Asset Browser and Console below. **Manage layouts...** opens Editor Preferences › Layouts, where you save the current arrangement as a preset or delete presets. The editor keeps your layout in its user data folder (`%LOCALAPPDATA%\Funke\Editor\imgui.ini` on Windows) and falls back to the default when that file docks none of the main panels.

**Editor Preferences › Appearance › Theme** picks the editor's look: **Auto** (follows the Windows light/dark setting), **Dark**, **Light**, **Liquid Glass** or **Custom**. Liquid Glass makes the panels, menus and windows translucent over a slowly flowing color backdrop, with rounder controls; the Editor View and Game View stay opaque, so the scene looks exactly as in the other themes. The backdrop is drawn by the editor itself, so the desktop behind the window does not show through. **Custom** lets you change every color (the first time, it starts from the theme that was active); giving the window background some transparency there brings back the Liquid Glass backdrop.

---

## Core workflows

### Picking colors from the screen

On Windows, every editor color window includes a pipette button labeled **Pick screen color**. Activate it, then click any visible screen pixel to copy its RGB color. A swatch and hex value preview the sampled color. **Escape** or **right-click** cancels without changing the value. Sampling keeps the field's existing alpha; Inspector edits support the usual undo/redo. The pipette is currently disabled on Linux and macOS.

### Keyboard shortcuts

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

### Selecting and editing an entity

1. Click an entity in the **Hierarchy** (or click it directly in the **Viewport**).
2. The **Inspector** shows its components.
3. Edit values — move it, change its sprite, tweak script fields, toggle it on/off.

The Inspector's header (icon, name, enabled switch and options) stays at the top while the components below scroll. The name field renames as you type; the whole edit is one undo step, and **Esc** restores the old name.

Components are listed with the transform first, then the built-in components in a fixed order, then scripts. To rearrange them, drag a component's header onto another one (the line shows where it lands), or use **Move Up** / **Move Down** in its **...** menu. Scripts and C# components move like any other component; the transform always stays on top. The order is saved with the entity (in scenes and prefabs, also as a prefab override), applies to every selected entity, and is one undo step. Components added later appear at the bottom. Dragging a header still drops a reference to that component into reference fields.

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

The **Editor View** is your 2D workspace:

- **Pan** with the middle mouse button, **zoom** with the scroll wheel.
- **Focus** the camera on the selected entity (frames it in view).
- **Click** a sprite to select it; hold **Ctrl/Shift** (**Cmd/Shift** on macOS) to multi-select.
- With **Gizmos** enabled, drag the on-screen handles to **move, rotate, and scale** the selected entity.

There are also display toggles (draw mode, post-processing on/off) to help you see what you're doing. The draw mode works in the 2D and the 3D Editor View:

- **Default** draws the scene normally.
- **Triangle** draws sprites and meshes as black triangle edges only, including edges hidden behind other meshes.
- **Mixed** draws the normal scene with the triangle edges on top; in 3D only the visible edges.

#### View Settings

The gizmo modes float over the top-left corner of the Editor View. The view options — 3D toggle, draw mode, Gizmos, Post Processing and Grid Snap (with its settings arrow) — float over its top-right corner. If you never use some of them, hide them:

- Open **Edit › View**, or **right-click** either floating toolbar.
- Untick any element to hide it. **Gizmo Modes** hides the left toolbar; **Show View Options** hides the whole right one.
- **Reset to Default** shows everything again.

The choice is saved per user (in the editor preferences, not in the project), so it survives restarts and applies to every project. Hidden actions are still in the command palette (**Ctrl+K**, or **Cmd+K** on macOS): *Gizmo Mode: None / Move / Rotate / Scale / Rect*, *Toggle 3D Editor View*, *Draw Mode: …*, *Toggle Gizmos*, *Toggle Post Processing in Editor View*, *Toggle Grid Snap*, *Reset Editor View Toolbar*.

The **Game View** toolbar (aspect ratio, VSync, Stats, Logs) has the same kind of menu in **Edit › View** (or a right-click on the toolbar): hide single elements or the whole toolbar, and **Reset Game View Toolbar** brings everything back. The command palette keeps *Toggle Game View Stats*, *Toggle Game View Logs*, *Toggle Game View VSync* and *Toggle Game View Toolbar*.

### Play, Pause, Stop

The toolbar controls let you test the game inside the editor:

- **Play** — snapshots your scenes and starts the game. Unsaved edits go into a private snapshot (`.funke/PlayMode`), so pressing Play never writes your `.scene` files. Scripts and systems begin running. The **Game View** shows the result.
- **Pause** — freezes the game so you can inspect it; the editor stays responsive.
- **Step** — while paused, advance a single frame (great for debugging).
- **Stop** — ends play and **restores the snapshot**, discarding anything that changed during play. Scenes that had unsaved edits before Play are still marked unsaved afterwards.

That last point is important: changes you make to entities *while playing* are intentionally thrown away when you stop, script fields edited in the Inspector included. See [Scenes › Play mode](../Engine-Core/Scenes.md#play-mode-in-the-editor).

### Saving

Save the current scene from the menu (or with the usual save shortcut). Scenes are written to `.scene` files in your project. The editor can also **auto-save** at an interval you set in preferences.

### Undo and redo

**Ctrl+Z** / **Ctrl+Y** (**Cmd+Z** / **Cmd+Shift+Z** on macOS, or **Edit › Undo/Redo**) cover inspector field edits, gizmo moves/rotations/scales, creating, deleting, duplicating, pasting and cutting entities, reparenting and reordering in the Hierarchy, adding/removing components and scripts, renaming, enabling/disabling, the Static flag and unpacking prefabs. The history belongs to the scene you are editing: it is cleared when you switch scenes, enter or leave prefab editing, and at Play/Stop. Changes made while playing are not recorded.

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
2. Check the **scene list**. Every scene in your project is listed automatically and included by default; untick a scene to leave it out of the build. The first ticked scene is the **startup** scene. Drag rows to reorder them, or right-click a row to use **Set as startup scene** / **Move up** / **Move down**. **Add Open Scene** adds the scene you have open, or ticks it again.
3. Pick an **output folder** and click **Build** (or **Build & Run**).

The editor compiles your C# scripts, copies the **runtime executable**, copies your **assets**, and writes the configuration the game needs to start. The result is a folder you can run or distribute. Because Funke ships [assets as loose files](../Assets/Assets.md#assets-at-runtime-vs-in-the-editor), the output contains your `Assets` alongside the executable.

Build profiles can be managed separately (create/duplicate/delete) so you can keep, say, a Windows debug profile and a release profile side by side.

For release prerequisites and the clean-machine checklist, see [Building and Shipping](Building-and-Shipping.md).

---

## Summary

- The **Editor** authors your game; the **Runtime** runs it; the **Launcher** creates/opens projects.
- Build scenes with the **Hierarchy** and **Viewport**, edit with the **Inspector**, manage files in the **Asset Browser**.
- **Add Component** respects conflicts; the **Viewport** gizmos move/rotate/scale entities.
- **Play/Pause/Step/Stop** test the game in-editor; stopping restores the scene from disk.
- **Prefabs** are reusable entities; the **Build** panel produces a distributable game.

## Related pages

- [ECS](../Engine-Core/ECS.md) — entities and components you edit.
- [Scenes](../Engine-Core/Scenes.md) — scenes, prefabs, and Play mode.
- [Assets](../Assets/Assets.md) — the Asset Browser and how assets are stored.
- [Scripting](../Scripting/Scripting.md) — the code behind your entities.
- [Writing Packages](../Packages/Writing-Packages.md) — extend the editor with new components.
- [Building and Shipping](Building-and-Shipping.md) — produce and validate a distributable build.
