---
name: funke-inspector-ruleset
description: Use when exposing fields to the Funke editor inspector — native C++ components (`PropertyDescriptor`, `Properties::Make*`, `Properties::Meta`) or C# script fields (`[ShowInEditor]`, `[ClampValue]`, `[EnabledIf]`, `[Header]`, `[EditorIgnore]`). Triggers on "PropertyType", "PropertyDrawer", "ReferencePicker", "inspector field", "multi-select inspector", "mixed value", "asset picker", "entity reference", "EntityRef", "ComponentRef", "AssetRef", "FlagEnum", "MakeTextureRef", "MakeEntityRef", "MakeVariant", "DeclareComponentConflict", "SetEditorGizmo", or work in `Funke-Engine/src/Inspector/`, `Funke-Editor/src/Inspector/`, `Funke-ScriptCore/Source/Funke/Attributes/`. Covers the field-type-to-PropertyType table, multi-select rules, reference pickers and the EntityRef entity/prefab split, lists, variants, metadata, C# attributes, conflicts and gizmos. Don't guess these from memory — mistakes cause silent mixed-state corruption or wrong picker filters.
---

# Funke Inspector Ruleset

The Funke editor inspector is **declarative and unified**. Both native C++ components and C# scripts feed the same `PropertyDescriptor` shape through the same `PropertyDrawer`. There are no per-type drawer implementations — adding a new supported type is a single change to `PropertyType` plus a switch arm in `PropertyDrawer.cpp` / `PropertyValue.cpp`. So the API is small, but the rules around it (multi-select, mixed values, reference filtering, variants, lists) are exact and non-obvious. This skill is the ruleset.

Source of truth lives in:
- `Funke-Engine/src/Inspector/PropertyType.hpp` — the canonical type list and wire tags; multi-select behavior lives in `PropertyDrawer.cpp`.
- `Funke-Engine/src/Inspector/PropertyRegistration.hpp` — the `Properties::Make*` and `Properties::Meta::*` helpers.
- `Funke-Engine/src/Inspector/PropertyMetadata.hpp` — clamp / tooltip / header / enabled-if / etc.
- `Funke-Editor/src/Inspector/PropertyDrawer.cpp` — the per-type widget dispatch.
- `Funke-Editor/src/Inspector/ReferencePicker.cpp` — the unified modal picker.
- `Funke-ScriptCore/Source/Funke/Attributes/` — the C# attribute set.
- `Funke-Engine/src/Packages/PackageRegistration.hpp` — `RegisterSerializableComponent`, `DeclareComponentConflict`, `SetEditorGizmo`.

## PropertyType — the canonical table

Every inspectable field maps to exactly one `PropertyType`. The drawer, serializer, and reference picker all dispatch on this enum.

| PropertyType | Native C++ field type | C# field type | Widget |
|---|---|---|---|
| `Bool` | `bool` | `bool` | Tri-state checkbox |
| `Int8` | `int8_t` | `sbyte` | DragInt, clamped `[-128, 127]` |
| `Int16` | `int16_t` | `short` | DragInt, clamped |
| `Int32` | `int32_t` / `int` | `int` | DragInt |
| `Int64` | `int64_t` | `long` | InputScalar (S64) |
| `UInt8` | `uint8_t` | `byte` | DragInt, clamped `[0, 255]` |
| `UInt16` | `uint16_t` | `ushort` | DragInt, clamped |
| `UInt32` | `uint32_t` | `uint` | DragInt, non-negative |
| `UInt64` | `uint64_t` | `ulong` | InputScalar (U64) |
| `Float` | `float` | `float` | DragFloat |
| `Double` | `double` | `double` | DragScalar (Double) |
| `String` | `std::string` | `string` | InputText (or multi-line if metadata set) |
| `StringList` | (via `MakeStringList`) | — (C# `List<string>` maps to `List`) | Row-per-entry text input + add/remove |
| `List` | `std::vector<TItem>` (via `MakeList`) | `List<T>` / `T[]` (incl. `List<string>`) | Row-per-item primitive widget + add/remove |
| `Vec2` / `Vec3` / `Vec4` | `glm::vec2/3/4` | `Vector2/3/4` | 2/3/4-channel DragFloat |
| `IntVec2` / `IntVec3` / `IntVec4` | `glm::ivec2/3/4` | `Vector2Int/3Int/4Int` | 2/3/4-channel DragInt |
| `Color` | `Color` | `Color` | ColorEdit4 |
| `Enum` | `enum class` | `enum` | Combo (single value) |
| `FlagEnum` | `enum class` (bitflags) | `[Flags] enum` | Multi-checkbox combo |
| `TextureRef` | asset GUID as `uint64_t` (via `MakeTextureRef`) | `Texture` | Reference picker (filtered to Texture) |
| `AudioRef` | asset GUID (via `MakeAudioRef`) | `Audio` | Reference picker (Audio) |
| `FontRef` | asset GUID (via `MakeFontRef`) | — (no C# field support) | Reference picker (Font) |
| `AssetRef` | asset GUID (hand-built descriptor + `Metadata.AssetKindFilter`) | `Material`, `Mesh`, `AnimationClip`, `AnimatorController`, `DataAsset` subclasses (wire tags `material`, `mesh`, `animationclip`, `animatorcontroller`, `dataasset:<Type>`) | Reference picker (filtered by kind; `ScriptAssetKindForTag` in `Funke-Editor/src/Scripting/ScriptComponentInspector.cpp` maps the C# tags) |
| `SceneRef` | asset GUID (hand-built) | `Scene` | Reference picker (Scene) |
| `EntityRef` | persistent entity id (`Scene::GetEntityPersistentID`, uint64) **or** prefab GUID | `Entity` (also holds prefabs) | Reference picker (Entity, polymorphic — see below) |
| `PrefabRef` | asset GUID (hand-built) | — (C# prefab refs are `Entity` fields → `EntityRef`) | Reference picker (Prefab) |
| `ComponentRef` | `"<entityId>:<typeName>"` | `Component` subclass (wire tag `component:<displayName>`) | Reference picker (Component) |
| `Graph` | — (no native helper) | `Graph` (`Funke.Graph`) | Inline curve editor (keys ride in `StringValue`) |

**The display label for empty / cleared reference fields is `(None)`** (`k_NoneLabel` in `PropertyType.hpp`). Re-themed in one place if needed — don't hardcode the string elsewhere.

## Native C++ side: declarative descriptors

The `Properties::Make*` family in `PropertyRegistration.hpp` builds `PropertyDescriptor` values from pointer-to-member or getter/setter pairs. The pointer-to-member overload is the common case — type is deduced via `DeducePropertyType<TField>()`, the `Get` / `Set` lambdas are synthesized.

### Pointer-to-member (`Make`)

The 95% case. Works for `bool`, all integer / float types, `std::string`, `glm::vec*` / `glm::ivec*`, `Color`, and **enums** (auto-builds the `EnumDescriptor` via magic_enum).

```cpp
std::vector<PropertyDescriptor> props = {
    Properties::Make("SortingOrder", "Sorting Order", &SpriteRendererComponent::SortingOrder),
    Properties::Make("Filter", "Filter", &SpriteRendererComponent::FilterMode), // enum auto-handled
};
```

The first argument is the stable property **name** — the inspector key, the Animator property path (`AnimationBinding.cpp`) and, by convention, the C# field name. It is **not** the scene JSON key: a native component's JSON keys come only from its `serialize`/`deserialize` (Transform2D's `Position` descriptor is stored as `posX`/`posY`). The second is the **display label** in the inspector. Pass empty for `displayName` to reuse `name`.

### Getter/setter (`MakeWith`)

When the field is exposed via methods (e.g. `Rigidbody2D::GetPosition` / `SetPosition`) or the setter has side effects (re-upload to physics body, dirty-flag scene state, etc.):

```cpp
Properties::MakeWith<float>("Mass", "Mass",
    [](const Entity& e) { return e.GetComponent<Rigidbody2DComponent>().GetMass(); },
    [](Entity& e, float v) { e.GetComponent<Rigidbody2DComponent>().SetMass(v); });
```

The explicit `<float>` template arg is the field type — used for `DeducePropertyType` and `Box`/`Unbox`. The drawer calls the getter / setter **once per entity in the multi-selection** inside its span.

### Flag enums (`MakeFlagEnum`)

Regular `Make` treats every enum as a single-value Combo. For bitflag enums (collision masks, layer masks, etc.), use `MakeFlagEnum`:

```cpp
Properties::MakeFlagEnum("Layers", "Collision Layers",
    &Collider2DComponent::Layers);
```

The drawer renders a multi-checkbox combo so individual bits can be toggled. Each bit is sampled independently across multi-selection — a bit uniformly on shows checked, uniformly off shows unchecked, mismatched shows the ImGui mixed-value flag.

### Reference fields

Each asset kind has an explicit helper so the picker knows which kind to filter for. **Don't register references with plain `Make`**: handle/GUID types (`TextureHandle`, `AudioHandle`, `FontHandle`, `UUID`/`AssetGUID`) deduce to `PropertyType::None` and fail `Make`'s `static_assert`, and a raw `uint64_t` GUID member compiles but deduces to `UInt64` — a number widget instead of a picker.

```cpp
Properties::MakeTextureRef("Texture", "Texture",
    [](const Entity& e) -> uint64_t { return e.GetComponent<ParticleSystem3DComponent>().Texture; },
    [](Entity& e, uint64_t guid) { e.GetComponent<ParticleSystem3DComponent>().Texture = guid; });

Properties::MakeAudioRef("Clip", "Clip", /* getter */, /* setter */);
Properties::MakeFontRef ("Font", "Font", /* getter */, /* setter */);
Properties::MakeEntityRef("Target", "Follow Target", /* getter */, /* setter */);
```

The getter must return a `uint64_t`: the asset GUID, or for `MakeEntityRef` the **persistent** entity id (`Scene::GetEntityPersistentID`, resolved back with `Scene::TryResolveEntityRef`) — never a runtime/entt id; the picker, drag-drop and display all use persistent ids. `MakeUiEntityRef` in `Scene/BuiltInComponentRegistrationShared.hpp` is the pattern to copy. The setter receives the new ID and is responsible for whatever side-effects loading or attaching the reference requires.

For sprite slices, `MakeTextureRef` also accepts a getter `(const Entity&, std::string& outSlice)` and setter `(Entity&, uint64_t, const std::string&)`. Use both together so comparison, animation recording, and preview restoration retain the slice. Copy `MakeTextureRefDirect` in `BuiltInComponentRegistrationRendering.cpp` for SpriteRenderer/Image: it maintains `TextureAssetId`, `TextureHandle`, and `SpriteName` together and rejects failed texture loads.

There's no `MakeAssetRef` / `MakeSceneRef` / `MakePrefabRef` / `MakeComponentRef` helper today — for those, build the `PropertyDescriptor` directly with `Type = PropertyType::AssetRef` (etc.) and populate the lambdas by hand. Set `PropertyMetadata::ComponentTypeName` for `ComponentRef` so the picker can filter to compatible components.

### Lists

Two variants:

- **`StringList`** — ordered list of strings, edited as one row per entry plus an "Add row" button. Use `Properties::MakeStringList(name, displayName, getter, setter, metadata)`.
- **`List`** — ordered list of any *primitive* PropertyType (int, float, vec, color, enum, string). The drawer renders the matching per-type widget per row.

```cpp
// Direct member access — vector<T> on the component:
Properties::MakeList<TileLayerComponent, int>("Tiles", "Tile IDs",
    &TileLayerComponent::Tiles);

// Custom getter/setter (e.g. when the storage is computed):
Properties::MakeListWith<float>("Speeds", "Speeds",
    [](const Entity& e) { return e.GetComponent<Comp>().GetSpeeds(); },
    [](Entity& e, const std::vector<float>& v) { e.GetComponent<Comp>().SetSpeeds(v); });
```

`StringList` exists separately because it predates the generic `List` and several serializers still depend on the legacy wire shape — don't migrate existing `StringList` fields without checking the serializers.

### Variants

A variant descriptor renders a discriminator combo at the top, then — if the discriminator is uniform across the selection — the matching branch's nested descriptors directly under the row (indented). Mixed discriminators render a `(mixed)` hint instead of any branch.

```cpp
Properties::MakeVariant<EmitterComponent, EmitterShape>(
    "Shape", "Shape",
    &EmitterComponent::Shape,    // enum discriminator
    {
        Properties::Branch(EmitterShape::Circle, {
            Properties::Make("Radius", "Radius", &EmitterComponent::Radius),
        }),
        Properties::Branch(EmitterShape::Box, {
            Properties::Make("Size", "Size", &EmitterComponent::Size),
        }),
    });
```

`MakeVariantWith` is the getter/setter variant — useful when the discriminator is computed from a `std::variant` via `std::visit` + tag swap rather than living as a direct enum field.

### Metadata builders (`Properties::Meta`)

Each helper returns a fresh `PropertyMetadata` configured with one common shape; the `PropertyMetadata::With*` chain methods compose more on top.

```cpp
Properties::MakeWith<float>("Volume", "Volume",
    [](const Entity& e) { return e.GetComponent<AudioSourceComponent>().GetVolume(); },
    [](Entity& e, float v) { e.GetComponent<AudioSourceComponent>().SetVolume(v); },
    Properties::Meta::Clamp(0.0, 1.0).WithDragSpeed(0.01f).WithTooltip("Linear gain."));

Properties::Make("Gravity", "Gravity Vector", &Settings::Gravity,
    Properties::Meta::EnabledIf<Settings>([](const Settings& s) { return s.UseGravity; }));

Properties::Make("Description", "Description", &NpcComponent::Description,
    Properties::Meta::MultiLine(8));
```

Available helpers (full list in `Properties::Meta` in `PropertyRegistration.hpp`):

| Helper | Purpose |
|---|---|
| `Clamp(min, max)` | Numeric clamp (drag is bounded). |
| `Clamp(min, max, dragSpeed)` | Clamp with explicit drag speed. |
| `DragSpeed(speed)` | Drag-speed alone (no clamp). |
| `Tooltip(text)` | Hover tooltip. |
| `ReadOnly()` | Render disabled, edits ignored. |
| `NoSteppers()` | Hide the numeric stepper buttons. |
| `Header(content, size=5)` | Section header drawn ABOVE this row. |
| `Space(h)` | Vertical spacer above the row. |
| `MultiLine(rows=4)` | `String` only — render as a resizable multi-line text box. |
| `EnabledIf<TComponent>(predicate)` | Gate by a per-component predicate. |
| `EnabledIf(entityPredicate)` | Gate by a raw `Entity`-keyed predicate. |

The `EnabledIf` gate is evaluated against every entity in the selection — if any returns false, the row renders disabled (greyed out, edits ignored). Same gate path serves both native components and C# scripts.

For section reset support (right-click "Reset to defaults" on a section header), pass a `WithResetSection(callback)` to the `Header`-bearing descriptor. The PostProcessing component is the canonical user.

## C# side: attributes

Managed scripts (`EntityScript`, `SceneSystem`, `GlobalSystem`, and any other `Component` subclass) use attributes from `Funke-ScriptCore/Source/Funke/Attributes/` to surface fields. The same `PropertyDrawer` renders them — the JSON wire format between the C# reflection pass and the editor is the `PropertyType` string from `ToString(PropertyType)` (e.g. `"float"`, `"vector2"`, `"texture"`).

### The attribute table

| Attribute | Purpose | Native equivalent |
|---|---|---|
| `[ShowInEditor]` | Surface a field that wouldn't be visible by default (e.g. private fields). Also accepts `displayName` and `readOnly`. | (implicit — descriptors are explicit) |
| `[ShowInEditor("Display Name")]` | Override the inspector label. | `Properties::Make(name, "Display Name", …)` |
| `[ShowInEditor(readOnly: true)]` | Inspector renders the row disabled. | `Properties::Meta::ReadOnly()` |
| `[ClampValue(min, max)]` | Numeric clamp. | `Properties::Meta::Clamp(min, max)` |
| `[EnabledIf("OtherField")]` | Disable this row whenever the named field is "falsy" (zero / empty / false). | `Properties::Meta::EnabledIf<...>(...)` |
| `[EnabledIf("OtherField", expectedValue)]` | Disable this row whenever the named field doesn't equal the expected value. | same |
| `[Header("Section")]` | Section header above this row. | `Properties::Meta::Header("Section")` |
| `[Space(h)]` | Vertical spacer above this row. | `Properties::Meta::Space(h)` |
| `[ToolTip("text")]` | Hover tooltip. | `Properties::Meta::Tooltip("text")` |
| `[EditorReadOnly]` | Render disabled, edits ignored. | `Properties::Meta::ReadOnly()` |
| `[EditorIgnore]` / `[HideFromEditor]` | Hide a field that *would* be visible. | (omit from the descriptor list) |

### Field visibility default

For a managed script, public fields and properties with a public getter are shown automatically, including computed properties. Properties without a public setter appear read-only; indexers and write-only properties are skipped. `[ShowInEditor]` surfaces non-public members. Use `[EditorIgnore]` (equivalently `[HideFromEditor]`) to hide a member that would otherwise be shown. `CollectEditorMembers` and `IsMemberEditorVisible` in `Interop/ScriptInstanceManager.cs` define these rules.

### Cross-field gating semantics (`EnabledIf`)

Mirrors `PropertyMetadata::EnabledIfFn` exactly. The expected value is compared against the named field's editor wire representation — bool, integer, float, string, and enum fields all work:

```csharp
public bool UseGravity;

[ShowInEditor, EnabledIf(nameof(UseGravity), true)]
public Vector2 Gravity;
```

When `expectedValue` is null (single-arg form), the gate passes for any truthy value. **Use `nameof(...)`** so renames don't silently disable the gate.

## Multi-selection rules

The drawer always operates on a **span of entities**, even when only one is selected (the single-select case is just `size == 1`). When the selection contains more than one entity, `PropertyDrawer.cpp` follows these rules:

- **Scalars (Bool, integers, Float, Double, String, Enum):**
  - All entities equal → widget shows the value normally.
  - Mismatched → widget shows `—` (or the ImGui mixed flag for Bool / FlagEnum).
  - Editing writes the new value to **all** selected entities.
- **Vectors / Color (Vec*, IntVec*, Color):**
  - Mixed state is **per-channel**. Editing only the changed channel writes that channel to all entities; untouched channels are left alone per-entity. This is critical — bulk-editing a `Position.x` does NOT clobber `Position.y` on entities whose Y differed.
- **References (Texture/Audio/Asset/Scene/Entity/Prefab/Component):**
  - All entities equal → button shows the asset/entity name + path.
  - Mismatched → button shows `—` with a tooltip listing "Multiple values".
  - Picker / drag-drop / clear writes to **all** selected entities.
- **FlagEnum:**
  - Each flag bit is sampled independently. Uniformly on/off shows checked/unchecked; differing across entities renders the bit with the ImGui mixed-value flag.

A getter/setter pair written by the user must respect this contract — the drawer calls them per-entity inside the span. Don't cache state in the getter; don't assume the setter is called once per "edit" (it's once per entity).

## Reference picker behaviour

All reference types share the same picker UX (`Funke-Editor/src/Inspector/ReferencePicker.cpp`):

1. **Button** labelled with the current target name, `(None)` when null, or `—` when mixed across selection. Tooltip shows the asset path / scene name.
2. **Click** opens a modal picker with a search field and a flat list, filtered to the matching kind (Texture, Audio, Font, Scene, Prefab, Entity, Component, or any). Picking writes the new reference to every selected entity. A `(None)` entry clears the reference.
3. **Drag-drop** accepts the appropriate payload type:
   - `ASSET_BROWSER_ITEM` for asset refs (Texture / Audio / Font / Asset / Scene / Prefab).
   - `HIERARCHY_ENTITY` for `EntityRef`.
   - `COMPONENT_REF` for `ComponentRef`.
   Drops are validated against the expected asset kind / component type **before** being accepted. A wrong-kind drop is silently rejected — no warning, the picker just doesn't take it.
4. **Right-click → "Clear"** writes a null reference.

### EntityRef polymorphism (entity vs prefab)

`EntityRef` is the one polymorphic type. The value may be either a **persistent entity id** or a **prefab asset reference**:

- `PropertyValue::UIntValue` stores the integer (entity ID or prefab GUID).
- `PropertyValue::StringValue == "prefab"` is the tag that flags the value as a prefab reference. When unset, it's an entity ID.
- `ToString(PropertyValue)` reconstructs the `"prefab:<id>"` wire format when the tag is set.
- Equality compares **both** fields.

**Code reading `PropertyValue::UIntValue` directly MUST also check `StringValue` to distinguish the two.** The drawer + picker handle both cases via the same EntityRef code path. If you write a custom inspector that pokes a UInt out of an EntityRef value, you'll mis-resolve prefab refs as scene entities and chase phantom bugs.

## Registering a component for the inspector

The full registration goes through `Funke::Package::RegisterSerializableComponent` (in `Funke-Engine/src/Packages/PackageRegistration.hpp`). Inside a package's `FunkePackage_OnLoad`:

```cpp
std::vector<PropertyDescriptor> props = {
    Properties::Make("Health",     "Health",     &HealthComponent::Health,
                     Properties::Meta::Clamp(0.0, 100.0)),
    Properties::Make("Regen",      "Regen / sec", &HealthComponent::Regen),
    Properties::Make("Invincible", "Invincible",  &HealthComponent::Invincible),
};

Funke::Package::RegisterSerializableComponent<HealthComponent>(
    /* displayName    */ "Health",                       // Add Component popup label
    /* subcategory    */ "Gameplay",                     // Add Component popup section
    /* serializedName */ "HealthComponent",              // JSON key in .scene / .prefab
    /* drawInspector  */ nullptr,                        // null = unified auto-drawer
    /* serialize      */ &SerializeHealth,               // Json::Value(const HealthComponent&)
    /* deserialize    */ &DeserializeHealth,             // void(HealthComponent&, const Json::Value&)
    /* category       */ ComponentCategory::Component,
    /* properties     */ std::move(props));
```

`properties` only drive the inspector and Animator; nothing is persisted from them. With `serialize`/`deserialize` = nullptr the component is never written to `.scene`/`.prefab` and vanishes on save and Play→Stop. Field rules: `funke-serialize-field`.

**Always pass `drawInspector = nullptr`** unless you have a genuine reason to bypass the auto-drawer. The auto-drawer respects multi-selection, undo, prefab overrides, EnabledIf gates, and every metadata feature — a custom drawer skips all of that unless you reimplement it. The two legitimate reasons to write a custom drawer are: (1) the field shape genuinely doesn't fit any PropertyType (rare — add a new PropertyType instead), or (2) the inspector needs interactive widgets that aren't field edits (gizmo buttons, "Bake" buttons, etc.) — and even then, prefer adding those as actions outside the property list.

## Component conflicts

When two components are mutually exclusive (typically rendering-style components: SpriteRenderer / Image / ParticleSystem2D / custom), declare the conflict so the Add Component popup hides the conflicting entry on entities that already have one:

```cpp
Funke::Package::DeclareComponentConflict<MyRendererComponent, Funke::SpriteRendererComponent>();
```

The relation is **symmetric** — declaring on either side is sufficient. The registry's `HasConflict` lookup walks both directions. Call this **after** both components are registered (typically at the end of `FunkePackage_OnLoad` for the second one).

Engine-side built-in conflicts are wired in `BuiltInComponentRegistration.cpp`. Packages declare their own against the built-ins they conflict with.

## Editor gizmos

A component can install a per-entity gizmo drawer that runs in the editor viewport selection pass:

```cpp
Funke::Package::SetEditorGizmo<MyComponent>(+[](Entity e) {
    const auto& c = e.GetComponent<MyComponent>();
    Gizmo::SetColor(Color::Green());                      // Graphics/Gizmo.hpp
    Gizmo::DrawWireCircle(c.Position, c.Radius);
});
```

Call **after** `RegisterComponent` / `RegisterSerializableComponent` for the same component — the helper asserts at registration time if the component isn't registered yet, because almost every "my gizmo isn't drawing" case is a missing or misordered Register call.

Last-call-wins: a second `SetEditorGizmo` replaces the first, which is the intended path for runtime rewiring of a gizmo without re-registering the whole component.

## Entity presets (Add Entity popup)

A package can surface a one-click "create a new entity preconfigured with this component" entry under the editor's Create menu:

```cpp
Funke::Package::RegisterComponentEntityPreset<TilemapComponent>(
    /* menuPath    */ "2D Entity",
    /* label       */ "Tilemap",
    /* defaultName */ "Tilemap");
```

The preset runs the registry's `AddWithDependencies` for the component so any required sibling components (declared via `ComponentInfo::dependsOn`, or `Codegen::DeclareDependency`) are added together. For non-trivial presets, use the raw `RegisterEntityPreset(menuPath, label, defaultName, createLambda)` form and assemble the entity yourself.

## Common pitfalls

1. **References need the explicit `MakeTextureRef` / `MakeAudioRef` / `MakeFontRef` / `MakeEntityRef` helpers.** `Make(&Component::TextureHandle)` fails to compile (handle types deduce to `None`), but a raw `uint64_t` GUID member silently becomes a `UInt64` number widget — no compile-time check catches that one.

2. **Editing a Vec3 in multi-select edits only the touched channel.** This is the contract — don't write a custom drawer that bulk-overwrites all channels of all entities, or you'll clobber per-entity per-channel state on the untouched axes.

3. **`EntityRef` is polymorphic between persistent entity ids and prefab GUIDs.** Reading `UIntValue` directly without checking `StringValue == "prefab"` mis-resolves prefab refs as entity IDs. Always go through the drawer / picker, or check both fields if you must read the raw value.

4. **Enums use magic_enum — names matter at runtime.** `Properties::Make` on an `enum class` builds the `EnumDescriptor` from `magic_enum::enum_values<T>()` and `magic_enum::enum_name(v)`. If you rename an enum value, every saved scene / prefab keeps the old integer but the new name appears in the combo — old saves still load (integer-based), but `serializedName` on the *component* doesn't help you here; rename carefully.

5. **`StringList` and `List` are not interchangeable.** `StringList` predates the generic `List<T>` and several serializers still depend on its legacy wire shape. Pick `StringList` for new string-only lists, `List<TItem>` for everything else.

6. **`EnabledIf` predicates run on every frame per entity.** Keep them cheap — no allocations, no scene-wide lookups. The `EnabledIf<TComponent>` overload reads the named component directly and skips entities that don't have it, which is the usual right answer.

7. **C# `[EnabledIf]` matches against the editor wire representation, not the runtime value.** A `float` field compared with `[EnabledIf(nameof(Speed), 0)]` checks against the `Float` wire (a double). Integer / enum literals work; complex types (vectors, colors, references) do not — use the predicate form on the native side instead.

8. **`name` is stable, `displayName` is cosmetic.** The `displayName` is only the inspector label and can be renamed freely. Renaming `name` breaks Animator clips that target the property path (scene JSON is unaffected — its keys live in `serialize`/`deserialize`), so don't rename it without migrating clips.

9. **`drawInspector = nullptr` is almost always correct.** Writing a custom inspector bypasses multi-select, undo, prefab overrides, EnabledIf gates, and the metadata system. Unless the field genuinely doesn't fit a PropertyType, leave it null.

10. **`ComponentRef` needs `Metadata.ComponentTypeName`** so the picker can filter the entity's components to the compatible type. Without it the picker shows every component on the target and accepts any drop — that's almost always a bug. Set it to the `displayName` registered for the target component type.

11. **`SetEditorGizmo` asserts if called before registration.** Order matters — register the component first, install the gizmo second. The assert message is the actionable hint; don't try to suppress it.

12. **Component conflicts are symmetric.** Declaring `DeclareComponentConflict<A, B>()` once is enough — the registry walks both directions. Declaring it twice (once per side) is harmless but redundant.

## Quick reference — where things live

| Concern | File |
|---|---|
| Canonical PropertyType enum + wire tags | `Funke-Engine/src/Inspector/PropertyType.hpp` |
| PropertyDescriptor struct + variant branches | `Funke-Engine/src/Inspector/PropertyDescriptor.hpp` |
| PropertyMetadata struct (clamp / tooltip / etc.) + `With*` builders | `Funke-Engine/src/Inspector/PropertyMetadata.hpp` |
| `Properties::Make*` + `Properties::Meta::*` (the registration API) | `Funke-Engine/src/Inspector/PropertyRegistration.hpp` |
| PropertyValue (the boxed value passed through Get/Set) | `Funke-Engine/src/Inspector/PropertyValue.{hpp,cpp}` |
| Per-type widget dispatch + multi-select behavior | `Funke-Editor/src/Inspector/PropertyDrawer.cpp` |
| Reference picker modal + drag-drop validation | `Funke-Editor/src/Inspector/ReferencePicker.{hpp,cpp}` |
| `RegisterSerializableComponent`, `DeclareComponentConflict`, `SetEditorGizmo`, `RegisterEntityPreset` | `Funke-Engine/src/Packages/PackageRegistration.hpp` |
| C# inspector attributes | `Funke-ScriptCore/Source/Funke/Attributes/*Attribute.cs` |
| Built-in engine component registration (good pattern reference) | `Funke-Engine/src/Scene/BuiltInComponentRegistration<Category>.cpp` (one TU per slice, listed in call order in `BuiltInComponentRegistrationShared.hpp`; see `funke-add-component`); `BuiltInComponentRegistration.cpp` owns the call order, which sets `typeIdU32`, plus the cross-category conflicts/dependencies |
