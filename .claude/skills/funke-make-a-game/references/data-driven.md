# Data-driven games with Data Assets

A Data Asset is Funke's ScriptableObject: a C# class deriving from `DataAsset` is the template, and each `.dataasset` file is one filled-in entry with its own GUID. Scripts hold references to entries in serialized fields. The Inspector edits them, and the engine keeps one shared live instance per file.

Agent-made games kept skipping this and wrote stat tables, `switch` ladders and `static readonly` arrays into C# instead. Such a game works, but every balance change becomes a code change, the content is invisible in the Editor, and adding an enemy means touching four scripts. Treat Data Assets as the default container for game content.

Ground truth: `Funke-ScriptCore/Source/Funke/Scene/DataAsset.cs`, `Interop/ScriptInstanceManager.cs` (`MapFieldType`, `FormatFieldValue`, `EncodeListString`, `GetObjectListFields`), `Funke-Engine/src/Scripting/DataAssetManager.cpp`, docs `Github-Docs/Documentation/Assets/Data-Assets.md`.

## When something is a Data Asset

| Situation | Container |
|---|---|
| Several entries of the same kind: enemy types, towers, weapons, items, waves, levels, upgrades, shop offers, dialogue lines, achievements | **Data Asset type**, one file per entry |
| Values a designer will tune: speeds, costs, health, spawn rates, difficulty curves | fields on a Data Asset (shared) or on the script (one entity only) |
| One set of values used by several prefabs or scenes: player stats, economy rules, theme colours | **one** Data Asset that they all reference |
| A reusable *entity* with components and children | **prefab**, which usually references its Data Asset |
| Per-instance values on one entity: this door's target, this platform's path | plain script fields in the scene |
| Runtime state: current health, score, cooldowns | fields on the script instance, **never** in a Data Asset |

## Declaring a type

```csharp
// Assets/Scripts/Data/EnemyData.cs
using Funke;

[CreateDataAsset("Game/Enemy")]                 // Asset Browser: Create > Data Asset > Game/Enemy
public class EnemyData : DataAsset
{
    public string Id = "";                      // stable id for saves and logs (renaming the file keeps the GUID, not the meaning)
    public string DisplayName = "Enemy";
    [ClampValue(1, 10000)] public int MaxHealth = 10;
    public float Speed = 1.5f;
    public int Armor;
    public int Reward = 5;
    public Entity? Prefab;                      // the prefab to spawn ("prefab:<guid>")
    public Texture? Icon;                       // for wave previews / bestiary
    public Audio? DeathSound;
    public EnemyKind Kind;                      // enum -> stored as its integer
    public Graph SpeedOverLife = new();         // curve, Graph.Evaluate(t01)

    public override void OnValidate() { if (Speed < 0) Speed = 0; }
}
```

Supported field types: `bool`, integer types, `float`, `double`, `string`, enums (`[Flags]` too), `Vector2/3/4`, `Color`, `Graph`, asset types (`Texture`, `Audio`, `Material`, `Mesh`, `Scene`, `AnimationClip`, `AnimatorController`), `Entity` (prefab), other `DataAsset` types, `List<T>`/`T[]` of those, a nested plain class/struct (flattened to `"Stats.Hp"`, up to 4 levels), and `List<PlainClass>` inline object lists (below). **Not serialized, without any warning:** `Font`, `Dictionary`, `HashSet`, other generic types, and lists inside list elements.

The class must be non-abstract with a parameterless constructor. Keep it in the global namespace, or use the full name as `type` in the file. Inspector attributes work (`[Header]`, `[ToolTip]`, `[ClampValue]`, `[Space]`).

## The file

```json
{
  "version": 1,
  "type": "EnemyData",
  "fields": {
    "Id": "slime", "DisplayName": "Slime", "MaxHealth": "12", "Speed": "1.25", "Reward": "5",
    "Prefab": "prefab:7300000000000000101", "Icon": "7300000000000000201", "Kind": "0"
  }
}
```

- Path: `Assets/Data/<Type plural>/<Entry>.dataasset` (e.g. `Assets/Data/Enemies/Slime.dataasset`) plus its `.meta`.
- **Every value is a JSON string.** A raw number or bool silently becomes `""`, and the field keeps its default. `funke_game_tool.py validate` flags it.
- Encodings: numbers in invariant culture (`1.25`, never `1,25`), `"true"`/`"false"`, `"x,y"` vectors, `"r,g,b,a"` colours (0..1), enums as integers, asset and Data Asset references as decimal GUID strings, prefabs as `"prefab:<guid>"`, `Graph` as `"px,py,inX,inY,outX,outY;…"`, nested fields as `"Stats.Hp"`.
- Lists: each element prefixed with a newline (`"\nslime\nbat"`; GUID lists the same way). Inline object lists nest that encoding. Don't write them by hand. Use the generator helpers:

```python
import importlib.util, sys
sys.path.insert(0, r"<skill>/scripts"); import funke_game_tool as fg

slime = fg.write_dataasset(P / "Assets/Data/Enemies/Slime.dataasset", "EnemyData",
                           {"Id": "slime", "MaxHealth": 12, "Speed": 1.25, "Prefab": f"prefab:{SLIME_PREFAB}"})
fg.write_dataasset(P / "Assets/Data/Waves/Wave01.dataasset", "WaveData",
                   {"Groups": fg.encode_objects([[str(slime), 6, 0.8, 0.0],        # SpawnGroup: Enemy, Count,
                                                 [str(bat),   4, 0.5, 3.0]]),      #   Interval, Delay (declaration order)
                    "Reward": 25})
```
`write_dataasset` writes the `.meta` first, encodes every value as a string, and by default **never overwrites an existing file** (`overwrite=False`): once someone has tuned the entry in the Inspector, the file is the source of truth.

## Referencing from scripts and scenes

```csharp
public sealed class EnemyHealth : EntityScript
{
    public EnemyData? Data;                     // wired in the prefab's ScriptFields
    public int Current { get; private set; }
    public override void OnAwake() => Current = Data?.MaxHealth ?? 1;   // copy into runtime state
}
```
```json
"ScriptFields": { "EnemyHealth": [
  { "name": "Data", "type": "dataasset:EnemyData", "value": "7300000000000000301" } ] }
```
Lists of Data Assets: `{"name": "Levels", "type": "list", "value": "\n<guid1>\n<guid2>"}`.

Loading in code is fine for catalogs that are meant to be complete: `AssetManager.FindAll<EnemyData>("Data/Enemies")` (every entry under a folder, subclasses included) or `AssetManager.LoadByGUID<T>(guid)`. Prefer a serialized field whenever one script needs specific entries.

## Patterns

### Types of things (enemies, towers, items)
One `XData` type plus one prefab per **look/behaviour family**. The prefab's scripts take `Data` as a field, wired in the prefab or overridden per placed instance with `"Overrides": {"EnemyHealth.Data": "<guid>"}`. A `Class.Field` override only applies when the prefab entity already has a `ScriptFields` row for that field, so give the prefab a default. If several types share a prefab, the spawner instantiates `data.Prefab` and hands the data over: `if (data.Prefab is { } p) Instantiate(p, pos).GetComponent<EnemyHealth>()?.Init(data);`. Stats never live in the prefab's scripts as literals.

### Waves and spawn tables
```csharp
public class SpawnGroup { public EnemyData? Enemy; public int Count = 5; public float Interval = 1f; public float Delay; }

[CreateDataAsset("Game/Wave")]
public class WaveData : DataAsset { public List<SpawnGroup> Groups = new(); public int Reward = 20; }
```
`List<PlainClass>` elements may hold scalars, enums and asset/Data Asset references, but no nested lists. A `WaveSpawner` script walks `Level.Waves` with a coroutine. Balancing waves then means editing files, with no code change.

### Levels and progression
```csharp
[CreateDataAsset("Game/Level")]
public class LevelData : DataAsset
{
    public string Id = "";  public string DisplayName = "";  public Scene? Scene;
    public List<WaveData> Waves = new();  public int StartMoney = 100;
    public int TwoStarScore, ThreeStarScore;  public Texture? Preview;
}

[CreateDataAsset("Game/Level Catalog")]
public class LevelCatalog : DataAsset { public List<LevelData> Levels = new(); }   // explicit order = unlock order
```
The level select reads `LevelCatalog.Levels` and instantiates a card prefab per entry. Progress is saved by `Id` (`PlayerPrefs.SetInt($"best.{level.Id}", stars)` or one `SaveData` object), never by list index, so reordering levels doesn't scramble saves. One catalog asset beats `FindAll` here because the order is explicit and visible in the Inspector.

### Shared rules and theme
`EconomySettings` (start money, sell ratio, interest), `PlayerSettings` (speed, jump, coyote time) and `Theme` (palette colours from `Design/palette.json`, written by the art generator) each exist **once** and are referenced by every script that needs them. A balance pass then touches one file.

### Curves
Difficulty over time, damage falloff and jump arcs go in a `Graph` field (`Curve.Evaluate(t01)`) that is editable in the Inspector, not in a hand-written formula with magic numbers.

## Rules

- **Read-only at runtime.** All holders share one live instance per file, so writing `Data.Speed *= 2` speeds up every enemy of that type, also in later Play sessions until a reload. Copy into runtime state (`m_Speed = Data.Speed * buff`).
- **Content ids are strings** (`Id` field) when they reach saves, logs or analytics.
- **Generated vs tuned:** a generator may create the initial entries (`write_dataasset(..., overwrite=False)`), but it doesn't overwrite them afterwards. If a generator must own a data set (e.g. a procedurally generated level list), say so in `Design/GameBrief.md` and in the generator's docstring.
- **Validate:** `funke validate` checks every `.dataasset`'s `type` and fields, and `funke_game_tool.py validate` adds the string-value check plus the C# lint (content tables in code warn as rule 2).
- Edits to `.dataasset` files reload live in the Editor, even during Play. That makes them the fastest balancing loop there is.
