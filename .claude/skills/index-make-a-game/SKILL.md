---
name: index-make-a-game
description: START HERE when building a GAME with the Index engine (using Index, not modifying the engine itself). Use when the user says "make a game in Index", "create a new Index project/game", "start an Index game", "I want to build a 2D game with Index", "add a player/enemy/level/menu", or any open-ended game-creation request where you need to know how the pieces fit and which specialist skill to open next. This is the orientation + quickstart + router skill: it gives the end-to-end workflow (Launcher → project → scenes/entities/components → C# scripts → Play → Build → Runtime) and points to the focused skills for scripting, project structure, UI, assets, and shipping. Open this first whenever you're unsure where to start; it is deliberately broad. For deep work, jump straight to the specialist skill named below. NOTE: this is for people MAKING games WITH Index — not for modifying the engine's C++ (for that, see the engine-dev skills like index-add-component / index-build-engine / index-engine-scripting).
---

# Making a Game with Index

Index is a **2D and 3D game engine** (3D: `Transform3D`, `Camera3D`, PBR materials, lights and shadows, Jolt physics, `ParticleSystem3D` effects; an agent building a 3D game reads `index-build-game` → `references/3d-games.md`). You author your game in the **Editor**, write game logic in **C# (.NET 10)**, and ship a slim **Runtime** executable with your assets beside it. This skill is the map; the specialist skills below are the territory.

**Audience:** game developers (and agents helping them) building a game *with* Index. If you are instead changing the engine's own C++ — adding a built-in component, a render feature, a package — those are different skills (`index-add-component`, `index-build-engine`, `index-engine-scripting`, `index-author-package`, `index-inspector-ruleset`).

## The mental model

```
Launcher ──create/open──► Project (a folder + project.json)
                              │
                          Editor (authoring)
                              │
   Scenes ── contain ──► Entities ── have ──► Components
      │                                          ▲
      │ behavior                                 │ data your scripts read/write
      ▼                                          │
   C# Scripts ── EntityScript / SceneSystem / GlobalSystem
      │
      ▼
   Press Play (test in editor)  ──►  Build  ──►  Runtime (ships your game)
```

- A **Project** is a folder with a `project.json`, an `Assets/` tree, and a C# script assembly. → `index-project-structure`
- A **Scene** (`.scene`) holds **Entities**; each entity is a bag of **Components** (Transform2D, SpriteRenderer, Rigidbody2D, your scripts…).
- **Behavior** is C# attached to entities/scenes. → `index-game-scripting`
- **Content** (sprites, audio, fonts, data) lives in `Assets/`, referenced by stable GUID. → `index-assets-and-content`
- **Menus/HUD** are built from UI components. → `index-game-ui`
- **Shipping** copies the runtime exe + your loose assets into an output folder. → `index-build-and-ship`

## Which skill do I open?

| You're doing… | Open |
|---|---|
| Writing player/enemy/game logic in C#; lifecycle, input, physics, audio, coroutines, scene queries | **`index-game-scripting`** |
| Understanding the project folder, `project.json`, the `.csproj`, where scripts/scenes/prefabs live, file formats | **`index-project-structure`** |
| Buttons, sliders, HUD, pause/settings menus, pointer/click handling | **`index-game-ui`** |
| Translations: `Assets/Localization/<lang>.json`, `LocalizedText` component, C# `Localization` (language picker, saved choice) | **`index-game-ui`** + `Github-Docs/Documentation/Engine-Core/Localization.md` |
| Sprites, audio, fonts, asset references, `AssetManager`, Data Assets (ScriptableObject equivalent) | **`index-assets-and-content`** |
| Build profiles, window/splash/icon config, producing a distributable game | **`index-build-and-ship`** |
| Exposing script fields to the Inspector (`[ShowInEditor]`, `[ClampValue]`, asset slots, …) | **`index-inspector-ruleset`** (existing) |
| Adding a reusable component/feature library that others install | **`index-author-package`** (existing) |
| Building a phone **app** rather than a game (tabs, lists, settings, music player, notes, …) | **`index-make-a-mobile-app`** |

## End-to-end quickstart

1. **Create the project.** In the **Launcher**, New Project → name + location. You get a project folder with `Assets/Scripts/`, `Assets/Scenes/SampleScene.scene` (one Camera entity), a `<Name>.csproj`, and a starter `Assets/Scripts/GameScript.cs`. (Details + the exact tree: `index-project-structure`.)

2. **Build the scene.** In the Editor's **Hierarchy**, right-click → create entities (Sprite, Camera, UI, physics body…). Position them in the **Viewport** with gizmos. Edit components in the **Inspector**. Drag a sprite from the **Asset Browser** onto a SpriteRenderer.

3. **Write a script.** Add a `.cs` under `Assets/` (anywhere — the csproj globs `Assets\**\*.cs`):

   ```csharp
   using Index;

   public class PlayerController : EntityScript
   {
       [ShowInEditor("Move Speed")] public float Speed = 5.0f;

       public override void OnUpdate()
       {
           Vector2 move = Input.GetAxis();                 // smoothed WASD/arrows, -1..1
           Transform.Position += move * Speed * Time.DeltaTime;
       }
   }
   ```

   Then in the Editor: select the entity → **Add Component → Script** → pick `PlayerController`. (Full API: `index-game-scripting`.)

4. **Play.** Press **Play**. The editor saves scenes to disk, reloads them, and runs `OnAwake → OnStart → OnUpdate…`. **Stop** reloads from disk, discarding play-mode changes (a safety net — don't expect runtime edits to persist).

5. **Build.** Open the **Build** panel → pick a profile + scene list + output folder → Build. You get a runnable folder: runtime exe + your `Assets/` as loose files. (Details: `index-build-and-ship`.)

## Golden rules (read before generating game code)

- **Pick the smallest script scope.** Most logic is `EntityScript` (one entity). Reach for `SceneSystem` only to operate over *many* entities at once, and `GlobalSystem` only for state that must survive scene loads (game manager, settings, save data).
- **`OnUpdate` vs `OnFixedUpdate`.** Frame/input/rendering logic → `OnUpdate` (scale with `Time.DeltaTime`). Physics → `OnFixedUpdate`.
- **Reference assets by slot, not path.** Expose a `public Texture? Icon;` (or `Audio`, `Font`) field and assign it in the Inspector; the engine stores a stable GUID. Hard-coded paths are a last resort. → `index-assets-and-content`.
- **Everything is C#-first.** Drop to native C++ `NativeScript` only when you've measured a real need; the Build panel doesn't ship native scripts.
- **Assets are loose files, always.** No cook/pack step. The runtime reads PNGs/WAVs from `Assets/` on disk. Keep every `.meta` file and commit it.
- **New projects save `.scene`/`.prefab`/`.dataasset` as JSON** (diff-friendly, editable by tools); builds ship them as binary. Older projects may still be Binary — `index convert --format json` or Project Settings switches them. → `index-project-structure`.
- **The `index` command-line tool** does the Launcher/Editor jobs without a window: `index create`, `index build [--release]`, `index run`, `index validate`, `index prefab …`, `index sprite slice …`, `index package …` (`Github-Docs/Documentation/Editor/Command-Line.md`).
- **`SceneSystem`, not `GameSystem`.** The per-scene C# base class is `SceneSystem`. (Some older notes say `GameSystem` — that's stale.)
- **Only the components an entity needs.** An entity that is never drawn, moved or looked up by name (an `AudioManager`, a `GameState` holder, a spawner that only runs a script) gets no `Transform2D`/`Transform3D`; drop `NameComponent` too when nothing finds it by name. Author such entities in the scene JSON and simply omit `"name"` and the transform (C# `Entity.Create()` always adds a transform; it skips the name when you pass none). The Hierarchy then shows "Unnamed Entity (<id>)", so keep a name on anything a human will look for in the editor.
- **Own components instead of fat scripts.** Data that several scripts or systems share belongs in a component, not in an `EntityScript` field soup (see *Custom C# components* below).

## Custom C# components

Game code can define its own components; both kinds are added in the editor (**Add Component**) and from code. Name the `.cs` file exactly like the type — the editor discovers them by file name.

**Struct components (`IComponent`)** — blittable data in native ECS storage, fastest for many entities and for `QueryRef`:

```csharp
using System.Runtime.InteropServices; using Index; using Index.Components;
[StructLayout(LayoutKind.Sequential)]                  // required, else it is skipped
public struct Velocity : IComponent { public Vector2 Value; public float Drag; }   // file: Velocity.cs

Entity.AddNativeComponent<Velocity>();
ref var v = ref Entity.GetNativeComponent<Velocity>();  // ref into storage; Unsafe.IsNullRef(ref v) if missing
v.Value *= 1f - v.Drag * Time.DeltaTime;
bool has = Entity.HasNativeComponent<Velocity>(); Entity.RemoveNativeComponent<Velocity>();
foreach (ref var vel in Scene.QueryRef<Velocity>()) vel.Value *= 0.99f;   // many entities, in place (index-game-scripting)
```
- Fields: `bool`, integer types, `float`, `double`, enums and structs made of those (e.g. `Vector2`, `Vector3`). No `string`, `Entity`, arrays or references — the struct is silently skipped.
- Listed under **Native Components (C#)**; serialized as raw bytes. Only **append** fields later — reordering or inserting shifts saved data.
- Inspector edits write straight into the component's bytes (saved with the scene); in scene JSON the data is a hex blob, so author values in the editor or in code rather than by hand.
- Re-fetch the `ref` after adding/removing components on that entity.

**Class components (`Component`)** — managed data with references, strings and Inspector-editable fields, no lifecycle callbacks:

```csharp
using Index;
public class Health : Component { public int Max = 10; [ShowInEditor] private int current; }   // file: Health.cs

var h = Entity.GetComponent<Health>() ?? Entity.AddComponent<Health>();
bool has = Entity.HasComponent<Health>(); Entity.RemoveComponent<Health>();
```
- Public fields (or `[ShowInEditor]`) show in the Inspector and are saved in the scene (`"ManagedComponents"` + `"ScriptFields"`, see `index-build-game` → `references/scene-json.md`). Needs a parameterless constructor.
- Values changed at runtime live only in C# and are not written back to the scene.
- Logic stays in an `EntityScript`/`SceneSystem` that reads the component; a `Component` gets no `OnUpdate`.

## Verifying your work

- **Compile check:** the game's C# assembly builds with `dotnet build <Project>/<Name>.csproj`. Scripts reference `Index-ScriptCore.dll` from `<Project>/Packages/Index-ScriptCore/`.
- **In-editor:** Console panel shows `Log.Info/Warn/Error` and `Print(...)` output. Press Play to exercise lifecycle code.
- Don't claim a feature works until you've either built the assembly or run it in Play mode and seen the behavior.

## Testing without clicking

An agent can't play the game, so build hooks that let it reach any state from the command line and then look at the result.

- **Env-var entry points.** Read a few environment variables in the boot/startup script, e.g. `MYGAME_START_SCENE` (jump straight to a scene), `MYGAME_LEVEL` (load a specific level file), `MYGAME_AUTOPLAY` (replay recorded input). Unset variables keep normal behavior. Launch with the engine repo's `scripts\dev\Run.ps1 -App Runtime -Project <dir> -Seconds N -Shots K -Interval S`. It screenshots *its own* process window into `bin-int\_agent\run\` and prints the log slice. Time `-Seconds` so the shots land on the moment you want to see.
- **Log the facts you'd otherwise eyeball.** Log a line on state changes ("mode Cube -> Ship at x=265", "died at x=…", "level completed") and grep the run log. That proves a full playthrough even when no screenshot catches it.
- **Measure, don't guess.** For "feels laggy / not smooth", log numbers (e.g. mean and stddev of camera speed over 600 frames) before and after the fix. Gate temporary probes behind an env var and delete them afterwards.
- **Deterministic simulation.** If gameplay uses fixed-step physics (recipe in `index-game-scripting`), mirror it in a small script (Python is fine) and use it to check that every level is beatable and to generate the autoplay input. Re-run it whenever physics constants or levels change.
- **Temporary UI state.** To screenshot a popup, gate a one-line `Open()` call behind an env var, run it once, and remove it again.

## Generating content from scripts

Writing scenes, prefabs, `.dataasset`s and textures with a generator script (switch the project to JSON serialization first) is fast and repeatable. Two rules:

- **Write atomically and only on change** (write a temp file, then rename; skip identical content). An open Editor hot-reloads changed assets, and a half-written PNG or scene loads as a white or missing texture.
- **Keep GUIDs stable.** Reuse an existing `.meta` GUID when regenerating, because scenes and data assets reference it. If the Editor already created a `.meta` for a new file, keep its GUID.
