#include "pch.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Components/General/General.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/FastBody2DComponent.hpp"
#include "Components/Physics/FastBoxCollider2DComponent.hpp"
#include "Components/Physics/FastCircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Components/UI/UI.hpp"
#include "Scripting/ScriptComponent.hpp"

// Single source of truth for built-in components — names, categories, properties.
//
// The registrations themselves live in one translation unit per category,
// BuiltInComponentRegistration<Category>.cpp. Every ComponentRegistry::Register<T>
// makes EnTT instantiate basic_storage<T>, storage_for<T>, their iterators and views,
// and every Properties::Make* call adds two std::function erasures — roughly 0.1s of
// compiler front-end time per component. As a single TU that was ~15s and ~1.25 GB in
// one cl.exe, the heaviest compile in the engine, and editing any one component's
// registration rebuilt all of it. Split, the slices compile in parallel and an edit
// only recompiles its own category. Add a new built-in to the slice for its category.
//
// THE CALL ORDER BELOW IS LOAD-BEARING. ComponentRegistry assigns
// ComponentInfo::typeIdU32 sequentially in registration order, and that id is what the
// ECB wire format and baked PrefabTemplateCache records use to name a component. The
// sequence replays the original single-function order exactly, including Hierarchy and
// Scripts after the UI layout groups and the Tags after the conflict and dependency
// declarations, so splitting the file changed no ids.
//
// Conflicts and dependencies stay here because they span categories and must run after
// every component they mention has been registered.
namespace Index {
	void RegisterBuiltInComponents(ComponentRegistry& registry) {
		using Index::Codegen::RegisterComponent;
		using Index::Codegen::DeclareConflict;
		using Index::Codegen::DeclareDependency;
		namespace Internal = BuiltInComponentRegistrationInternal;

		Internal::RegisterGeneral(registry);
		Internal::RegisterRendering(registry);
		Internal::RegisterPhysics(registry);
		Internal::RegisterAudio(registry);
		Internal::RegisterUIWidgets(registry);
		Internal::RegisterUILayout(registry);

		// HierarchyComponent is real but managed via Entity::SetParent;
		// users don't add it from the Add Component popup. Tagged so the
		// inspector hides it from the picker.
		RegisterComponent<HierarchyComponent>(registry, "Hierarchy", ComponentCategory::Tag);

		// ── Scripting ───────────────────────────────────────────────
		RegisterComponent<ScriptComponent>(registry, "Scripts",
			ComponentCategory::Component, "Scripting", "Scripts");

		// ── Conflicts ───────────────────────────────────────────────
		// One renderer per entity (Tilemap2D adds its own conflicts in its package init).
		DeclareConflict<SpriteRendererComponent, ParticleSystem2DComponent>(registry);
		DeclareConflict<SpriteRendererComponent, ImageComponent>(registry);
		DeclareConflict<ImageComponent, ParticleSystem2DComponent>(registry);

		// One spatial transform per entity — UI rects and world-space transforms
		// occupy the same role and should not coexist on a single entity.
		DeclareConflict<Transform2DComponent, RectTransform2DComponent>(registry);

		// Box2D and Index-Physics stacks must not be mixed per-entity.
		DeclareConflict<Rigidbody2DComponent, FastBody2DComponent>(registry);
		DeclareConflict<BoxCollider2DComponent, FastBoxCollider2DComponent>(registry);
		DeclareConflict<BoxCollider2DComponent, FastCircleCollider2DComponent>(registry);
		DeclareConflict<CircleCollider2DComponent, FastBoxCollider2DComponent>(registry);
		DeclareConflict<CircleCollider2DComponent, FastCircleCollider2DComponent>(registry);
		DeclareConflict<PolygonCollider2DComponent, FastBoxCollider2DComponent>(registry);
		DeclareConflict<PolygonCollider2DComponent, FastCircleCollider2DComponent>(registry);

		// One Box2D shape per entity — the rigidbody adopts whichever collider
		// is on the entity, and Scene::OnRigidBody2DComponentConstruct only
		// reaches for one. Layering two would leak a body.
		DeclareConflict<BoxCollider2DComponent, CircleCollider2DComponent>(registry);
		DeclareConflict<BoxCollider2DComponent, PolygonCollider2DComponent>(registry);
		DeclareConflict<CircleCollider2DComponent, PolygonCollider2DComponent>(registry);

		// Within Index-Physics, only one shape per body is supported.
		DeclareConflict<FastBoxCollider2DComponent, FastCircleCollider2DComponent>(registry);

		// Only one layout group per entity — they all rewrite the same
		// children's RectTransforms, so layering would produce frame-by-
		// frame fights between layout passes.
		DeclareConflict<HorizontalLayoutGroupComponent, VerticalLayoutGroupComponent>(registry);
		DeclareConflict<HorizontalLayoutGroupComponent, GridLayoutGroupComponent>(registry);
		DeclareConflict<VerticalLayoutGroupComponent, GridLayoutGroupComponent>(registry);

		// ── Dependencies ────────────────────────────────────────────
		DeclareDependency<ButtonComponent,     InteractableComponent>(registry);
		DeclareDependency<ButtonComponent,     RectTransform2DComponent>(registry);
		DeclareDependency<InputFieldComponent, InteractableComponent>(registry);
		DeclareDependency<InputFieldComponent, RectTransform2DComponent>(registry);
		DeclareDependency<ToggleComponent,     InteractableComponent>(registry);
		DeclareDependency<ToggleComponent,     RectTransform2DComponent>(registry);
		DeclareDependency<SliderComponent,     InteractableComponent>(registry);
		DeclareDependency<SliderComponent,     RectTransform2DComponent>(registry);
		DeclareDependency<DropdownComponent,   InteractableComponent>(registry);
		DeclareDependency<DropdownComponent,   RectTransform2DComponent>(registry);
		DeclareDependency<MaskComponent,       RectTransform2DComponent>(registry);

		// Tags (not user-addable)
		RegisterComponent<IdTag>(registry, "Id", ComponentCategory::Tag);
		RegisterComponent<StaticTag>(registry, "Static", ComponentCategory::Tag);
		RegisterComponent<DisabledTag>(registry, "Disabled", ComponentCategory::Tag);
		RegisterComponent<InheritedDisabledTag>(registry, "Inherited Disabled", ComponentCategory::Tag);
		RegisterComponent<DeadlyTag>(registry, "Deadly", ComponentCategory::Tag);

		// Debug check: conflict edges must be symmetric — asymmetry would silently break "Add Component" filtering.
		registry.ValidateConflictSymmetry();
	}
}
