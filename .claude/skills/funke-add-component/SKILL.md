---
name: funke-add-component
description: Use when adding a new built-in ECS component to the Funke engine (C++), or auditing whether an existing one is wired everywhere. Triggers on "add a component", "new component", "create a Health component", "built-in component", "RegisterComponent", "ComponentInfo", "ComponentRegistry", "BuiltInComponentRegistration", "serializedName", "DeclareConflict", "DeclareDependency", "on_construct hook", "ComponentCategory::Tag", "C# component wrapper", "s_NativeComponentNames", or work in `Funke-Engine/src/Components/`, `Funke-Engine/src/Scene/BuiltInComponentRegistration*.cpp`, `Scene/ComponentInfo.hpp`. Covers the full pipeline: struct, per-category registration slice, serialization (registry-driven vs the three hand-written ladders), prefab/clone/ECB concerns, Scene construct/destroy hooks, C# exposure, tests and premake regen. For changing fields on an existing component use `funke-serialize-field`; for inspector rows use `funke-inspector-ruleset`; for package-owned components use `funke-author-package`.
---

# Add a Built-in Component

A built-in component touches up to eight sites. Missing one fails silently (data lost on save, play/stop, paste, prefab spawn, or invisible to C#), so walk the whole list. Reference implementations: `SpriteAnimator2DComponent` (newest, data + system + C#), `WidthConstraintComponent` / `MaskComponent` (minimal registry-driven serialization).

## 1. Struct

`Funke-Engine/src/Components/<Category>/<Name>Component.hpp` (categories: `General/`, `Graphics/`, `Physics/`, `Audio/`, `UI/`; tags go in `Components/Tags.hpp`).

```cpp
#pragma once
#include "Core/Export.hpp"

namespace Funke {
	// One line on what it does, if not obvious.
	struct FUNKE_API HealthComponent {
		float Max = 100.0f;
		float Current = 100.0f;

		// Runtime state; not serialized, so every Play session starts fresh.
		bool Dead = false;
	};
}
```

- Plain value type, default member initializers. Keep it **default-constructible** (`ComponentInfo::defaultEmplace`, editor Add Component and ECB depend on it). Naming/export rules: `funke-code-style`.
- `FUNKE_API` only matters for classes with out-of-line members or symbols used across the DLL boundary (editor, tests, packages); header-only POD structs work without it, but adding it is harmless.
- Owning heap members (`std::string`, `std::vector`) make the type non-trivially-destructible: the registry then leaves `emplaceFromBytes`/`writeBytes` null, so C# ECB playback rejects `AddComponent` of it (logged `Ecb` warning) and prefabs containing it are not baked by `PrefabTemplateCache` (slow path, still correct). If C# has a POD mirror of a prefix, use `Codegen::RegisterPrefixByteEmplacer<T>(registry, offsetof(T, firstNonPodMember))`.
- Prefer not to cache scene pointers at all: `ParticleSystem3DComponent` takes the emitter's world matrix as a parameter of `Simulate(dt, world, quality)` (its system and the editor preview pass it in), so value copies, clones and prefab bakes stay valid with the synthesized `copyTo`, and the simulation is unit-testable without a scene.
- Caching `Scene*`/entity handles/runtime pointers inside the component requires a custom `copyTo` and `emplaceFromBytes` (see `ParticleSystem2DComponent` in `BuiltInComponentRegistrationRendering.cpp`), otherwise clones point at the source entity.
- If it is public API: forward-declare it in `Components/Forward.hpp` and include it from the umbrella `Funke-Engine/src/Funke.hpp` (used by native scripts and the SDK). Do NOT add to `Components/Components.hpp` (deprecated, emits a warning). Consumers include the specific component header.
- Module gating: `Funke-Engine/premake5.lua` strips `src/Components/Graphics/**` etc. when a module is off. Put the file where its module lives.

## 2. Register (per-category slice)

`Funke-Engine/src/Scene/BuiltInComponentRegistration<Category>.cpp` (`General`, `Rendering`, `Physics`, `Audio`, `UIWidgets`, `UILayout`). `BuiltInComponentRegistration.cpp` owns the call order of the slices plus all `DeclareConflict` / `DeclareDependency` / tag registrations. Append your component at the end of its slice; keep the `Internal::Register*` order stable (registration order assigns `typeIdU32`, used in-process by the ECB wire format and prefab templates; never persisted).

A component that belongs to an engine module (2D world: Transform2D, sprites, Box2D…; 3D: Transform3D, meshes, lights, Jolt…) registers inside `Internal::ModuleRegistrationScope scope(registry, EngineModule::TwoD|ThreeD);` and under `#if FUNKE_WITH_2D|3D`, its files go into `Funke-Engine/EngineModules.json`, and its C# bindings into `ScriptBindings2D.cpp` / `ScriptBindings3D.cpp` plus an `X(Slot)` line in `Scripting/ScriptBindingModuleSlots.hpp`. UI and other core components need none of that.

Two registration shapes, both used in-tree:

```cpp
// a) helper: no serialization of its own
RegisterComponent<HealthComponent>(registry, "Health",          // displayName (Add Component popup, C# lookup)
	ComponentCategory::Component, "Gameplay", "Health",            // subcategory, serializedName (JSON key)
	{ Properties::Make("Max", "Max", &HealthComponent::Max),
	  Properties::Make("Current", "Current", &HealthComponent::Current) });

// b) explicit ComponentInfo: preferred for new components because it carries serialize/deserialize
ComponentInfo info{ "Health", "Gameplay", ComponentCategory::Component };
info.serializedName = "Health";
info.properties = { /* see funke-inspector-ruleset */ };
info.serialize = [](Entity e) -> Json::Value {
	const auto& c = e.GetComponent<HealthComponent>();
	Json::Value v = Json::Value::MakeObject();
	v.AddMember("max", Json::Value(c.Max));
	v.AddMember("current", Json::Value(c.Current));
	return v;
};
info.deserialize = [](Entity e, const Json::Value& v) {
	auto& c = e.GetComponent<HealthComponent>();
	const HealthComponent d{};   // missing key -> type default, so Reset Component (deserialize with {}) works
	const Json::Value* m = nullptr;
	c.Max     = (m = v.FindMember("max"))     ? static_cast<float>(m->AsDoubleOr(d.Max))     : d.Max;
	c.Current = (m = v.FindMember("current")) ? static_cast<float>(m->AsDoubleOr(d.Current)) : d.Current;
};
registry.Register<HealthComponent>(info);
```

Many in-tree UI registrations fall back to the *current* value (`AsDoubleOr(c.X)`), which makes Reset Component a no-op for them; don't copy that.

- `has`/`add`/`remove`/`copyTo`/`getRaw`/`defaultEmplace` are synthesized by `ComponentRegistry::Register<T>` (`Scene/ComponentRegistry.hpp`) — don't write them.
- `serializedName` empty = never saved, and Copy/Paste/Reset Component are disabled in the inspector.
- `ComponentCategory::Tag` hides it from Add Component (use for internal/identity data and empty marker structs; `Scene::AddComponent<TTag>` requires `std::is_empty_v`). `ComponentCategory::System` is not for components.
- **3D and later slices:** after the Tags come `Register3D`, `RegisterPhysics3D`, `RegisterParticles3D`, each in its own TU. Do **not** append to an earlier slice such as `Register3D` (it would shift every Physics3D/Particles3D id); add a new slice function at the end of `RegisterBuiltInComponents` (declare it in `BuiltInComponentRegistrationShared.hpp`; the premake glob picks up the new `BuiltInComponentRegistration<Name>.cpp`).
- Conflicts ("one renderer per entity") and dependencies (Button pulls in Interactable) go in `RegisterBuiltInComponents` via `DeclareConflict<A,B>` / `DeclareDependency<Dependent,Dependency>`, after every mentioned type is registered. `onAdd` (smart defaults) only fires through `AddWithDependencies` (editor Add Component), never on load.
- Inspector/hierarchy icon: add `SetIcon<T>(registry, "<name>")` to `AssignBuiltInIcons` in `BuiltInComponentRegistration.cpp` (`ComponentInfo::icon`, a file stem from `Funke-Runtime/FunkeAssets/Textures/Editor/General/`, e.g. `light_point`). Without one the editor guesses from the display name; `ComponentIconTests.cpp` checks every icon exists. Entity icons in the Hierarchy follow the priority list in `EntityTypeIconName` (`ImGuiEditorLayer.cpp`).
- Inspector fields, custom drawers (`Funke-Editor/src/Editor/EditorComponentRegistration.cpp`), gizmos, variants, refs: **defer to `funke-inspector-ruleset`**. Property `name` should match the C# field name; prefer `drawInspector = nullptr`.

## 3. Serialization

**Prefer the registry-driven pair** (`info.serialize` / `info.deserialize`, step 2b). `SceneSerializer` picks it up automatically for scene save/load (JSON and binary), clipboard/duplicate, Copy/Paste/Reset Component, `.prefab` sources and prefab-instance override diffs. Field-level rules (defaults, keys, entity/asset refs, prefab diff pitfalls): `funke-serialize-field`.

- `ComponentInfo::serializeArchive` exists but is **not consumed** by `SceneSerializer` today, despite the "SCHEDULED FOR REMOVAL" comment on the legacy pair. Use `serialize`/`deserialize`.
- The alternative is the hand-written ladders (how colliders, Rigidbody2D, AudioSource, RectTransform2D, Image, Text are still done; Transform2D, SpriteRenderer, Camera2D and ParticleSystem2D moved to the registry in `BuiltInComponentRegistration2D.cpp`): `SerializeEntity` in `Serialization/SceneSerializer.cpp`, `DeserializeFullEntity` **and** `DeserializeComponent` in `Serialization/SceneSerializerDeserialize.cpp`. All three must agree, or data is lost on one path (this caused the ParticleSystem2D bursts regression). Don't add new components there.
- Registry-driven serialization only runs when an `Application` with a `SceneManager` exists (`SceneManager::GetComponentRegistry()`); a bare `Scene::CreateDetachedScene` in a test won't serialize it.
- `EntityOrigin::Runtime` entities are never saved; play-mode survival is automatic once serialization works (see `funke-scene-workflow`).

## 4. Scene hooks (only if add/remove must touch a subsystem)

`Funke-Engine/src/Scene/Scene.cpp` constructor (`Scene::Scene`, ~line 1440): `m_Registry.on_construct<T>().connect<&Scene::OnTConstruct>(this)` / `on_destroy`. Declare the private handlers in `Scene.hpp` next to the others.

- `on_construct` fires at emplace, **before** a loader fills the fields. Idempotent hooks that read component data must defer while `m_SuppressConstructHooks` is set (push to `m_DeferredConstructHooks`; see `OnSpriteRendererComponentConstruct`). `Scene::LoadGuard` flushes them.
- Hooks that touch global worlds (physics, audio, scripting) must early-out on `m_IsDetached || m_LifecycleSuspended` (detached scenes back the prefab inspector, staged loads and tests) and must also be replayed in `Scene::InitializeRuntimeComponentState()` for suspended scenes being activated.

## 5. C# exposure (optional)

Native name lookup (`FindComponentByName` in `ScriptBindings.cpp`) accepts the serializedName, `<serializedName>Component`, or the displayName, so a C# `Component` subclass gets generic `HasComponent/AddComponent/RemoveComponent<T>` without new bindings; property access needs bindings. Details in `funke-engine-scripting`; the sites are:

1. Native functions in `Funke-Engine/src/Scripting/ScriptBindings.cpp` (use `GET_COMPONENT(Type, entityID, failReturn)`), assigned in `ScriptBindings::PopulateNativeBindings` as `b.X = FNK_GUARDED_BINDING(Funke_X)` (`Scripting/ScriptBindingGuard.hpp`). Non-component bindings live in `ScriptBindingsScene.cpp`, ECB in `ScriptBindingsEcb.cpp`, jobs in `ScriptBindingsJobs.cpp`.
2. Function pointers **appended at the end** of `NativeBindings` in `Scripting/ScriptGlue.hpp` and, in identical order, of `NativeBindingsStruct` in `Funke-ScriptCore/Source/Funke/Interop/NativeCallbacks.cs`. The handshake checks `sizeof` and `k_ScriptInteropLayoutVersion` / `InteropHandshake.LayoutVersion` (`Interop/ScriptHostBridge.cs`); bump both together on reorder/remove.
3. `internal static` wrappers in `Funke-ScriptCore/Source/Funke/Core/InternalCalls.cs`.
4. Wrapper class `: Component` in `Funke-ScriptCore/Source/Funke/Scene/Components.cs` (UI types in `UI/UIComponents.cs`), using `RequireComponent<T>()`.
5. Map the type to the native **displayName** in `s_NativeComponentNames` (`Funke-ScriptCore/Source/Funke/Scene/Entity.cs`). Without it the C# class name is used for lookup (works only if it equals the serializedName) and reverse name→type resolution misses it.
6. If the C++ layout of `Transform2DComponent` / `SpriteRendererComponent` changes, update `Funke-ScriptCore/Source/Funke/Native/NativeTransform2D.cs` / `NativeSpriteRenderer.cs` (size is verified at init).

## 6. Tests (`Tests/Funke-Engine-Tests/src`, doctest)

- Serialization round-trip: extend `SceneSerializerRoundTripTests.cpp` (full-entity `SerializeEntityFull` → `DeserializeEntityFromValue`, plus component-level `SerializeComponent` → `DeserializeComponent`). Registry-driven components need a headless `Application` (pattern: `MakeSceneTestConfiguration()` in `SceneManagerFailureTests.cpp`, assertions collected in `Start()`, then `Application::Quit()`).
- Entity refs / clone remap: `SceneSerializerClipboardTests.cpp`; prefab internal refs: `SceneSerializerPrefabRefTests.cpp`.
- Behavior: expose a static pure step function on the system and unit-test it (`SpriteAnimationTests.cpp` → `SpriteAnimationSystem::Step`).
- If the component is public API, add a `static_assert(std::is_default_constructible_v<...>)` line to `UmbrellaHeaderTests.cpp`.
- C# wrapper changes: `Tests/Funke-ScriptCore-Tests` (xUnit, e.g. `InteropHandshakeTests.cs`).

## 7. Build

New `.hpp`/`.cpp` or test files require regenerating project files (`vendor\bin\premake5.exe vs2022`), since the .vcxproj files list sources explicitly. Then build per `funke-build-engine`, and remember the editor loads `Funke-Engine.dll` from its own folder.

## 8. Docs

User-facing components: `CHANGELOG.md`, `Github-Docs/Documentation/`, and the component key list in `.claude/skills/funke-build-game/references/scene-json.md` (and its `.agents` copy).

## Pitfalls

- Adding to `NativeBindings`: bump `k_ScriptInteropLayoutVersion` and `InteropHandshake.LayoutVersion` together, append the C# delegates in the same order, and extend the packed-count assertions in `Tests/Funke-ScriptCore-Tests/Rendering3DTests.cs` when you add a packed `float*` layout.
- New renderable 3D component: add its pass to `Renderer3D::RenderScene` (meshes, then particles); every pass must honour `m_OwnDepthNeedsClear` and call `WebGPUBackend::FlushCommands()` before the next scene's `WriteBuffer`. Editor and runtime both go through `RenderScene`, so nothing else needs wiring for drawing.
- Editor play-on-awake lives in three places: the scene system's `Awake`, `PlayOnAwakeRecursive` in `Scene.cpp` (prefab spawns) and `StartPlayOnAwakeComponents` in `ImGuiEditorLayer.cpp`.

- Registered with `RegisterComponent(...)` but no `serialize` → component silently vanishes on save, play→stop, paste, and prefab apply.
- Display name typo in `s_NativeComponentNames` → C# `GetComponent<T>()`/`HasComponent<T>()` silently fail.
- `AddComponent` inside an `on_construct` hook for a type whose hook is being deferred → ordering bugs; keep hooks idempotent.
- A component with heap members added via a C# `EntityCommandBuffer` → playback rejected with an `Ecb` "no emplaceFromBytes callback" warning (see step 1).
