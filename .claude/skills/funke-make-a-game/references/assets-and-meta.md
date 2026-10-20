# Assets, `.meta` files & GUIDs

Ground truth: `Funke-Engine/src/Assets/AssetRegistry.hpp` (meta read/write, scan, classification), `Assets/TextureMeta.hpp`, `Scripting/DataAssetManager.cpp`, `Funke-ScriptCore/Source/Funke/Interop/ScriptInstanceManager.cs`.

## How references work

Every file under `Assets/` has a sidecar `<file>.meta` (e.g. `Hero.png` → `Hero.png.meta`) holding a 64-bit **GUID**. Scenes, prefabs, data assets and script fields reference assets by that GUID. There is **no database to update** — the `.meta` files are the source of truth (`.funke/asset-registry.cache` is a disposable cache; never edit it, deleting it is safe).

**You may choose the GUID yourself.** Write the asset and its `.meta` together, before the Editor/Runtime scans the folder, and you can use the GUID in JSON immediately. Use the tool:

```bash
python <skill>/scripts/funke_game_tool.py meta Assets/Textures/Characters/Hero.png      # creates Hero.png.meta, prints GUID
python <skill>/scripts/funke_game_tool.py meta Assets/Prefabs/Enemy.prefab              # prefabs, scenes, dataassets too
python <skill>/scripts/funke_game_tool.py list <ProjectRoot>                             # every asset → GUID (incl. existing metas)
```
The tool never overwrites an existing valid `.meta` (it prints the existing GUID instead) — existing GUIDs are already referenced by something.

## `.meta` schema

```json
{
  "AssetGUID": "5079411902368823170",
  "uuid": "5079411902368823170",
  "kind": "Texture"
}
```
- `AssetGUID` and `uuid`: identical **decimal strings**, plain digits only, non-zero. Pick from `[1, 2^63-1]` (avoids the built-in `0xAA…`/`0xAB…` ranges, which resolve first).
- `kind` is informational (`Texture`, `Audio`, `Font`, `Scene`, `Prefab`, `DataAsset`, `Script`, `Shader`, `Other`); the real kind comes from the extension.
- **A malformed `.meta` is not an error — it is silently replaced** with a fresh random GUID (losing `import`/`sprites`). Every reference to the old GUID then dangles. Write strict JSON.
- **Duplicate GUIDs** (e.g. a copied meta) are "fixed" silently by re-rolling one of them; which one is arbitrary. Never copy `.meta` files.
- Changing a GUID breaks all references to it. Don't.

### Texture import settings (optional)
```json
{
  "AssetGUID": "…", "uuid": "…", "kind": "Texture",
  "import": { "filter": "Point", "wrapU": "Clamp", "wrapV": "Clamp" },
  "sprites": [
    { "name": "hero_run_0", "rect": [0, 0, 32, 32], "pivot": [0.5, 0.5] },
    { "name": "hero_run_1", "rect": [32, 0, 32, 32], "pivot": [0.5, 0.5] }
  ]
}
```
- `filter`: `Point` (pixel art) | `Bilinear` | `Trilinear` | `Anisotropic`. `wrapU/V`: `Repeat` | `Clamp` | `Mirror` | `Border` (case-sensitive). Without an `import` block a `SpriteRenderer` samples Point and an `Image` Bilinear (Clamp). Pixel art: save it 4× nearest and import `Bilinear` (`funke-game-art` → "sharp bilinear").
- `sprites`: named slices, `rect` = `[x, y, w, h]` in pixels from the **top-left**, `w`/`h` > 0. Select a slice with `"spriteName"` on `SpriteRenderer`/`Image`; animate with `SpriteAnimator2D.frames` (list of slice names). `pivot` = `[x, y]`, 0..1 from the slice's **bottom-left**.
- By default on-screen size = `Transform2D` scale (scale 1 = 1 world unit; set scale to the sprite's aspect ratio). A `SpriteRenderer` with `"pixelsPerUnit"` > 0 instead draws the slice at its native size (px / pixelsPerUnit × scale) on its pivot — use that for Unity-style art where frames differ in size or stand on a foot pivot.

Audio and font metas have no import settings (volume/pitch/loop live on `AudioSource`, size on `TextRenderer.fontSize`).

## Supported formats

| Kind | Extensions |
|---|---|
| Texture | `.png .jpg .jpeg .bmp .tga` |
| Audio | `.wav .mp3 .flac` — **`.ogg` is recognized but cannot be decoded** |
| Font | `.ttf .otf` |
| Scene / Prefab / Data | `.scene` / `.prefab` / `.dataasset` |

## Built-in assets (no files needed)

Engine assets in `FunkeAssets/` have fixed GUIDs and need no `.meta`. Ideal for prototyping — tint them with the renderer color.

| Built-in sprite | GUID |
|---|---|
| Square | `12290035569547768290` |
| Circle | `12278967664992971869` |
| Capsule | `12290210590699758690` |
| Pixel | `12320886721272979873` |
| IsometricDiamond | `12269503932229824646` |
| HexagonFlatTop | `12283288399142820423` |
| HexagonPointedTop | `12286582881878784879` |
| 9Sliced | `12291056940125391100` |
| Invisible | `12297742813535580454` |

- Default font: `12321848580485677057` — what `TextRenderer` uses when `fontAsset` is omitted. The set of built-in font files is changing, so **ship the game's fonts in `Assets/Fonts/`** (with licence) and set `"defaultFontAsset"` in `project.json`.
- Any other file in the engine's `FunkeAssets/` folder (e.g. `Textures/Default/SquareBorder.png`, `Spike.png`) has a path-hash GUID: `python funke_game_tool.py builtin Textures/Default/SquareBorder.png`.
- Prefer the user's own assets once they exist; built-ins are placeholders.

## Asset references in JSON (wired before runtime)

| Where | Key | Value |
|---|---|---|
| `SpriteRenderer`, `Image`, `ParticleSystem2D` | `textureAsset` | texture GUID string (+ optional `spriteName`) |
| `AudioSource` | `clipAsset` | audio GUID string |
| `TextRenderer` | `fontAsset` | font GUID — **must be a string** |
| UI widget sprite states | `normalSprite`, `hoveredSprite`, … | texture GUID string |
| Prefab instance | `PrefabGUID` | prefab GUID string |
| Script field `Texture` / `Audio` / `Scene` | `ScriptFields` `value` | GUID string |
| Script field `Entity` (prefab) | `ScriptFields` `value` | `"prefab:<GUID>"` |
| Script field `DataAsset` subclass | `ScriptFields` `value` | GUID string |

Legacy path keys (`"texture": "Default/Pixel.png"`, `"clip"`) still load but resolve relative to the working directory — don't use them.

### C# field types that can be wired from JSON
`Texture`, `Audio`, `Scene`, `Material`, `Mesh`, `AnimationClip`, `AnimatorController`, `Graph`, `Entity` (scene entity or prefab), any `Component`/`EntityScript` subclass and built-in component wrapper, any `DataAsset` subclass, and `List<T>`/`T[]` of these. **Not** wireable: `Font` (put it on the `TextRenderer`), and the types `FontRef`/`SceneRef`/`PrefabRef` don't exist (`TextureRef`/`AudioRef` are internal) despite what older docs say.

```csharp
public class BrickHealth : EntityScript
{
    public int Hits = 1;
    public Audio? BreakSound;     // wired: "type": "audio",  "value": "<GUID>"
    public Entity? DebrisPrefab;  // wired: "type": "entity", "value": "prefab:<GUID>"
}
```

## Data Assets (`.dataasset`)

Tables of game content (enemies, items, waves, levels) and shared tuning live in Data Assets, not in C#. Class, file format, list encodings, generator helpers and patterns: **`data-driven.md`**. The short version: `public class EnemyData : DataAsset { … }`, one `Assets/Data/Enemies/Slime.dataasset` per entry (`{"version":1,"type":"EnemyData","fields":{"MaxHealth":"12"}}`, every value a string), referenced from scripts as `"type": "dataasset:EnemyData", "value": "<GUID>"`.

## Shipping notes
- Builds ship only what the game can reach: build scenes and everything they reference (by GUID, path or scene name, followed recursively), paths in `project.json`, folders/files named by string literals in C#, `AssetManager.FindAll<T>("Prefix")` targets, and all of `Assets/StreamingAssets/`. Files found through a **computed** path (`"Level" + i + ".json"`) are not detected, so put them in `Assets/StreamingAssets/`. Scenes/prefabs are converted to binary in the output (`Github-Docs/Documentation/Editor/Building-and-Shipping.md`).
- Keep `.meta` files in version control (the Launcher's `.gitignore` already does).
