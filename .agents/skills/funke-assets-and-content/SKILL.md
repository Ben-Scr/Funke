---
name: funke-assets-and-content
description: Use when working with GAME CONTENT in a Funke project — textures and sprites, audio, fonts, materials, meshes, animation assets, the GUID-based asset system that ties them together, and Data Assets (Funke's ScriptableObject equivalent for shared game data). Triggers on "asset", "GUID", ".meta file", "sprite slice", "AssetManager.Load / LoadByGUID / FindAll / GetGUID", "Texture.FromAssetUUID / runtime texture / SetPixel", "Audio.FromAssetUUID / PlayOneShot", "asset slot in inspector", "Data Asset", "[CreateDataAsset]", "OnValidate", ".dataasset", "item/stats/loot/dialogue data", or how content is found, referenced, loaded, cached and shipped. The source of truth is `Funke-ScriptCore/Source/Funke/Core/AssetManager.cs`, `Graphics/Texture.cs`, `Audio/Audio.cs`, `Scene/DataAsset.cs` and `Github-Docs/Documentation/Assets/`. Inspector field rules are in `funke-inspector-ruleset`, on-disk layout in `funke-project-structure`.
---

# Funke Assets & Content

Content in Funke is **discovered, not imported**: drop a file into the project's `Assets/` folder and the engine assigns it a permanent **GUID**. Everything references content by GUID, never by path, so you can move/rename freely. This skill covers the asset model, how to reference and load content from C#, the per-type asset classes, and **Data Assets** (shared game data).

## The asset model

- **No import wizard.** Files in `Assets/` are scanned and classified by extension into an `AssetKind`:

  | Kind | Extensions | Use |
  |---|---|---|
  | Texture | `.png .jpg .jpeg .bmp .tga` | sprites/images |
  | Audio | `.wav .mp3 .ogg .flac` (Ogg Vorbis; Opus can't be decoded) | sfx/music |
  | Font | `.ttf .otf` | text |
  | Scene | `.scene` | a level/screen |
  | Prefab | `.prefab` | reusable entity |
  | Script | `.cs` (+ C/C++) | game logic |
  | Shader | `.wgsl .vert .frag …` | custom rendering |
  | Data Asset | `.dataasset` | shared game data |
  | Material | `.material` | shader + parameters |
  | Mesh | `.obj .gltf .glb` | 3D models |
  | AnimationClip | `.anim` | keyframe animation |
  | AnimatorController | `.animctrl` | animation state machine |
  | Other | anything else | tracked, unhandled |

- **GUID + `.meta`.** Each asset gets a 64-bit GUID stored in a `<file>.meta` JSON sidecar (GUID + import settings like texture filter/wrap and sprite slices). **Keep and commit `.meta` files; move them with their asset.** A regenerated `.meta` gets a *new* GUID and breaks existing references.
- **Built-in engine assets** (default font, default white/circle/etc. textures) have fixed stable GUIDs identical on every machine.
- **File-based runtime assets.** Textures/audio are loaded on demand and cached behind handles. The build stages project content, can prune unused assets, and converts staged scenes, prefabs and Data Assets to binary; platform packaging may then bundle those files (for example, inside an Android APK). The source assets remain editable. Build details: `funke-build-and-ship`.

## Referencing content from C#

Two idioms — prefer the first.

**1. Inspector slot (the normal way).** Declare a typed field; the Editor shows an asset picker; the engine stores the GUID with the scene. Drag the asset onto the slot.

```csharp
public class Pickup : EntityScript
{
    public Texture? Icon;      // texture picker
    public Audio?   PickupSfx; // audio picker

    public override void OnStart()
    {
        if (Icon != null) GetComponent<SpriteRenderer>()!.Texture = Icon;
    }
}
```
(Slot/attribute rules — `[ShowInEditor]`, read-only, etc. — are in `funke-inspector-ruleset`.)

**2. Load in code via `AssetManager` (static).**

```csharp
T? Load<T>(string path) where T : class;          // path resolved to a GUID internally
T? LoadByGUID<T>(ulong guid) where T : class;     // also a string-GUID overload
Task<T?> LoadAsync<T>(string path, CancellationToken cancellationToken = default) where T : class;
Task<T?> LoadByGUIDAsync<T>(ulong guid, CancellationToken cancellationToken = default) where T : class; // also a string-GUID overload
IReadOnlyList<T> FindAll<T>(string pathPrefix = "") where T : class;  // every asset of a kind, optionally under a folder
ulong GetGUID(string path);                        // path → GUID
```
```csharp
Texture? tex = AssetManager.Load<Texture>("Sprites/player.png");
var allItems = AssetManager.FindAll<ItemData>();              // great for building catalogs at startup
```

Under the hood everything is GUID-based; the path form just looks up the GUID. References saved in scenes/prefabs/components are always GUIDs. Paths are relative to `Assets/`. `T` can be `Texture`, `Audio`, `Font`, `Material`, `Mesh`, `AnimationClip`, `AnimatorController`, a `DataAsset` subclass, or `Entity` for a prefab; anything else returns `null` / empty.

**Async loading** supports `Texture`, `Audio`, `Font` (32px cache bucket) and `Mesh`. Start it on the engine thread: `Texture? texture = await AssetManager.LoadAsync<Texture>("Sprites/player.png", token);`. File reading and decoding/rasterization run on workers; GUID/metadata resolution, cache publication and GPU uploads remain on the main thread. Failure returns `null`, cancellation cancels the task, and unsupported types throw `NotSupportedException`.

**Interop cost:** synchronous `AssetManager.Load/LoadByGUID` combines resolution, expected-kind checks and any required loading in one native entry for common asset reference types, then creates the managed wrapper directly; reference-only types remain lazy. Async loading uses one native begin call per asset, no per-asset status polling, and one native-to-managed callback for each ready batch; cancellation is batched too. Native request release is automatic for C# completion batches. Data Assets retrieve the already-loaded shared managed instance with subtype checking, without another `EnsureLoaded` call or another `OnValidate` invocation.

## Asset classes (`Funke.*`)

### `Texture`
```csharp
ulong UUID;  bool IsValid;  bool IsRuntime;  string Name;  string Path;  int Width;  int Height;
static Texture? FromAssetUUID(ulong assetId);
// Built-in shapes (stable GUIDs): Texture.Square / Pixel / Circle / Capsule /
//   IsometricDiamond / HexagonFlatTop / HexagonPointedTop / NineSliced / Invisible
// Procedural runtime textures:
static Texture? Create(int w, int h, TextureFilter filter = TextureFilter.Point);
void SetPixel(Vector2Int cell, Color c);  Color GetPixel(Vector2Int cell);
void SetPixels(Color[] rowMajor);  Color[] GetPixels();  void Apply();   // upload CPU buffer → GPU
bool SaveToPng(string path);  void Dispose();                            // runtime textures aren't GC'd — Dispose them
```
`TextureFilter`: `Point | Bilinear | Trilinear | Anisotropic | Default`.

### `Audio`
```csharp
ulong UUID;  bool IsValid;  string Name;  string Path;
static Audio? FromAssetUUID(ulong assetId);
void PlayOneShot(float volume = 1.0f);     // fire-and-forget
```
For looping/positional/controllable playback use the **`AudioSource`** component (`Play/Pause/Stop/Resume`, `Volume`, `Pitch`, `Loop`) — see `funke-game-scripting`.

### `Font`
```csharp
ulong UUID;  bool IsValid;  string Name;  string Path;
static Font? FromAssetUUID(ulong assetId);
```
Assign to a `TextRenderer.Font`. Fonts are cached per pixel size.

`TextureRef` / `AudioRef` are thin GUID wrappers used in serialization; you normally just use the `Texture` / `Audio` field types directly.

## Data Assets — shared game data (ScriptableObject equivalent)

When many objects should read the **same** data edited in **one** place (item/enemy/stat definitions, loot tables, dialogue, difficulty presets), use a **Data Asset**. The class is the template; each `.dataasset` file is one filled-in instance with its own GUID. The engine keeps **one shared live instance per file**.

### Define the class

```csharp
using Funke;

[CreateDataAsset("Gameplay/Item Data")]   // adds a Create → Data Asset → Gameplay/Item Data menu entry
public class ItemData : DataAsset
{
    public string DisplayName = "New Item";
    public int    MaxStack    = 99;
    [ClampValue(0f, 1000f)] public float Weight = 1.0f;
    public Color   Tint = Color.White;
    public Texture? Icon;
    public RarityData? Rarity;            // a Data Asset can reference another Data Asset

    public override void OnValidate()     // runs after edit-in-editor or load-from-disk
    {
        if (MaxStack < 1) MaxStack = 1;
    }
}
```
- Base class: `public abstract class DataAsset` (in `Funke-ScriptCore/Source/Funke/Scene/DataAsset.cs`); override `OnValidate()`.
- `[CreateDataAsset(menuPath)]` (`menuPath` is slash-separated; default `""`). Without it the class still works but won't appear in the create menu.
- **Field types** = the same set as script fields: primitives, `string`, enums, `Vector2/3/4`, `Color`, `Graph`, `Texture`/`Audio`/`Material`/`Mesh`/`Scene`, prefabs (`Entity`), **other `DataAsset` subclasses**, lists of those and `List<PlainClass>` inline objects. `Font`, `Dictionary` and other generic types are silently not serialized. Inspector attributes apply (`[ClampValue]`, `[Header]`, `[ToolTip]`, …).

### Create & reference

- **Editor:** Asset Browser → right-click → **Create → Data Asset → <your menu path>** → name it → edit fields in the Inspector.
- **From a script (slot):** `public ItemData? Item;` shows a Data Asset picker; reference saved by GUID.
- **From code:** `AssetManager.Load<ItemData>("Items/Sword.dataasset")` or `AssetManager.FindAll<ItemData>("Items/Weapons")`.

### On disk
A `.dataasset` stores the class `type` name, a format `version`, and the field values (JSON or binary per the project's `assetSerializationFormat`). Hot reload re-creates instances from disk and replays field values (creating all first, so cross-references resolve).

File format, list encodings, generator helpers and patterns (enemy types, waves, levels, catalogs): `funke-make-a-game` → `references/data-driven.md`.

**Data Asset vs alternatives:** shared data decoupled from entities → **Data Asset**; a reusable *entity* with components/children → **Prefab**; per-instance values on one entity → **plain script fields**.

## Hot reload & purging (editor)

- Editing a `.png`/`.ttf`/`.wav` on disk re-decodes and re-uploads it live; Asset Browser thumbnails refresh. The shipped runtime reads files once (no hot reload).
- Managers cache loaded assets behind generation-checked handles. Texture purging preserves built-ins, runtime textures, direct scene references, indirect material/3D-particle texture references, and registered reference providers. Runtime textures remain owned by their script `Texture` objects and must be disposed explicitly.

## Pitfalls

- **Never hard-code GUIDs or fragile paths** when an Inspector slot will do — slots keep references stable across moves/renames.
- **Lost `.meta` = broken references.** Treat `.meta` as part of the asset; commit it.
- **Runtime `Texture` objects must be `Dispose()`d** — they hold a GPU resource and are not garbage-collected.
- **New projects serialize as JSON**; older projects (or a `project.json` without `assetSerializationFormat`) are Binary and their `.dataasset`/`.scene` files aren't human-readable until converted (`funke convert --format json`, see `funke-project-structure`).
- **`SpriteRenderer.Color` is a `Vector4`**, but `Color` (struct, with presets like `Color.White`) is what most APIs and Data Asset fields use — convert deliberately.

## Related skills
`funke-game-scripting` (using assets at runtime), `funke-inspector-ruleset` (asset-slot attributes), `funke-project-structure` (on-disk layout & formats), `funke-build-and-ship` (staging, pruning and platform packaging).
