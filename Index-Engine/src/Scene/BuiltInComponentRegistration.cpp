#include "pch.hpp"
#include "Scene/BuiltInComponentRegistration.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/General/AnimatorComponent.hpp"
#include "Components/General/General.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/Tags.hpp"
#include "Components/UI/UI.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Components/Graphics/PostProcessing2DComponent.hpp"

#if INDEX_WITH_2D
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/SpriteAnimator2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/FastBody2DComponent.hpp"
#include "Components/Physics/FastBoxCollider2DComponent.hpp"
#include "Components/Physics/FastCircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#endif

#if INDEX_WITH_3D
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/Camera3DComponent.hpp"
#include "Components/Graphics/DirectionalLightComponent.hpp"
#include "Components/Graphics/MeshRendererComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Components/Graphics/PointLightComponent.hpp"
#include "Components/Graphics/SkinnedMeshRendererComponent.hpp"
#include "Components/Graphics/SpotLightComponent.hpp"
#include "Components/Physics3D/Collider3DComponents.hpp"
#include "Components/Physics3D/Rigidbody3DComponent.hpp"
#endif

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
// ComponentRegistry assigns ComponentInfo::typeIdU32 sequentially in registration order;
// the ECB wire format and the in-memory PrefabTemplateCache name components by it, and the
// managed side asks for it at runtime. Ids are per process, never persisted, so a build
// without an engine module (whose components simply aren't registered) is consistent; the
// order is still kept stable for full builds.
//
// Engine feature modules (Index.2D, Index.3D) register inside a ModuleRegistrationScope so
// the editor can hide their components while the project leaves the module out.
//
// Conflicts and dependencies stay here because they span categories and must run after
// every component they mention has been registered.
namespace Index {
	namespace {
		template <typename T>
		void SetIcon(ComponentRegistry& registry, const char* icon) {
			if (ComponentInfo* info = registry.GetInfo<T>()) info->icon = icon;
		}

		// Inspector header / hierarchy icons; names are files in IndexAssets/Textures/Editor/General.
		void AssignBuiltInIcons(ComponentRegistry& registry) {
			SetIcon<ScriptComponent>(registry, "script");
			SetIcon<AnimatorComponent>(registry, "animator");
			SetIcon<RectTransform2DComponent>(registry, "bounding_box");
			SetIcon<ImageComponent>(registry, "eye_open");
			SetIcon<TextRendererComponent>(registry, "caret");
			SetIcon<InputFieldComponent>(registry, "caret");
			SetIcon<LocalizedTextComponent>(registry, "caret");
			SetIcon<HorizontalLayoutGroupComponent>(registry, "grid");
			SetIcon<VerticalLayoutGroupComponent>(registry, "grid");
			SetIcon<GridLayoutGroupComponent>(registry, "grid");
			SetIcon<AudioSourceComponent>(registry, "audio_source");
			SetIcon<PostProcessing2DComponent>(registry, "post_processing");
#if INDEX_WITH_2D
			SetIcon<Transform2DComponent>(registry, "move");
			SetIcon<Camera2DComponent>(registry, "camera");
			SetIcon<SpriteRendererComponent>(registry, "eye_open");
			SetIcon<SpriteAnimator2DComponent>(registry, "eye_open");
			SetIcon<ParticleSystem2DComponent>(registry, "particles");
			SetIcon<Rigidbody2DComponent>(registry, "rigidbody_2d");
			SetIcon<FastBody2DComponent>(registry, "rigidbody_2d");
			SetIcon<BoxCollider2DComponent>(registry, "collider_box_2d");
			SetIcon<FastBoxCollider2DComponent>(registry, "collider_box_2d");
			SetIcon<CircleCollider2DComponent>(registry, "collider_circle_2d");
			SetIcon<FastCircleCollider2DComponent>(registry, "collider_circle_2d");
			SetIcon<PolygonCollider2DComponent>(registry, "collider_polygon_2d");
#endif
#if INDEX_WITH_3D
			SetIcon<Transform3DComponent>(registry, "move");
			SetIcon<Camera3DComponent>(registry, "camera");
			SetIcon<DirectionalLightComponent>(registry, "light_sun");
			SetIcon<PointLightComponent>(registry, "light_point");
			SetIcon<SpotLightComponent>(registry, "light_spot");
			SetIcon<ParticleSystem3DComponent>(registry, "particles");
			SetIcon<SkinnedMeshRendererComponent>(registry, "animator");
			SetIcon<Rigidbody3DComponent>(registry, "rigidbody_3d");
			SetIcon<BoxCollider3DComponent>(registry, "collider_box_3d");
			SetIcon<SphereCollider3DComponent>(registry, "collider_sphere_3d");
			SetIcon<CapsuleCollider3DComponent>(registry, "collider_capsule_3d");
			SetIcon<MeshCollider3DComponent>(registry, "collider_mesh_3d");
#endif
		}
	}

	void RegisterBuiltInComponents(ComponentRegistry& registry) {
		using Index::Codegen::RegisterComponent;
		using Index::Codegen::DeclareConflict;
		using Index::Codegen::DeclareDependency;
		namespace Internal = BuiltInComponentRegistrationInternal;

		Internal::RegisterGeneral(registry);
		Internal::RegisterRendering(registry);
#if INDEX_WITH_2D
		{
			Internal::ModuleRegistrationScope module2D(registry, EngineModule::TwoD);
			Internal::RegisterPhysics(registry);
		}
#endif
		Internal::RegisterAudio(registry);
		Internal::RegisterUIWidgets(registry);
		Internal::RegisterUILayout(registry);
#if INDEX_WITH_2D
		Internal::Attach2DSerialization(registry);
#endif

		// HierarchyComponent is real but managed via Entity::SetParent;
		// users don't add it from the Add Component popup. Tagged so the
		// inspector hides it from the picker.
		RegisterComponent<HierarchyComponent>(registry, "Hierarchy", ComponentCategory::Tag);

		// ── Scripting ───────────────────────────────────────────────
		RegisterComponent<ScriptComponent>(registry, "Scripts",
			ComponentCategory::Component, "Scripting", "Scripts");

		// ── Conflicts ───────────────────────────────────────────────
#if INDEX_WITH_2D
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
#endif

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

#if INDEX_WITH_3D
		{
		// ── 3D ──────────────────────────────────────────────────────
		// Appended after the Tags so no older typeIdU32 shifts; a build without the module skips this block.
		Internal::ModuleRegistrationScope module3D(registry, EngineModule::ThreeD);
		Internal::Register3D(registry);
#if INDEX_WITH_2D
		DeclareConflict<Transform3DComponent, Transform2DComponent>(registry);
#endif
		DeclareConflict<Transform3DComponent, RectTransform2DComponent>(registry);
		DeclareDependency<Camera3DComponent,         Transform3DComponent>(registry);
		DeclareDependency<MeshRendererComponent,     Transform3DComponent>(registry);
		DeclareDependency<DirectionalLightComponent, Transform3DComponent>(registry);
		DeclareDependency<PointLightComponent,       Transform3DComponent>(registry);
		DeclareDependency<SpotLightComponent,        Transform3DComponent>(registry);
		DeclareConflict<DirectionalLightComponent, PointLightComponent>(registry);
		DeclareConflict<DirectionalLightComponent, SpotLightComponent>(registry);
		DeclareConflict<PointLightComponent, SpotLightComponent>(registry);

		// ── Physics 3D (Jolt) ───────────────────────────────────────
		// After the 3D slice for the same typeIdU32 reason. Transform3D already conflicts with
		// Transform2D, so these dependencies also keep the 2D and 3D physics stacks apart.
		Internal::RegisterPhysics3D(registry);
		DeclareDependency<Rigidbody3DComponent,       Transform3DComponent>(registry);
		DeclareDependency<BoxCollider3DComponent,     Transform3DComponent>(registry);
		DeclareDependency<SphereCollider3DComponent,  Transform3DComponent>(registry);
		DeclareDependency<CapsuleCollider3DComponent, Transform3DComponent>(registry);
		DeclareDependency<MeshCollider3DComponent,    Transform3DComponent>(registry);

		// ── Particles 3D ────────────────────────────────────────────
		// After Physics3D for the same typeIdU32 reason.
		Internal::RegisterParticles3D(registry);
		DeclareDependency<ParticleSystem3DComponent, Transform3DComponent>(registry);
#if INDEX_WITH_2D
		DeclareConflict<ParticleSystem3DComponent, ParticleSystem2DComponent>(registry);
#endif
		}
#endif

		// ── Animation ───────────────────────────────────────────────
		// Core (no module); after the 3D slices for the same typeIdU32 reason.
		Internal::RegisterAnimation(registry);

#if INDEX_WITH_3D
		{
		// ── Skinned Mesh 3D ─────────────────────────────────────────
		// After Animation for the same typeIdU32 reason.
		Internal::ModuleRegistrationScope module3D(registry, EngineModule::ThreeD);
		Internal::RegisterSkinnedMesh3D(registry);
		DeclareDependency<SkinnedMeshRendererComponent, Transform3DComponent>(registry);
		DeclareConflict<SkinnedMeshRendererComponent, MeshRendererComponent>(registry);
		}
#endif

		// ── Localization ────────────────────────────────────────────
		// Core (no module); last for the same typeIdU32 reason.
		Internal::RegisterLocalization(registry);
		DeclareDependency<LocalizedTextComponent, TextRendererComponent>(registry);

		// ── Module conflicts ────────────────────────────────────────
		// 2D and 3D components describe different worlds (Transform2D vs Transform3D, Box2D vs Jolt): one entity
		// carrying both would have half its components silently ignored. Neither belongs on a UI rect either.
		// Derived from the module tags so new module components inherit the rule.
		{
			std::vector<std::type_index> twoD;
			std::vector<std::type_index> threeD;
			registry.ForEachComponentInfo([&](const std::type_index& id, const ComponentInfo& info) {
				if (!info.engineModule || info.category != ComponentCategory::Component) return;
				(*info.engineModule == EngineModule::TwoD ? twoD : threeD).push_back(id);
			});
			for (const std::type_index& a : twoD) {
				for (const std::type_index& b : threeD) {
					registry.DeclareConflict(a, b);
				}
			}
			const std::type_index rect = typeid(RectTransform2DComponent);
			for (const std::type_index& world : twoD) registry.DeclareConflict(world, rect);
			for (const std::type_index& world : threeD) registry.DeclareConflict(world, rect);
		}

		AssignBuiltInIcons(registry);

		// Debug check: conflict edges must be symmetric — asymmetry would silently break "Add Component" filtering.
		registry.ValidateConflictSymmetry();
	}
}
