# Writing Packages

A **package** is how you add new functionality to Index. Anything the engine core doesn't ship — a custom component, a gameplay system, a third-party integration — lives in a package. This page shows you how to write your own.

If you just want the short version: run the scaffolder, fill in your code, list the package in your project, and the editor builds and loads it.

> **See also:** [Package System](Package-System.md) covers the deeper architecture (the loader, build outputs, the CLI, and the non-obvious rules) once you've got the basics here.

---

## What is a package?

A package is a self-contained folder with:

- a **manifest** (`index-package.lua`) that describes it, and
- one or more **layers** of code (C++ and/or C#).

When built, a package produces one or two DLLs that Index loads at startup. Your package can register new components, systems, and behavior, and those show up in the editor just like the built-in ones.

### Where packages live

There are two places a package can live:

| Location | Who it's for |
|----------|--------------|
| `<engine repo>/packages/<Name>/` | Packages shipped with the engine itself. |
| `<your project>/Packages/<Name>/` | Packages that belong to *your* game. **This is where your packages go.** |

End users can also install packages from a cloud registry through the editor's Package Manager — more on that below.

---

## The three layers

A package is made of **layers**. Each layer is a kind of code, and you pick the ones you need:

| Layer | Language | Links the engine? | Use it for |
|-------|----------|-------------------|------------|
| `native` | C++ | **Yes** | Components, systems, anything that talks to the engine in C++. |
| `native_standalone` | C++ | No | Pure C++ with no engine dependency (e.g. wrapping a C library). |
| `csharp` | C# (.NET 10) | n/a (auto-links the script core) | Gameplay logic, managed APIs, tools. |

Two important rules:

- A package can mix `csharp` with `native`, but it **cannot have both `native` and `native_standalone`** — they conflict (one links the engine, the other deliberately doesn't).
- The `csharp` layer automatically references `Index-ScriptCore`, so you get the scripting API for free.

---

## Quick start: scaffold a package

Don't hand-write the boilerplate — use the scaffolder. From the engine repo root:

```sh
# A C++ package for your project, with an example "Health" component wired end-to-end:
python scripts/packages/NewPackage.py MyGame.Combat --layers native --component Health \
    --project "C:/path/to/MyProject"
```

Other common variations:

```sh
# Pure C# package:
python scripts/packages/NewPackage.py MyGame.Tools --layers csharp --project "C:/path/to/MyProject"

# C++ and C# together, bridged via P/Invoke:
python scripts/packages/NewPackage.py MyGame.Loot --layers native+csharp --project "C:/path/to/MyProject"

# Depends on another package (optionally with a version range):
python scripts/packages/NewPackage.py MyGame.Combat --depends-on MyGame.Core@^1.0.0
```

The scaffolder creates the folder, the manifest, the entry point, the example code, **adds the package to your project's allow-list**, and regenerates the project files so it appears in your IDE. After that you can build from the editor (or your IDE) and it loads automatically.

> **Naming rule:** package names are `PascalCase.PascalCase`, with at least one dot. `MyGame.Loot` ✓, `MyGame.Loot.Combat` ✓, but `loot` ✗, `My-Game` ✗, `MyGame.loot` ✗. The folder name must match the package name.

---

## The manifest (`index-package.lua`)

The manifest is a small Lua file that returns a table describing your package. A typical one:

```lua
return {
    name        = "MyGame.Loot",        -- required, PascalCase.PascalCase
    version     = "0.1.0",              -- required, MAJOR.MINOR.PATCH
    description = "Loot drops and tables.",
    engine      = ">=2026.1.0",         -- optional, engine versions this release supports

    layers = {
        native = {
            sources          = { "src/**.cpp", "src/**.hpp", "include/**.hpp" },
            includes         = { "include" },
            private_includes = { "src" },
        },
        csharp = {
            sources     = { "Source/**.cs" },
            pinvoke_dll = "Pkg.MyGame.Loot.Native",  -- only if calling into the native layer
        },
    },

    dependencies = { "MyGame.Core@^1.0.0" },   -- optional, other packages this one needs
    modules = { "Index.2D" },                  -- optional, engine modules this one builds on
}
```

Key points:

- **`name`** and **`version`** are required; every layer needs a non-empty **`sources`** list. Bump `version` with `scripts/packages/BumpPackageVersion.py` when you publish a change: patch for fixes, minor for additions, major for anything that breaks packages or games using yours.
- **`dependencies`** lists *other packages* (not NuGet packages, not the script core), each optionally with a version range (`"MyGame.Core@^1.0.0"` or `{ name = "MyGame.Core", version = "^1.0.0" }`). Dependencies build and load first, and a project only needs to list your package; the rest comes along. The range syntax is in [Package System](Package-System.md#versions-and-dependencies).
- **`engine`** states which engine versions the release supports; a project on another engine gets a clear error instead of a crash.
- **`modules`** lists the [engine modules](../Engine-Core/Engine-Modules.md) your package uses (`"Index.2D"`, `"Index.3D"`). Installing the package turns them on in the project; a project with one of them off doesn't load the package and refuses to build it. Leave it out when you only use the core (UI, text, audio, input, assets). `NewPackage.py --modules Index.2D` writes it for you.
- The manifest is validated when project files are generated — mistakes fail loudly with a clear `[Index Packages]` message, so you'll know right away.

---

## Writing C++ code: the entry point

Every C++ package needs an **entry point** — a function Index calls when it loads your package. Without it, the package won't be recognized.

```cpp
#include "Packages/PackageApi.hpp"
#include "Core/Log.hpp"

extern "C" INDEX_PACKAGE_API int IndexPackage_OnLoad() {
    IDX_INFO_TAG("Pkg.MyGame.Loot", "Loaded.");
    return 0;   // 0 = success
}

// Optional — runs when the package unloads:
extern "C" INDEX_PACKAGE_API void IndexPackage_OnUnload() {
}
```

`INDEX_PACKAGE_API` is the marker that exports these functions across the DLL boundary. Always use it (not the engine's `INDEX_API`).

### Registering a component

Most C++ packages exist to add a **component**. You register it inside `IndexPackage_OnLoad`:

```cpp
#include "MyLootComponent.hpp"
#include "Inspector/PropertyRegistration.hpp"
#include "Packages/PackageRegistration.hpp"

extern "C" INDEX_PACKAGE_API int IndexPackage_OnLoad() {
    using namespace Index;

    std::vector<PropertyDescriptor> props = {
        Properties::Make("DropChance", "Drop Chance", &MyLootComponent::DropChance),
        Properties::Make("TableId",    "Table Id",    &MyLootComponent::TableId),
    };

    Package::RegisterSerializableComponent<MyLootComponent>(
        /* display name    */ "Loot Drop",
        /* subcategory     */ "Gameplay",
        /* serialized name */ "LootDrop",
        /* draw inspector  */ nullptr,   // null = use the automatic inspector
        /* serialize       */ nullptr,
        /* deserialize     */ nullptr,
        /* category        */ ComponentCategory::Component,
        /* properties      */ std::move(props));

    return 0;
}
```

Once registered, your component appears in the editor's **Add Component** menu, draws its fields in the Inspector, and saves/loads with scenes automatically. The `props` list is what makes its fields editable — see the inspector documentation for the full set of field types (sliders, asset pickers, enums, references, and so on).

You can also declare that two components can't coexist:

```cpp
Package::DeclareComponentConflict<MyLootComponent, Index::SomeOtherComponent>();
```

---

## Bridging C# to C++ (P/Invoke)

If your package has **both** a native and a csharp layer, the C# side can call into the C++ side. This is useful when you want fast native data with a friendly managed API on top.

The conventions:

1. Set `pinvoke_dll = "Pkg.<Name>.Native"` in the manifest's csharp layer.
2. Prefix every exported C function with your package name (underscores instead of dots) so symbols don't collide: `MyGame.Loot` → `MyGame_Loot_GetDrop`.
3. Use `extern "C"` on the C++ side and `CallingConvention.Cdecl` on the C# side.

C++ side:

```cpp
extern "C" INDEX_PACKAGE_API
int32_t MyGame_Loot_RollDrop(int32_t tableId) { /* ... */ return 0; }
```

C# side:

```csharp
using System.Runtime.InteropServices;

internal static class Native {
    private const string Lib = "Pkg.MyGame.Loot.Native";

    [DllImport(Lib, EntryPoint = "MyGame_Loot_RollDrop",
               CallingConvention = CallingConvention.Cdecl)]
    public static extern int RollDrop(int tableId);
}
```

The names and signatures **must match exactly** on both sides — there's no tool that checks this for you, and a mismatch shows up as a runtime error. If you need pointer/`Span` work, set `allow_unsafe = true` on the csharp layer.

---

## Turning a package on for your project

A project only builds the packages it explicitly lists. Open your project's `project.json` and add the package to the `packages` array:

```jsonc
{
  "packages": [
    "MyGame.Core",
    "MyGame.Loot"
  ]
}
```

- Only listed packages are built and registered. Their dependencies are pulled in automatically.
- An empty list (or a missing file) means **no** packages — this is deliberate (a fresh project ships with none).
- After editing the list, **regenerate project files** (the scaffolder does this for you; manual edits don't).
- Rebuilding the engine from the editor also regenerates and rebuilds every enabled native package before relaunch.

> If you used `NewPackage.py --project`, this entry was added for you.

---

## Sharing and installing packages

Packages can be shared through Index's **cloud registry**. End users don't touch any of the above — they open the editor's **Package Manager**, browse the registry, and click install. The editor downloads the package, verifies it, drops it into the project's `Packages/` folder, adds it to the allow-list, rebuilds, and loads it — all without a restart.

To publish your own package to the registry, you zip the package folder and add an entry (download URL + checksum) to the registry index. The full publishing flow and the registry format are covered in [Package System](Package-System.md#cloud-registry).

---

## Common mistakes to avoid

These trip people up, so internalize them:

- **No entry point** → the package is silently ignored. Every C++ package needs `IndexPackage_OnLoad`.
- **Forgetting the allow-list** → the package never builds. Add it to `project.json` (and regenerate).
- **Editing files but not regenerating project files** → your IDE solution lags behind the manifest. Re-run premake (or use the scaffolder, which does it for you).
- **Copying only a native DLL** → PackageHost also requires its generated `.dll.indexabi` sidecar. A `native` package must be rebuilt after the engine changes; for source packages under `<project>/Packages/` the editor does this when it opens the project.
- **Mixing `native` and `native_standalone`** → not allowed; pick one.
- **Wrong package name format** → must be `PascalCase.PascalCase`. The folder name must match.
- **P/Invoke name/signature mismatch** → runtime crash. Keep both sides identical.

---

## Related pages

- [Package System](Package-System.md) — the loader, build outputs, the package CLI, the cloud registry, and the deeper rules.
- [ECS](../Engine-Core/ECS.md) — what a component is and how the engine stores them.
- [Scripting](../Scripting/Scripting.md) — the C# API your csharp layer builds on.
