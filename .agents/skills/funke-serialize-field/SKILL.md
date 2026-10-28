---
name: funke-serialize-field
description: Use when adding, renaming or changing a persisted field on an existing Funke component, or any data written to disk — scene/prefab files, project.json, build profiles, editor preferences. Triggers on "serialize", "save/load", "round-trip", "Json::Value", "FindMember", "AddMember", "SceneSerializer", "SerializeEntity", "DeserializeFullEntity", "DeserializeComponent", "ResetComponent", ".scene", ".prefab", "prefab override", "EntityOverrides", "binary scene", "FNKSCNB", "SCENE_FORMAT_VERSION", "project.json", "FunkeProject", "BuildProjectJson", "IsKnownProjectMember", "entity reference field", "asset GUID field", "field lost after play/stop", or work in `Funke-Engine/src/Serialization/` or `Funke-Engine/src/Project/`. Covers the in-house `Funke::Json` DOM, registry-driven vs hand-written component ladders, defaults/forward-compat, asset and entity refs, prefab-diff and staged-load consequences, project.json fields and which tests to extend. For brand-new components start with `funke-add-component`.
---

# Serialize a Field

Everything Funke persists is an `Funke::Json::Value` tree (`Funke-Engine/src/Serialization/Json.hpp`, in-house DOM parser: `Json::TryParse(text, out, &err)`, `Json::Stringify(v, pretty)`, `Value::MakeObject/MakeArray`, `AddMember`, `FindMember`, `AsDoubleOr/AsIntOr/AsInt64Or/AsUInt64Or/AsBoolOr/AsStringOr`, structural `operator==`). Binary scenes (`FNKSCNB` magic, chosen by project `assetSerializationFormat`) are a tagged encoding of the same tree, so a field that round-trips as JSON also round-trips as binary. No regex/string-scan parsing anywhere except `premake/package-loader.lua` reading `"packages"`.

## Pick the scope

| Data | Writer / reader | Notes |
|---|---|---|
| Component field (scene, `.prefab`, clipboard, undo, play snapshot) | Registry pair `ComponentInfo::serialize/deserialize` in `Funke-Engine/src/Scene/BuiltInComponentRegistration<Category>.cpp`, **or** the hand-written ladders (below) | Per-entity, per-instance |
| Scene header (`version`, `name`, `sceneId`, `systems`) | `SceneSerializer::SerializeScene` (`SceneSerializer.cpp`) / `ProcessSceneHeader` (`SceneSerializerDeserialize.cpp`) | C# scene systems + field overrides live in `systems` |
| C# `[ShowInEditor]` script fields | Automatic (`ScriptFields`, `SerializeScriptFields`) | Nothing to wire; see `funke-inspector-ruleset` |
| Project setting | `<project>/project.json` via `Funke-Engine/src/Project/FunkeProject.{hpp,cpp}` | See below |
| Build profile | `FunkeBuildProfile.cpp` (`.build` files) | See `funke-build-and-ship` |
| Editor-only preference | `Funke-Editor/src/Editor/EditorPreferences.cpp` (`%LOCALAPPDATA%\Funke\Editor\EditorPreferences.json`) | Not per-project |

## Component field: find which ladder owns the component

```
rg -n '"<SerializedName>"' Funke-Engine/src/Serialization Funke-Engine/src/Scene -g '*.cpp'
```

**A. Registry-driven** (Transform2D, SpriteRenderer, Camera2D, ParticleSystem2D in `BuiltInComponentRegistration2D.cpp`; UI widgets/layout, PostProcessing2D, Mask, all 3D components, package components next to their registration): edit the `serialize` and `deserialize` functions. One place; `SceneSerializer` calls them for save, load, paste, reset and prefab diff. Without an `Application` (CLI, doctest, headless tools) the serializer falls back to a registry holding only the built-in components (`SceneManager::GetComponentRegistryOrBuiltIn`), so package and C# components are skipped there. Files list registry components in registration order (`typeIdU32`). `ComponentInfo::serializeFirst` writes and reads a component before every hand-written one; Transform2D sets it because collider loading reads the transform's scale.

**B. Hand-written** (TextRenderer, Rigidbody2D, Box/Circle/Polygon colliders, AudioSource, RectTransform2D, Image, Scripts, tags `static`/`disabled`/`deadly`; the Fast* bodies moved to the Funke.Physics package and serialize via its registration): edit **all three** in the same change:
1. `SerializeEntity` — `Funke-Engine/src/Serialization/SceneSerializer.cpp` (anonymous namespace, `if (registry.all_of<T>(entity)) { ... entityValue.AddMember("<Name>", ...) }`).
2. `DeserializeFullEntity` — `SceneSerializerDeserialize.cpp` (scene load, binary streaming load, prefab instantiation, duplicate).
3. `DeserializeComponent` — `SceneSerializerDeserialize.cpp` (inspector Paste, Reset = `ResetComponent` passes `{}`, prefab revert).
The defaults used in 2 and 3 must be identical and equal the C++ member initializers. A field present in 1 but missing in 2 is lost on every save/load (the ParticleSystem2D bursts regression).

Read helpers for B: `SceneSerializerShared::GetFloatMember/GetIntMember/GetUInt64Member/GetBoolMember/GetStringMember/GetObjectMember/GetArrayMember`, `LoadTextureFromValue`, `LoadAudioFromValue` (`Serialization/SceneSerializerShared.hpp`). For A in the UI/rendering slices: `UIColorToJson/UIColorFromJson`, `UIUuidToJson/UIUuidFromJson`, `UIEntityHandleToJson/UIEntityHandleFromJson`, `UIEventListToJson/FromJson` (`Scene/BuiltInComponentRegistrationShared.hpp`). There are no `SerializeVec2`-style helpers; vectors are flat keys (`posX`/`posY`) or `{r,g,b,a}` objects.

## Field rules

- **Missing key = type default**, never an error, never the current value. Old files must load; `ResetComponent` relies on it. Only write optional keys when non-default if absence already means default (e.g. `spriteName`).
- **Key names**: short lowerCamel JSON keys (`playOnAwake`, `clipAsset`). They show up verbatim in the inspector's prefab "Revert Field" menu and in `Overrides` dot-paths (`AudioSource.volume`). Renaming a key = reading both old and new on load (legacy fallback, as `DeserializeComponent` does with `filterMode`/`filter`).
- **Numbers**: `Json::Value(float)` stores a double; read with `static_cast<float>(m->AsDoubleOr(d))`. Enums as `int`, validated/clamped on read.
- **64-bit ids** (asset GUIDs, entity persistent ids): write as decimal **strings** (`std::to_string(id)`). `GetUInt64Member` accepts string or number; `UIUuidFromJson` accepts strings only and otherwise returns its fallback. Doubles lose precision above 2^53.
- **Asset references**: persist the asset GUID (`UUID`/`AssetGUID`), never `TextureHandle`/`AudioHandle`/`FontHandle` (runtime-only). Resolve on load through `TextureManager::LoadTextureByUUID`, `AudioManager::LoadAudioByUUID`, or `LoadTextureFromValue(obj, "textureAsset", "texture", ...)` which also honors legacy path keys.
- **Entity references**: write `scene->GetEntityPersistentID(handle)` (never `RuntimeID`, never `entt` ids). On load resolve with `Scene::TryResolveEntityRef`; if not yet resolvable or `IsEntityRefRemapActive()` (duplicate/paste/prefab spawn), defer with `Scene::DeferEntityRefFixup` so clones retarget to their own children — copy `UIEntityHandleFromJson`. Store the ref in pointer-stable component storage only.
- **Runtime-only state** (timers, play flags, cached pointers): don't serialize; comment it as such in the struct.
- **Must be idempotent and side-effect-free**: one scene load runs serialize/deserialize several times (staged load into a detached scene, `SerializeScene`, `CommitStagedScene` re-deserialize, rollback snapshot), and `ReloadScene`/play-mode restore go through the same path. Deserialize code must not touch global systems (physics/audio) directly; that belongs in Scene construct hooks (see `funke-add-component`).
- **Format version**: `SCENE_FORMAT_VERSION` is defined in both `SceneSerializer.cpp` and `SceneSerializerDeserialize.cpp`, plus `SceneSerializerInternal::k_SceneFormatVersion` in `SceneSerializerShared.hpp`; files with a newer version are rejected. Additive fields do **not** bump it; bump all three together only for breaking changes, with a migration branch.

## Prefab consequences

A scene prefab instance is stored as `Origin: "Prefab"`, `PrefabGUID`, `Overrides` (dot-path → value, from `BuildOverridePatch` diffing the instance against the `.prefab` source) and `EntityOverrides` (per child source uuid). Implications:
- Identity keys (`uuid`, `Origin`, `RuntimeID`, `EntityID`, `SceneGUID`, `PrefabGUID`, `parentUuid`) are stripped before diffing (`Detail::RemoveEntityIdentityMembers`, `RemoveEntityComparisonIdentityMembers`). A new per-instance identity-like key must be added there or every instance shows as overridden.
- **Adding a new always-written key to an existing component** makes every instance of an older `.prefab` (which lacks the key) report an override, because a key missing from the source is a diff. Either write the key only when non-default, or normalize it for the diff like `SceneSerializerShared::NormalizeLegacyColliderKeysForDiff` does for `BoxCollider2D.localSizeX/Y`.
- Floats that are recomputed on load (normalized, clamped) must re-serialize to the same value, or instances diff against their source forever.

## Play mode

Edit-mode state survives Play→Stop only through serialization: on Play the editor snapshots every loaded scene (dirty ones to `<project>/.funke/PlayMode/<name>.scene`, clean ones by their project path) and on Stop rebuilds them from those files (`ImGuiEditorLayer::PreparePlayModeSnapshot` / `RestoreEditorSceneAfterPlaymode`). There is no in-memory snapshot. A field missing from either direction resets on Stop. (C# script field values additionally get re-applied from `m_PlayModeScriptFields`.)

## project.json field

`Funke-Engine/src/Project/FunkeProject.{hpp,cpp}` (real JSON, validated):
1. Member with default in `FunkeProject.hpp`.
2. Write in `BuildProjectJson`.
3. Read in the load block (`root.FindMember("<key>")` in `FunkeProject.cpp`), defaulting if absent.
4. Add the key to `IsKnownProjectMember` (or `IsKnownNestedProjectMember` for `splashScreen`/`profiler`/`graphics` children); the lists are fixed-size `std::array`s, so bump the element count too or it won't compile. Unknown keys are preserved verbatim on save (`PreserveUnknownProjectMembers`), so a missing entry here keeps a stale copy around after you stop writing it.
5. Add a type check to `ValidateProjectJsonStructure`; invalid types make load and save refuse.
6. `projectFormatVersion` / `k_CurrentProjectFormatVersion` only for breaking changes (needs a migration; newer formats are refused).
If the field affects the build, check `RuntimeApplication` and the Build panel too (`funke-build-and-ship`); project layout reference: `funke-project-structure`.

## Tests (`Tests/Funke-Engine-Tests/src`, doctest)

- Component fields: `SceneSerializerRoundTripTests.cpp` — add the field to the full-entity case (`SerializeEntityFull` → `DeserializeEntityFromValue`) **and** the component-level case (`SerializeComponent` → `DeserializeComponent`); for ladder B both are needed to catch a missing ladder. Also cover "missing key → default". Built-in registry components round-trip in a plain detached scene; package and C# components need a headless `Application` (see `MakeSceneTestConfiguration` in `SceneManagerFailureTests.cpp`).
- Entity-ref fields: `SceneSerializerClipboardTests.cpp` (duplicate remap), `SceneSerializerPrefabRefTests.cpp`.
- Json behavior: `JsonTests.cpp`. project.json: `ProjectSettingsTests.cpp` (round-trip, unknown-member preservation).
- New test files need `vendor\bin\premake5.exe vs2022` before building.

## Pitfalls

- Editing only `SerializeEntity` for a ladder-B component: saves fine, loads default.
- Reading a uint64 id with `AsDoubleOr`/`AsIntOr` → silently wrong GUID.
- Resolving an entity ref immediately during duplicate/paste → clone points at the original's child.
- Storing a path instead of a GUID → breaks on asset rename/move.
- Calling `SceneSerializer::LoadFromFile` on any loaded, non-suspended scene → refused ("requires an owner-level staged swap"); load inside an `OnLoad`/`OnLoadChecked` callback instead (`funke-scene-workflow`).
