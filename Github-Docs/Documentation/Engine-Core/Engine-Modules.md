# Engine Modules

World rendering and physics are separate optional modules. A project's `modules` selection activates compiled features; it cannot add code missing from the installed engine.

| Module | Features | Requires |
| --- | --- | --- |
| `Funke.2D` | 2D transforms, sprites, cameras, particles, world-space text and picking | Core |
| `Funke.3D` | 3D transforms, cameras, meshes, materials, lights and particles | Core |
| `Funke.Physics2D` | Box2D bodies, colliders, joints, queries, events and debug drawing | `Funke.2D` |
| `Funke.Physics3D` | Jolt bodies, colliders, queries, events and interpolation | `Funke.3D` |

UI, audio, input, scripting, assets and the general fixed-update lifecycle remain available without world physics. `modules: []` selects the Application/UI runtime, not the separate minimal `core` build profile. Each dimension can be absent, present without physics, or present with physics: nine combinations in total. Selecting physics automatically includes its dimension; selecting a dimension does not enable physics.

## Project selection and migration

New projects use project format **2** and explicitly store `modules`. These are excerpts of `project.json`; each row describes a separate project:

| Project | `projectFormatVersion` | `modules` |
| --- | --- | --- |
| 2D rendering | 2 | `["Funke.2D"]` |
| 2D with physics | 2 | `["Funke.2D", "Funke.Physics2D"]` |
| 3D rendering | 2 | `["Funke.3D"]` |
| 3D with physics | 2 | `["Funke.3D", "Funke.Physics3D"]` |

Mixed worlds can enable both dimensions and either, both or neither physics module. IDs are case-sensitive; unknown IDs and malformed lists fail validation. Built-in Empty2D and Empty3D templates start without physics.

Loading an older project expands each selected dimension to include its physics. A missing legacy `modules` field becomes all four modules; an explicit empty list stays empty. Read-only loading performs the same interpretation in memory without rewriting files. The existing project migration backs up the original before saving version 2. Once migrated, removing physics is permanent until explicitly enabled again; subsequent loads do not add it back.

Packages and templates independently declare `moduleRequirementsVersion = 2` (Lua) or `"moduleRequirementsVersion": 2` (JSON). Without it, or with version 1, dimensional requirements retain their legacy physics meaning, including transitive package dependencies. Version 2 separates them. An explicit empty template selection stays empty; it is not a missing recommendation. New package scaffolding writes version 2.

## Editor and command line

The Package Manager's **Engine Modules** section installs/removes modules. Project Settings links there. A changed selection takes effect through the existing editor reload prompt; **Later** leaves **Reload pending** and **Reload Now** available. CLI examples:

```text
funke package add Funke.Physics2D
funke package remove Funke.Physics2D
funke package list --json
```

Enabling physics also enables its dimension. A dimension cannot be removed while its physics or an installed package requires it. Package requirements are checked before installation and loading. JSON module rows expose `enabled`, `compiled`, `status` and `message`; statuses are `enabled`, `disabled`, `not_compiled` and `unknown`.

Add Component and world creation menus hide unavailable components. In a full editor, existing inactive physics components remain serialized and their inspector is disabled with an explanation. They create no bodies, worlds or module-owned systems. Physics binding calls return safe empty values and initialize their output buffers; generic script component queries and pointer access also treat inactive components as unavailable. Native direct registry access remains possible and is not a sandbox.

**Disabled and compiled out differ for data preservation.** A full build can load and save inactive physics data, including joints, polygon paths and mesh references. If a reduced build encounters a serialized physics component whose implementation was compiled out, scene/entity serialization is refused with a diagnostic. The original asset is left intact. This conservative scene-level restriction remains until the scene is reopened in a build containing the required module; deleting the offending entity alone does not clear it.

## One world per entity

2D and 3D world components remain mutually exclusive, including physics components owned by their separate modules. Rect Transform 2D conflicts with world renderers, cameras and physics but can coexist with either world transform for a [world-space UI canvas](World-Space-UI.md). Transform 2D and Transform 3D cannot coexist. Supported add/paste/command-buffer APIs enforce these rules. Legacy mixed data remains loadable with diagnostics and without creating an incompatible body.

## Export and native compatibility

Desktop source exports select a reduced runtime when possible. Android, iOS and Web forward the same resolved selection to CMake and use module-specific caches. Shipped scene/prefab data referencing disabled modules fails export validation.

New reduced runtime IDs use the `v2-` prefix and canonical order `2d`, `3d`, `physics2d`, `physics3d`; examples are `v2-2d`, `v2-2d-physics2d`, `v2-2d-3d-physics3d` and `v2-nomodules`. All four modules use `full`. Old SDK IDs `2d`, `3d` and `nomodules` remain readable metadata, but are not selected as replacements for new configurations. In particular, old `2d`/`3d` binaries contain physics.

Installed SDKs only offer variants whose payloads are declared and present. Current packaging provides full runtimes by default. Missing reduced variants may fall back to the full runtime. Native scripts and native engine packages also use the full runtime because their binaries were linked against it. The exporter explicitly reports this fallback, which leaves disabled backends compiled into the binary. Use `funke build --exact-modules` to require an exact selection; incompatible fallback fails with an explanation. Build JSON includes `runtimeVariant` and `fullRuntimeFallback`.

The managed `NativeBindings` field order and signatures remain unchanged. Native script ABI version 7 reflects the added scene-extension callback; rebuild native script binaries. Package engine fingerprints remain required. A full-runtime fallback makes no backend-size or simulation-speed claim.

## Engine development and verification

`Funke-Engine/EngineModules.json` owns the four source lists and dependency metadata. Premake and CMake read it. Compile guards are `FUNKE_WITH_2D`, `FUNKE_WITH_3D`, `FUNKE_WITH_PHYSICS2D` and `FUNKE_WITH_PHYSICS3D`; runtime activation uses the corresponding `EngineModule` value. Physics requires its dimension in both graphs. Backend include paths, source projects and links belong to the physics selection.

2D scene hooks live in `PhysicsScene2D`; the generic transform hierarchy uses a per-scene backend-free adapter registered only by active physics. Box2D retains its global world and Jolt its per-scene worlds. Fixed-step order is unchanged. Binding slot groups separately cover each module and supply signature-matched inactive stubs.

To generate a rendering-only 2D Windows runtime:

```bat
vendor\bin\premake5.exe --without-3d --without-physics2d vs2022
```

Build `premake/generated/variant-v2-2d/Funke-Runtime/Funke-Runtime.vcxproj`. CMake accepts explicit `MODULES Funke.2D` or `MODULES none` through `funke_add_engine`. Nine-way reference checks are repeatable with:

```text
python scripts/ci/physics_module_matrix.py --build
python scripts/ci/physics_cmake_matrix.py
```

The first command builds and runs native smoke tests on desktop platforms, auditing backend project references and source selection (MSBuild on Windows, Make on Linux/macOS). Use `--variant v2-2d-3d` for the rendering-only representative selected by Linux/macOS CI. The second configures all nine CMake graphs with dependency stand-ins; it is a graph check, not a cross-platform compiler or device test. Neither test establishes a physics performance improvement.

See [Physics Modules Verification](Physics-Modules-Verification.md) for executed builds, runtime checks, compatibility evidence and remaining qualification work.

The four `FNK_FOR_EACH_*_BINDING` lists in `Scripting/ScriptBindingModuleSlots.hpp` are the shared source for guarded registration and inactive-module stubs. Add a module slot there and implement its `Funke_<Slot>` function in the corresponding `ScriptBindings*.cpp`; do not maintain a second assignment list. Native/managed positional ABI declarations still need matching signatures and ordering, with the existing version handshake.
