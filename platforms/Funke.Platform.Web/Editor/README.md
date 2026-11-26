# Original Funke editor: browser authoring and Web game export

The original C++ editor, renderer, scene serializer and Box2D run in WebAssembly.
A .NET/Mono host runs the matching Funke-ScriptCore and Roslyn compiles C# locally.
The HTML host provides browser file transfer, a source editor and ZIP packaging.

## Build and run

From the engine root, with the installed .NET 11 wasm-tools SDK:

```powershell
python platforms/Funke.Platform.Web/Editor/build-managed.py
python -m http.server 4186 --bind 127.0.0.1 --directory output/web-editor/managed/publish/wwwroot
```

The build uses `output/web-editor/managed/native` and the existing SDL/Dawn platform caches.
Only the `FunkeWeb` target is built. No desktop targets, Premake regeneration or cache cleanup
are required. `--host-only` updates managed code and browser files after a successful native
build. Browser-only edits can be copied into the published folder without compiling C++;
refresh `runtime-files.json` before staging a website release.

The earlier `build.py` target is retained as the native-only feasibility build. The deployed
editor uses `build-managed.py`, `Managed/Program.cs` and `Browser/`.

## Authoring

- Edit native scenes and components in the original hierarchy/Inspector. Play/Stop runs the
  original game loop and restores the scene snapshot when stopping.
- **C#-Skripte** creates/imports `.cs` files, edits source, compiles all C# under `Assets/`,
  reports file/line/column diagnostics and assigns compiled EntityScripts to scene objects.
  Double-clicking C# files in the native Project browser opens the same source editor.
- Successful builds load through the existing native scripting facade and binding tables.
  Syntax errors retain the working assembly. Browser-specific preflight loads unique assembly
  identities from bytes, validates type/layout metadata and only then replaces instances.
- Ctrl+S saves scenes. The help menu saves full project ZIPs (including code and assets),
  restores them after downloading a backup, and imports assets or individual native scenes.
  IndexedDB uses the existing `/libsdl` mount, preserving earlier editor projects/preferences.

## Export

**Webspiel exportieren**, also reachable from the native Build panel, emits a ZIP with a
selected startup scene, all project assets/scenes, freshly compiled C# and the complete runtime.
The game uses the original engine in player mode with no editor layers. The package starts
independently and loads no runtime files from Schneidertools or a CDN.

Extract and run `python start-local.py`, or host the entire folder over HTTPS. WebAssembly
cannot be started from a `file://` URL. The release script compile defines are `FUNKE_BUILD`,
`FUNKE_BUILD_RELEASE`, `FUNKE_WEB`, `FNK_DIST`; editor compiles use `FUNKE_EDITOR`,
`FUNKE_BUILD_DEVELOPMENT`, `FUNKE_WEB`, `FNK_RELEASE`. `Application.IsEditor` is false in exports.

## Browser limits

Requires desktop WebGPU. Native extension/package installation, OS processes, desktop/mobile
exports, detached OS windows and 3D physics are not provided. Browser .NET cannot block or
create ordinary native threads; use supported frame/coroutine APIs. ScriptCore is the original
API, not a C# syntax emulator. Unsafe/PInvoke calls still require signatures supported by the
prebuilt runtime. Compiler references are the matching .NET reference assemblies and ScriptCore.

Mono retains compiled revisions until tab reload. Scripts are emitted without PDBs because
Roslyn's PDB writer uses blocking Task waits; compiler diagnostics still report source locations.
Project backups contain source; game exports omit source and include a newly compiled assembly.

## Verification

```powershell
$env:PLAYWRIGHT_MODULE = '<existing Playwright module directory>'
$env:FUNKE_EDITOR_URL = 'http://127.0.0.1:4186'
node platforms/Funke.Platform.Web/Editor/managed-smoke.cjs
```

The test uses Edge with real WebGPU. It compiles the supplied controller, attaches a script,
checks real scripted physics, changes and reloads code, checks compiler-error recovery,
reloads browser storage, backs up/restores code and an imported asset, exports a ZIP, and
runs the extracted game on another server with external requests blocked. Screenshots and
logs are written to `output/web-editor/qa/managed`. The earlier `smoke.cjs` remains useful for
native Inspector editing, scene import/export and Play snapshot restoration.

The website snapshot is the files listed in `runtime-files.json`, plus the manifest itself.
Do not copy unlisted stale build directories. Preserve binary bytes in source control.
