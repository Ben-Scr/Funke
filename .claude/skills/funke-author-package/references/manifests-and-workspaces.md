# Package manifests and workspaces

## Scaffolding new packages

Don't hand-write the boilerplate. Use the scaffolder:

```sh
# Engine-shipped native-only:
python scripts/packages/NewPackage.py Funke.Foo

# Engine-shipped csharp-only (Serialization-style pure managed):
python scripts/packages/NewPackage.py Funke.Foo --layers csharp

# Native + C# bridged (Tilemap2D-style):
python scripts/packages/NewPackage.py Funke.Foo --layers native+csharp

# Same, plus an example component end-to-end (.hpp/.cpp/.cs/PInvoke):
python scripts/packages/NewPackage.py Funke.Foo --layers native+csharp --component Health

# Project-local (also adds to project.json packages array):
python scripts/packages/NewPackage.py MyGame.Loot --layers native+csharp \
    --project "C:/Users/me/Funke/Projects/MyGame"

# Skip premake regen (CI / batch):
python scripts/packages/NewPackage.py Funke.Foo --no-premake

# With dependencies on other packages (optional @range per entry):
python scripts/packages/NewPackage.py Funke.Foo --depends-on Funke.Bar@^1.0.0,Funke.Baz
```

`--layers` shorthand: `native` → `native`, `standalone` → `native_standalone`, `csharp` → `csharp`. Separate with `,` or `+`.

`--component Health` is the recommended starter for gameplay packages — it scaffolds `HealthComponent.{hpp,cpp,cs}`, the matching `Properties::Make` registration, and the Get/Set P/Invoke surface. The name must NOT include a trailing `Component` (the scaffolder adds it).

After creation the scaffolder regenerates projects — `premake5 vs2022` for engine packages, `premake/project-packages.lua --funke-project=…` with `--project` — and, for a csharp layer, runs `dotnet restore` on the generated `Pkg.<Name>.csproj` (the mixed engine solution can't be restored by the .NET CLI). Pass `--no-premake` to skip that.

Sibling scripts: `scripts/packages/RemovePackage.py` (deletes the dir + allow-list entry + reruns premake) and `scripts/packages/BumpPackageVersion.py` (rewrites the `version` line).

## The three layers

A package declares one or more layers under `manifest.layers`. **Canonical names** (use these in new manifests):

| Layer | Builds | Links the engine? | Default refs |
|---|---|---|---|
| `native` | `Pkg.<Name>.Native` (C++ SharedLib) | Yes (via `EditorRuntimeCommon`) | Engine include set, `FNK_IMPORT_DLL`, `FUNKE_ENTITY_BITS=…` |
| `native_standalone` | Same `Pkg.<Name>.Native` SharedLib | **No** — pure C++, no engine symbols | None |
| `csharp` | `Pkg.<Name>` (C# net10.0 SharedLib) | n/a | Auto-links `Funke-ScriptCore` |

**Deprecated aliases** (still loaded but warned): `engine_core` → `native`, `standalone_cpp` → `native_standalone`. The loader normalises them before validation. Rename old manifests, don't add new ones with the old names.

**Hard rule:** `native` and `native_standalone` **cannot coexist** in the same package. They would merge into a single `Pkg.<Name>.Native` DLL, but `native` links the engine and `native_standalone` is the explicit "no engine link" path — combining them silently links the engine into "standalone" sources and defeats the whole point. The loader fails loud (`PackageError` in [package-loader.lua](../../../../premake/package-loader.lua)). `NewPackage.py` rejects the combo upfront too.

## Manifest schema

`packages/<Name>/funke-package.lua` returns a table:

```lua
return {
    name        = "Funke.Hello",       -- required, PascalCase.PascalCase[…], must match folder name in practice
    version     = "0.1.0",             -- required, SemVer MAJOR.MINOR.PATCH[-pre]
    description = "Hello world.",      -- optional
    engine      = ">=2026.1.0",        -- optional engine version range (npm syntax)

    layers = {                         -- required, at least one layer
        native = {
            sources          = { "src/**.cpp", "src/**.hpp", "include/**.hpp" },   -- required list
            includes         = { "include" },                                       -- optional, public-ish
            private_includes = { "src" },                                           -- optional
        },
        csharp = {
            sources      = { "Source/**.cs" },
            pinvoke_dll  = "Pkg.Funke.Hello.Native",   -- only when bridging to native sibling
            allow_unsafe = true,                        -- enables `clr "Unsafe"` for fixed-pin pinvoke
            nuget        = { "System.Text.Json:8.0.0" }, -- optional PackageReferences (keep short)
        },
    },

    -- optional; "Name", "Name@range", { name = ..., version = range } or ["Name"] = range
    dependencies = { "Funke.Foo", "Funke.Bar@^1.2.0" },

    -- optional; engine modules the package builds on. Install enables them; a project with one off
    -- doesn't load the package, GameBuilder refuses the build, and the module can't be removed under it.
    -- Omit when only core APIs (UI, text, audio, input, assets) are used.
    modules = { "Funke.2D" },
}
```

Manifests are parsed by Lua (premake) **and** by a literal-only reader in C++ (`Packages/PackageManifest.cpp`, used by PackageHost and the editor): keep them plain data, with no function calls, string concatenation or variables, or the editor and runtime ignore the package.

**Validation rules enforced by the loader** (all fail-loud with `[Funke Packages]` prefix):

- `name` needs dot-separated alphanumeric segments, each starting uppercase, with at least one dot. `NewPackage.py` additionally requires at least two characters per segment (`^[A-Z][A-Za-z0-9]+(\.[A-Z][A-Za-z0-9]+)+$`); the Lua loader accepts single-letter segments. Use names accepted by both, such as `Funke.Foo` or `MyGame.Loot.Combat`.
- Layer names must be `native` / `native_standalone` / `csharp` (or the deprecated aliases).
- Every layer must declare a non-empty `sources` list.
- `pinvoke_dll` on the csharp layer requires a sibling native layer and must equal `Pkg.<manifest.name>.Native` exactly.
- `nuget` entries must be `"PackageId:Version"` strings.
- `dependencies` entries need valid names and ranges, no self-reference, no duplicates. The loader topologically sorts; cyclic deps, missing deps and an installed version outside a declared range fail loud. A non-SemVer `version` only warns, but any dependent with a range on it then fails.
- `modules` must be a list of known module ids (`Funke.2D`, `Funke.3D`); module ids are not package names, so never put them in `dependencies`. `NewPackage.py --modules Funke.2D` scaffolds it.
- `engine` must parse as a range and accept the engine's `FNK_VERSION` (pre-release suffix ignored), read from `Funke-Engine/src/Core/BuildMetadata.generated.hpp`.
- The same rules (SemVer, ranges, pre-release opt-in) are implemented twice: Lua in `package-loader.lua` (self-test: `vendor\bin\premake5.exe --file=premake/tests/package-loader-tests.lua funke-test-packages`) and C++ in `Packages/SemanticVersion.cpp` / `PackageResolver.cpp` (doctest `PackageVersioningTests.cpp`). Change both together.
- Duplicate package names across `packages/` and `<project>/Packages/` fail loud.

## The two roots

| Root | Who ships it | Generated into |
|---|---|---|
| `<repo>/packages/<Name>/` | Engine-shipped | `Funke.sln` (engine `premake5 vs2022`): every package, no allow-list |
| `<project>/Packages/<Name>/` | Per-project (local, or installed from the registry by the editor) | The project's own package workspace `<project>/.funke/build/packages/`, generated by `premake/project-packages.lua` |

Generate a project's package workspace (NewPackage.py, RemovePackage.py and the editor run this for you; the editor's code path is `FunkeProject::RegeneratePackageWorkspaceForProject` + `BuildProjectPackages`):

```sh
./vendor/bin/premake5.exe --file=premake/project-packages.lua --funke-project="C:/path/to/MyProject" vs2022
```

The path must be absolute. The workspace registers the project's enabled packages from both roots. On the engine workspace (`premake5.lua`) `--funke-project` only selects `entityBits`; it no longer registers project packages.

## The project allow-list

In the project package workspace and at runtime (`PackageHost`), `<project>/project.json` is the source of truth for which packages register and load:

```jsonc
{
  "packages": [
    "Funke.Tilemap2D",
    "MyGame.Loot"
  ]
}
```

Behaviour (`ResolveProjectPackages` / `FilterManifestsByAllowList` in `package-loader.lua`):

- **Field present and non-empty** → only listed packages are registered. Transitive dependencies are auto-included so listing `B` (which deps on `A`) pulls `A` along.
- **Field present and empty `[]`** → no packages registered (fresh-project state).
- **Field absent / `project.json` missing** → fail closed (no packages registered). This is deliberate — without an explicit list we don't know what the project intends.
- **Engine workspace (`Funke.sln`)** → no filter: every package under `<repo>/packages/` is registered.

Skipped packages get a one-line `[Funke Packages] Skipped N package(s)…` log. Allow-list entries that don't resolve to a manifest get a `WARNING: project lists package 'X' but no manifest…`.

## Build outputs

For each registered package (`<cfg>` = e.g. `Release-windows-x86_64`; engine packages under the repo root, project packages under `<project>/.funke/build/packages/`):

- `native` and/or `native_standalone` → one `Pkg.<Name>.Native` project (C++ SharedLib, C++20, /Zc:preprocessor, MultiProcessorCompile).
  - Output: `bin/<cfg>/Pkg.<Name>.Native/Pkg.<Name>.Native.dll`, plus the `.funkeabi` sidecar written by a post-build step (see *Runtime loading*)
  - Intermediate: `bin-int/<cfg>/Pkg.<Name>.Native/`
  - Generated vcxproj: `premake/generated/Pkg.<Name>.Native/` (project packages: `generated/Pkg.<Name>.Native/`)
- `csharp` → one `Pkg.<Name>` project (C# SharedLib, net10.0, Nullable enabled, EnableDynamicLoading true, AppendTargetFrameworkToOutputPath=false).
  - Output: `bin/<cfg>/Pkg.<Name>/Pkg.<Name>.dll`
  - Implicit references: `Funke-ScriptCore` (always — don't add it as a dependency manually; the project workspace links the engine's built `Funke-ScriptCore.dll` per configuration).

Configurations: `Debug` / `Release` / `Dist`. The Dist config strips symbols and is what shipped runtimes use.

## Manual manifest changes

After changing the allow-list or package set by hand, regenerate the owning workspace. Use the engine's Premake configuration for engine packages or `premake/project-packages.lua` with `--funke-project` for project packages. The scaffolder and editor installer handle their own regeneration.

The loader and scaffolder validate the package name because it becomes a folder, project and DLL name. The C# layer already references Funke-ScriptCore. Use `manifest.dependencies` for other Funke packages and pin versions in `nuget` for the NuGet libraries the package ships.
