# Engine Modules

Index splits its world features into two optional engine modules. A project lists the ones it uses in `project.json`:

```json
"modules": [ "Index.2D", "Index.3D" ]
```

| Module | Contains |
|---|---|
| `Index.2D` | Transform 2D, Sprite Renderer, Camera 2D, Sprite Animator 2D, Particle System 2D, Post Processing 2D, 2D physics (Box2D and Index-Physics: Rigidbody 2D, colliders, `Physics2D`), world-space text on 2D entities, 2D picking |
| `Index.3D` | Transform 3D, Camera 3D with sky, Mesh Renderer, materials and meshes, Directional/Point/Spot lights, Particle System 3D, 3D physics (Jolt: Rigidbody 3D, colliders, `Physics3D`) |

UI (Rect Transform 2D and every UI widget), text, images, audio, scripting, gizmos, input and assets belong to the engine core and are always available. A project may list one module, both, or none (a pure UI app).

## In the editor

The **Package Manager** lists the modules under **Engine Modules**, next to the packages: **Install** turns a module on, **Remove** turns it off, and **Reload Editor** in the banner applies the change. Project Settings only shows which modules are on and links there. From the command line, `index package add Index.3D` / `index package remove Index.3D` do the same.

Packages declare the modules they build on (`modules = { "Index.2D" }` in `index-package.lua`). Installing such a package turns those modules on as well, including the ones its dependencies need, and a module can't be removed while an installed package needs it. A package whose module is off is not loaded and the build stops with an error that names it.

The editor offers only the enabled modules:

- the Add Component menu and the **Create** menus (2D Entity, 3D Entity presets, Camera) list their components only while their module is on;
- the Editor View is fixed to 2D or 3D when only one module is on, and shows the **3D** toggle only with both;
- **Create > Material** and the **3D Rendering** graphics settings need `Index.3D`;
- systems and renderers of a disabled module don't run, and script calls into its API do nothing and log one error.

Turning a module off never deletes data: components of a disabled module stay in scenes and prefabs, and loading such a scene logs a warning that names the module.

Projects created before modules existed have no `modules` field and keep both. New projects get the modules of their template (the built-in starter is `Index.2D`).

## One world per entity

An entity is either 2D, 3D or UI. Components of the 2D and the 3D module can't be combined on one entity, and neither can sit on an entity with a Rect Transform 2D: the Add Component menu greys them out ("Conflicts with Transform 2D"), and `Entity.AddComponent`, entity command buffers and **Paste Component** refuse them with a warning (`Entity.AddComponent<T>()` returns `null`). Put a 3D object and a 2D object next to each other as separate entities instead.

Scenes saved before this rule may still mix them. They load unchanged, the editor logs which entity combines which components, and the component of the other world stays inactive: a 2D physics body or camera on a 3D or UI entity is not created (it never adds a second transform), and 3D components on a 2D entity are ignored by the 3D renderer and physics.

## In a game build

The game ships a runtime compiled **without** the modules the project leaves out — their code, shaders and third-party libraries (Box2D and Index-Physics for `Index.2D`, Jolt for `Index.3D`) are not in the executable.

- **Windows, Linux, macOS:** the build generates a runtime variant workspace (`premake/generated/variant-<modules>`) and compiles it into `bin/<config>-<system>-<arch>-<modules>`. The first build of a variant compiles the whole engine once; later builds are incremental.
- **Android, iOS, Web:** the platform package passes the modules to the engine's CMake build (`MODULES` of `index_add_engine`); each module set gets its own build cache.
- A project whose shipped scenes or prefabs use a component of a module it doesn't list fails the build, with every offending entity in the log.
- Projects with native (C++) packages ship the full engine: those packages are linked against it.
- From an EngineSDK without project files (no MSBuild/`make` workspace) the build falls back to the prebuilt full runtime.

## For engine developers

The module source lists live in [`Index-Engine/EngineModules.json`](../../../Index-Engine/EngineModules.json); premake and `cmake/IndexEngine.cmake` both read it. Code in the core that touches a module guards it with `#if INDEX_WITH_2D` / `#if INDEX_WITH_3D` (compile time) and asks `EngineModules::IsActive(EngineModule::TwoD)` (project setting at runtime). Built-in components register inside a `ModuleRegistrationScope` so the editor can hide them. Script bindings of a module live in `Scripting/ScriptBindings2D.cpp` / `ScriptBindings3D.cpp`, and every slot they fill is listed in `Scripting/ScriptBindingModuleSlots.hpp`, which provides signature-matched stubs when the module is off.

Build a variant by hand:

```bat
vendor\bin\premake5.exe --without-3d vs2022
```

then build `premake\generated\variant-2d\Index-Runtime\Index-Runtime.vcxproj`, and run it with `scripts\dev\Run.ps1 -App Runtime -Variant 2d -Project <dir>`.
