#include "pch.hpp"
#include "Physics/PhysicsScene2D.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Core/EngineModules.hpp"
#include "Scene/Scene.hpp"
#include "Systems/TransformHierarchySystem.hpp"
#include "Components/General/Transform2DComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#if FUNKE_WITH_3D
#include "Components/General/Transform3DComponent.hpp"
#endif
#include "Components/Tags.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Joint2DComponents.hpp"

namespace Funke {
	namespace {
		// Lets a 2D component adopt a missing Transform2D, except on a 3D or UI entity: that transform would sit
		// next to the entity's own, so the component stays inert instead (the editor refuses the mix up front).
		bool EnsureTransform2D(entt::registry& registry, EntityHandle entity, const char* component) {
			if (registry.all_of<Transform2DComponent>(entity)) return true;
			const char* otherTransform = nullptr;
#if FUNKE_WITH_3D
			if (registry.all_of<Transform3DComponent>(entity)) otherTransform = "Transform3D";
#endif
			if (!otherTransform && registry.all_of<RectTransform2DComponent>(entity)) otherTransform = "RectTransform2D";
			if (otherTransform) {
				FNK_CORE_WARN_TAG("Scene", "{} on entity {} needs a Transform2D, but the entity uses {}; the component stays inactive.",
					component, static_cast<uint32_t>(entity), otherTransform);
				return false;
			}
			FNK_CORE_WARN_TAG("Scene", "Adding missing Transform2DComponent before creating {} for entity {}",
				component, static_cast<uint32_t>(entity));
			registry.emplace<Transform2DComponent>(entity);
			return true;
		}
	}
	void PhysicsScene2D::OnRigidBody2DComponentConstruct(entt::registry& registry, EntityHandle entity)
	{
		// Body adoption invariant: rigidbody adopts an existing collider's b2BodyId; the destroyer that calls b2DestroyBody MUST zero the sibling's m_BodyId to prevent aliasing on ID recycle.
		// Disabled physics keeps component data without creating a body.
		if (m_Scene.m_IsDetached || m_Scene.m_LifecycleSuspended || !EngineModules::IsActive(EngineModule::Physics2D) || !PhysicsSystem2D::IsInitialized()) return;

		if (!EnsureTransform2D(registry, entity, "Rigidbody2D")) return;

		bool isEnabled = !registry.all_of<DisabledTag>(entity);
		Rigidbody2DComponent& rb2D = registry.get<Rigidbody2DComponent>(entity);

		// Honor the component's body type instead of forcing Dynamic. For a freshly-added
		// rigidbody m_BodyType defaults to Dynamic, so editor/runtime/slow-load behavior is
		// unchanged; only an ECB fast-spawn (which byte-restores m_BodyType before the body
		// exists) differs — a prefab authored Static now spawns Static instead of Dynamic.
		if (m_Scene.HasAnyComponent<BoxCollider2DComponent>(entity)
			&& PhysicsSystem2D::GetMainPhysicsWorld().OwnsBody(m_Scene.GetComponent<BoxCollider2DComponent>(entity).m_BodyId, m_Scene, entity)) {
			rb2D.m_BodyId = m_Scene.GetComponent<BoxCollider2DComponent>(entity).m_BodyId;
			rb2D.SetBodyType(rb2D.m_BodyType);
		}
		else if (m_Scene.HasAnyComponent<CircleCollider2DComponent>(entity)
			&& PhysicsSystem2D::GetMainPhysicsWorld().OwnsBody(m_Scene.GetComponent<CircleCollider2DComponent>(entity).m_BodyId, m_Scene, entity)) {
			rb2D.m_BodyId = m_Scene.GetComponent<CircleCollider2DComponent>(entity).m_BodyId;
			rb2D.SetBodyType(rb2D.m_BodyType);
		}
		else if (m_Scene.HasAnyComponent<PolygonCollider2DComponent>(entity)
			&& PhysicsSystem2D::GetMainPhysicsWorld().OwnsBody(m_Scene.GetComponent<PolygonCollider2DComponent>(entity).m_BodyId, m_Scene, entity)) {
			rb2D.m_BodyId = m_Scene.GetComponent<PolygonCollider2DComponent>(entity).m_BodyId;
			rb2D.SetBodyType(rb2D.m_BodyType);
		}
		else {
			rb2D.m_BodyId = PhysicsSystem2D::GetMainPhysicsWorld().CreateBody(entity, m_Scene, rb2D.m_BodyType);
		}

		rb2D.SetEnabled(isEnabled);
	}

	void PhysicsScene2D::OnRigidBody2DComponentDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_Scene.m_IsDetached) return;
		auto& rb2D = m_Scene.GetComponent<Rigidbody2DComponent>(entity);

		if (!rb2D.IsValid()) return;

		const bool hasAnyCollider =
			registry.all_of<BoxCollider2DComponent>(entity) ||
			registry.all_of<CircleCollider2DComponent>(entity) ||
			registry.all_of<PolygonCollider2DComponent>(entity);

		if (m_Scene.IsEntityBeingDestroyed(entity)) {
			if (registry.all_of<BoxCollider2DComponent>(entity)) {
				registry.get<BoxCollider2DComponent>(entity).Destroy();
				rb2D.m_BodyId = b2_nullBodyId;
				return;
			}
			if (registry.all_of<CircleCollider2DComponent>(entity)) {
				registry.get<CircleCollider2DComponent>(entity).Destroy();
				rb2D.m_BodyId = b2_nullBodyId;
				return;
			}
			if (registry.all_of<PolygonCollider2DComponent>(entity)) {
				registry.get<PolygonCollider2DComponent>(entity).Destroy();
				rb2D.m_BodyId = b2_nullBodyId;
				return;
			}
		}

		if (hasAnyCollider) {
			rb2D.SetBodyType(BodyType::Static);
		}
		else {
			rb2D.Destroy();
			// Defensive: rb2D.Destroy() destroys the body. Make sure any mirror is
			// cleared even if the collider branch above didn't run.
			rb2D.m_BodyId = b2_nullBodyId;
		}
	}

	void PhysicsScene2D::OnBoxCollider2DComponentConstruct(entt::registry& registry, EntityHandle entity) {
		if (m_Scene.m_IsDetached || m_Scene.m_LifecycleSuspended || !EngineModules::IsActive(EngineModule::Physics2D) || !PhysicsSystem2D::IsInitialized()) return;
		if (!EnsureTransform2D(registry, entity, "BoxCollider2D")) return;

		bool isEnabled = !registry.all_of<DisabledTag>(entity);
		BoxCollider2DComponent& boxCollider = m_Scene.GetComponent<BoxCollider2DComponent>(entity);
		boxCollider.m_EntityHandle = entity;

		// Reuse existing rigidbody if present, else spawn a static body.
		if (m_Scene.HasComponent<Rigidbody2DComponent>(entity)) {
			auto& rb = m_Scene.GetComponent<Rigidbody2DComponent>(entity);
			boxCollider.m_BodyId = rb.GetBodyHandle();
		}
		else if (!PhysicsSystem2D::GetMainPhysicsWorld().OwnsBody(boxCollider.m_BodyId, m_Scene, entity)) {
			boxCollider.m_BodyId = PhysicsSystem2D::GetMainPhysicsWorld().CreateBody(entity, m_Scene, BodyType::Static);
		}

		boxCollider.m_ShapeId = PhysicsSystem2D::GetMainPhysicsWorld().CreateShape(entity, m_Scene, boxCollider.m_BodyId, ShapeType::Square);
		boxCollider.SetEnabled(isEnabled);

		// The shape attach made Box2D recompute mass from density*area; restore an
		// explicit mass if the rigidbody pinned one.
		if (m_Scene.HasComponent<Rigidbody2DComponent>(entity)) {
			m_Scene.GetComponent<Rigidbody2DComponent>(entity).ApplyMassFromState();
		}
	}

	void PhysicsScene2D::OnBoxCollider2DComponentDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_Scene.m_IsDetached) return;
		auto& boxCollider2D = m_Scene.GetComponent<BoxCollider2DComponent>(entity);
		// Rigidbody2D owns body lifetime when present; collider must NOT call b2DestroyBody or both hooks double-free on ID recycle.
		if (registry.all_of<Rigidbody2DComponent>(entity)) {
			boxCollider2D.DestroyShape(false);
		}
		else {
			boxCollider2D.Destroy();
		}
		if (registry.all_of<Rigidbody2DComponent>(entity)) {
			auto& rb = registry.get<Rigidbody2DComponent>(entity);
			if (!b2Body_IsValid(rb.m_BodyId)) {
				rb.m_BodyId = b2_nullBodyId;
			}
		}
	}

	void PhysicsScene2D::OnCircleCollider2DComponentConstruct(entt::registry& registry, EntityHandle entity) {
		if (m_Scene.m_IsDetached || m_Scene.m_LifecycleSuspended || !EngineModules::IsActive(EngineModule::Physics2D) || !PhysicsSystem2D::IsInitialized()) return;
		if (!EnsureTransform2D(registry, entity, "CircleCollider2D")) return;

		bool isEnabled = !registry.all_of<DisabledTag>(entity);
		CircleCollider2DComponent& circleCollider = m_Scene.GetComponent<CircleCollider2DComponent>(entity);
		circleCollider.m_EntityHandle = entity;

		// Reuse existing rigidbody body if present, else spawn a static body —
		// mirrors BoxCollider2D's body-sharing rules.
		if (m_Scene.HasComponent<Rigidbody2DComponent>(entity)) {
			auto& rb = m_Scene.GetComponent<Rigidbody2DComponent>(entity);
			circleCollider.m_BodyId = rb.GetBodyHandle();
		}
		else if (!PhysicsSystem2D::GetMainPhysicsWorld().OwnsBody(circleCollider.m_BodyId, m_Scene, entity)) {
			circleCollider.m_BodyId = PhysicsSystem2D::GetMainPhysicsWorld().CreateBody(entity, m_Scene, BodyType::Static);
		}

		circleCollider.m_ShapeId = PhysicsSystem2D::GetMainPhysicsWorld().CreateShape(entity, m_Scene, circleCollider.m_BodyId, ShapeType::Circle);
		circleCollider.SetEnabled(isEnabled);

		if (m_Scene.HasComponent<Rigidbody2DComponent>(entity)) {
			m_Scene.GetComponent<Rigidbody2DComponent>(entity).ApplyMassFromState();
		}
	}

	void PhysicsScene2D::OnCircleCollider2DComponentDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_Scene.m_IsDetached) return;
		auto& circleCollider = m_Scene.GetComponent<CircleCollider2DComponent>(entity);
		// See OnBoxCollider2DComponentDestroy: collider does NOT own the body when
		// a Rigidbody2D is present.
		if (registry.all_of<Rigidbody2DComponent>(entity)) {
			circleCollider.DestroyShape(false);
		}
		else {
			circleCollider.Destroy();
		}
		if (registry.all_of<Rigidbody2DComponent>(entity)) {
			auto& rb = registry.get<Rigidbody2DComponent>(entity);
			if (!b2Body_IsValid(rb.m_BodyId)) {
				rb.m_BodyId = b2_nullBodyId;
			}
		}
	}

	void PhysicsScene2D::OnPolygonCollider2DComponentConstruct(entt::registry& registry, EntityHandle entity) {
		if (m_Scene.m_IsDetached || m_Scene.m_LifecycleSuspended || !EngineModules::IsActive(EngineModule::Physics2D) || !PhysicsSystem2D::IsInitialized()) return;
		if (!EnsureTransform2D(registry, entity, "PolygonCollider2D")) return;

		bool isEnabled = !registry.all_of<DisabledTag>(entity);
		PolygonCollider2DComponent& polygonCollider = m_Scene.GetComponent<PolygonCollider2DComponent>(entity);
		polygonCollider.m_EntityHandle = entity;

		if (m_Scene.HasComponent<Rigidbody2DComponent>(entity)) {
			auto& rb = m_Scene.GetComponent<Rigidbody2DComponent>(entity);
			polygonCollider.m_BodyId = rb.GetBodyHandle();
		}
		else if (!PhysicsSystem2D::GetMainPhysicsWorld().OwnsBody(polygonCollider.m_BodyId, m_Scene, entity)) {
			polygonCollider.m_BodyId = PhysicsSystem2D::GetMainPhysicsWorld().CreateBody(entity, m_Scene, BodyType::Static);
		}

		polygonCollider.m_ShapeId = PhysicsSystem2D::GetMainPhysicsWorld().CreateShape(entity, m_Scene, polygonCollider.m_BodyId, ShapeType::Polygon);
		polygonCollider.SetEnabled(isEnabled);

		if (m_Scene.HasComponent<Rigidbody2DComponent>(entity)) {
			m_Scene.GetComponent<Rigidbody2DComponent>(entity).ApplyMassFromState();
		}
	}

	void PhysicsScene2D::OnPolygonCollider2DComponentDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_Scene.m_IsDetached) return;
		auto& polygonCollider = m_Scene.GetComponent<PolygonCollider2DComponent>(entity);
		// See OnBoxCollider2DComponentDestroy: collider does NOT own the body when
		// a Rigidbody2D is present.
		if (registry.all_of<Rigidbody2DComponent>(entity)) {
			polygonCollider.DestroyShape(false);
		}
		else {
			polygonCollider.Destroy();
		}
		if (registry.all_of<Rigidbody2DComponent>(entity)) {
			auto& rb = registry.get<Rigidbody2DComponent>(entity);
			if (!b2Body_IsValid(rb.m_BodyId)) {
				rb.m_BodyId = b2_nullBodyId;
			}
		}
	}
	template <typename TJoint>
	void PhysicsScene2D::OnJoint2DComponentDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_Scene.m_IsDetached) return;
		auto& joint = registry.get<TJoint>(entity);
		if (joint.m_Owner == entity) joint.DestroyJoint();
	}

	void PhysicsScene2D::Initialize() {
		if (m_Scene.m_IsDetached || m_Scene.m_LifecycleSuspended) return;
		if (EngineModules::IsActive(EngineModule::Physics2D) && PhysicsSystem2D::IsInitialized()) {
			auto& world = PhysicsSystem2D::GetMainPhysicsWorld();
			std::vector<EntityHandle> createdBodies;
			for (EntityHandle entity : m_Scene.m_Registry.view<Rigidbody2DComponent>()) {
				auto& rigidbody = m_Scene.m_Registry.get<Rigidbody2DComponent>(entity);
				if (world.OwnsBody(rigidbody.m_BodyId, m_Scene, entity)) continue;
				rigidbody.m_BodyId = b2_nullBodyId;
				OnRigidBody2DComponentConstruct(m_Scene.m_Registry, entity);
				createdBodies.push_back(entity);
			}
			auto initializeCollider = [&]<typename TCollider>(auto construct) {
				for (auto&& [entity, collider] : m_Scene.m_Registry.view<TCollider>().each()) {
					const bool ownedBody = world.OwnsBody(collider.m_BodyId, m_Scene, entity);
					if (!ownedBody) collider.m_BodyId = b2_nullBodyId;
					auto ownsShape = [&](b2ShapeId shape) {
						return ownedBody && b2Shape_IsValid(shape)
							&& B2_ID_EQUALS(b2Shape_GetBody(shape), collider.m_BodyId);
					};
					bool complete = ownsShape(collider.m_ShapeId);
					if (!complete) collider.m_ShapeId = b2_nullShapeId;
					if constexpr (std::is_same_v<TCollider, PolygonCollider2DComponent>) {
						const auto removedShapes = std::erase_if(collider.m_ExtraShapeIds,
							[&](b2ShapeId shape) { return !ownsShape(shape); });
						const auto removedChains = std::erase_if(collider.m_ChainIds, [&](b2ChainId chain) {
							if (!ownedBody || !b2Chain_IsValid(chain)) return true;
							b2ShapeId segment = b2_nullShapeId;
							return b2Chain_GetSegments(chain, &segment, 1) != 1 || !ownsShape(segment);
						});
						complete = complete && removedShapes == 0 && removedChains == 0;
					}
					collider.m_EntityHandle = entity;
					if (complete) continue;
					if (!collider.IsValid()) (this->*construct)(m_Scene.m_Registry, entity);
					collider.RestoreShapeFromState(m_Scene);
				}
			};
			initializeCollider.operator()<BoxCollider2DComponent>(&PhysicsScene2D::OnBoxCollider2DComponentConstruct);
			initializeCollider.operator()<CircleCollider2DComponent>(&PhysicsScene2D::OnCircleCollider2DComponentConstruct);
			initializeCollider.operator()<PolygonCollider2DComponent>(&PhysicsScene2D::OnPolygonCollider2DComponentConstruct);
			for (EntityHandle entity : createdBodies) {
				m_Scene.m_Registry.get<Rigidbody2DComponent>(entity).RestoreBodyFromState();
			}
		}
	}

	void PhysicsScene2D::SetEnabled(EntityHandle entity, bool enabled) {
		auto& registry = m_Scene.GetRegistry();
		if (auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity); rigidbody && rigidbody->IsValid()) {
			rigidbody->SetEnabled(enabled);
		}

		if (auto* collider = registry.try_get<BoxCollider2DComponent>(entity); collider && collider->IsValid()) {
			collider->SetEnabled(enabled);
		}

		if (auto* collider = registry.try_get<CircleCollider2DComponent>(entity); collider && collider->IsValid()) {
			collider->SetEnabled(enabled);
		}

		if (auto* collider = registry.try_get<PolygonCollider2DComponent>(entity); collider && collider->IsValid()) {
			collider->SetEnabled(enabled);
		}
	}

	void PhysicsScene2D::Connect() {
		auto& registry = m_Scene.GetRegistry();
		registry.on_construct<Rigidbody2DComponent>().connect<&PhysicsScene2D::OnRigidBody2DComponentConstruct>(this);
		registry.on_destroy<Rigidbody2DComponent>().connect<&PhysicsScene2D::OnRigidBody2DComponentDestroy>(this);
		registry.on_construct<BoxCollider2DComponent>().connect<&PhysicsScene2D::OnBoxCollider2DComponentConstruct>(this);
		registry.on_destroy<BoxCollider2DComponent>().connect<&PhysicsScene2D::OnBoxCollider2DComponentDestroy>(this);
		registry.on_construct<CircleCollider2DComponent>().connect<&PhysicsScene2D::OnCircleCollider2DComponentConstruct>(this);
		registry.on_destroy<CircleCollider2DComponent>().connect<&PhysicsScene2D::OnCircleCollider2DComponentDestroy>(this);
		registry.on_construct<PolygonCollider2DComponent>().connect<&PhysicsScene2D::OnPolygonCollider2DComponentConstruct>(this);
		registry.on_destroy<PolygonCollider2DComponent>().connect<&PhysicsScene2D::OnPolygonCollider2DComponentDestroy>(this);
		registry.on_destroy<RevoluteJoint2DComponent>().connect<&PhysicsScene2D::OnJoint2DComponentDestroy<RevoluteJoint2DComponent>>(this);
		registry.on_destroy<PrismaticJoint2DComponent>().connect<&PhysicsScene2D::OnJoint2DComponentDestroy<PrismaticJoint2DComponent>>(this);
		registry.on_destroy<WeldJoint2DComponent>().connect<&PhysicsScene2D::OnJoint2DComponentDestroy<WeldJoint2DComponent>>(this);
	}

	void PhysicsScene2D::Disconnect() {
		auto& registry = m_Scene.GetRegistry();
		registry.on_construct<Rigidbody2DComponent>().disconnect<&PhysicsScene2D::OnRigidBody2DComponentConstruct>(this);
		registry.on_destroy<Rigidbody2DComponent>().disconnect<&PhysicsScene2D::OnRigidBody2DComponentDestroy>(this);
		registry.on_construct<BoxCollider2DComponent>().disconnect<&PhysicsScene2D::OnBoxCollider2DComponentConstruct>(this);
		registry.on_destroy<BoxCollider2DComponent>().disconnect<&PhysicsScene2D::OnBoxCollider2DComponentDestroy>(this);
		registry.on_construct<CircleCollider2DComponent>().disconnect<&PhysicsScene2D::OnCircleCollider2DComponentConstruct>(this);
		registry.on_destroy<CircleCollider2DComponent>().disconnect<&PhysicsScene2D::OnCircleCollider2DComponentDestroy>(this);
		registry.on_construct<PolygonCollider2DComponent>().disconnect<&PhysicsScene2D::OnPolygonCollider2DComponentConstruct>(this);
		registry.on_destroy<PolygonCollider2DComponent>().disconnect<&PhysicsScene2D::OnPolygonCollider2DComponentDestroy>(this);
		registry.on_destroy<RevoluteJoint2DComponent>().disconnect<&PhysicsScene2D::OnJoint2DComponentDestroy<RevoluteJoint2DComponent>>(this);
		registry.on_destroy<PrismaticJoint2DComponent>().disconnect<&PhysicsScene2D::OnJoint2DComponentDestroy<PrismaticJoint2DComponent>>(this);
		registry.on_destroy<WeldJoint2DComponent>().disconnect<&PhysicsScene2D::OnJoint2DComponentDestroy<WeldJoint2DComponent>>(this);
	}

	SceneExtension PhysicsScene2D::Extension() {
		return {
			"Funke.Physics2D",
			[](Scene& scene) {
				if (!EngineModules::IsActive(EngineModule::Physics2D)) return;
				auto& context = scene.GetRegistry().ctx();
				if (context.contains<std::unique_ptr<PhysicsScene2D>>()) return;
				auto& state = context.emplace<std::unique_ptr<PhysicsScene2D>>(std::make_unique<PhysicsScene2D>(scene));
				state->Connect();
				PhysicsSystem2D::RegisterTransformAdapter(scene);
			},
			[](Scene& scene) {
				if (auto* state = scene.GetRegistry().ctx().find<std::unique_ptr<PhysicsScene2D>>()) (*state)->Initialize();
			},
			[](Scene& scene) {
				auto& context = scene.GetRegistry().ctx();
				if (auto* state = context.find<std::unique_ptr<PhysicsScene2D>>()) {
					(*state)->Disconnect();
					context.erase<std::unique_ptr<PhysicsScene2D>>();
					context.erase<Transform2DExternalAdapter>();
				}
				if (PhysicsSystem2D::IsInitialized()) {
					try {
						PhysicsSystem2D::GetMainPhysicsWorld().DestroyAllBodiesForScene(&scene);
					}
					catch (...) {
						FNK_CORE_ERROR_TAG("Physics2D", "Body cleanup for scene '{}' failed", scene.GetName());
					}
				}
			},
			[](Scene& scene, EntityHandle entity, bool enabled) {
				if (auto* state = scene.GetRegistry().ctx().find<std::unique_ptr<PhysicsScene2D>>()) (*state)->SetEnabled(entity, enabled);
			}
		};
	}
}
