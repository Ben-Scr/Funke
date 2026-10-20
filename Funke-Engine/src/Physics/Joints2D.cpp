#include "pch.hpp"
#include "Physics/Joints2D.hpp"

#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/Joint2DComponents.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Tags.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Scene/Scene.hpp"

#include <box2d/box2d.h>
#include <cmath>

namespace Funke::Joints2D {
	b2BodyId ResolveBody(const entt::registry& registry, EntityHandle entity) {
		if (entity == entt::null || !registry.valid(entity)) return b2_nullBodyId;
		if (const auto* rb = registry.try_get<Rigidbody2DComponent>(entity); rb && rb->IsValid()) return rb->GetBodyHandle();
		if (const auto* c = registry.try_get<BoxCollider2DComponent>(entity); c && b2Body_IsValid(c->m_BodyId)) return c->m_BodyId;
		if (const auto* c = registry.try_get<CircleCollider2DComponent>(entity); c && b2Body_IsValid(c->m_BodyId)) return c->m_BodyId;
		if (const auto* c = registry.try_get<PolygonCollider2DComponent>(entity); c && b2Body_IsValid(c->m_BodyId)) return c->m_BodyId;
		return b2_nullBodyId;
	}

	namespace {
		struct Frames {
			b2Transform A;
			b2Transform B;
		};

		// Frame A sits on Anchor, turned to frameAngleA; frame B coincides with it in world space at build time,
		// so angles and translations start at zero from the authored pose.
		Frames BuildFrames(const Joint2DBase& joint, b2BodyId bodyA, b2BodyId bodyB, float frameAngleA) {
			const b2Transform xfA = b2Body_GetTransform(bodyA);
			const b2Transform xfB = b2Body_GetTransform(bodyB);
			const b2Vec2 anchorA{ joint.Anchor.x, joint.Anchor.y };
			const b2Vec2 anchorB = joint.AutoConfigureConnectedAnchor
				? b2InvTransformPoint(xfB, b2TransformPoint(xfA, anchorA))
				: b2Vec2{ joint.ConnectedAnchor.x, joint.ConnectedAnchor.y };
			const b2Rot rotA = b2MakeRot(frameAngleA);
			const b2Rot rotB = b2InvMulRot(xfB.q, b2MulRot(xfA.q, rotA));
			return Frames{ b2Transform{ anchorA, rotA }, b2Transform{ anchorB, rotB } };
		}

		void FillBase(b2JointDef& def, const Joint2DBase& joint, b2BodyId bodyA, b2BodyId bodyB, const Frames& frames) {
			def.bodyIdA = bodyA;
			def.bodyIdB = bodyB;
			def.localFrameA = frames.A;
			def.localFrameB = frames.B;
			def.collideConnected = joint.CollideConnected;
		}

		b2JointId Create(b2WorldId world, const RevoluteJoint2DComponent& j, b2BodyId a, b2BodyId b) {
			b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
			FillBase(def.base, j, a, b, BuildFrames(j, a, b, 0.0f));
			def.enableLimit = j.EnableLimit;
			def.lowerAngle = std::min(j.LowerAngle, j.UpperAngle);
			def.upperAngle = std::max(j.LowerAngle, j.UpperAngle);
			def.enableMotor = j.EnableMotor;
			def.motorSpeed = j.MotorSpeed;
			def.maxMotorTorque = std::max(j.MaxMotorTorque, 0.0f);
			def.enableSpring = j.EnableSpring;
			def.hertz = std::max(j.SpringHertz, 0.0f);
			def.dampingRatio = std::max(j.SpringDampingRatio, 0.0f);
			return b2CreateRevoluteJoint(world, &def);
		}

		b2JointId Create(b2WorldId world, const PrismaticJoint2DComponent& j, b2BodyId a, b2BodyId b) {
			b2PrismaticJointDef def = b2DefaultPrismaticJointDef();
			const bool hasAxis = j.Axis.x != 0.0f || j.Axis.y != 0.0f;
			const float axisAngle = hasAxis ? std::atan2(j.Axis.y, j.Axis.x) : 0.0f;
			FillBase(def.base, j, a, b, BuildFrames(j, a, b, axisAngle));
			def.enableLimit = j.EnableLimit;
			def.lowerTranslation = std::min(j.LowerTranslation, j.UpperTranslation);
			def.upperTranslation = std::max(j.LowerTranslation, j.UpperTranslation);
			def.enableMotor = j.EnableMotor;
			def.motorSpeed = j.MotorSpeed;
			def.maxMotorForce = std::max(j.MaxMotorForce, 0.0f);
			def.enableSpring = j.EnableSpring;
			def.hertz = std::max(j.SpringHertz, 0.0f);
			def.dampingRatio = std::max(j.SpringDampingRatio, 0.0f);
			return b2CreatePrismaticJoint(world, &def);
		}

		b2JointId Create(b2WorldId world, const WeldJoint2DComponent& j, b2BodyId a, b2BodyId b) {
			b2WeldJointDef def = b2DefaultWeldJointDef();
			FillBase(def.base, j, a, b, BuildFrames(j, a, b, 0.0f));
			def.linearHertz = std::max(j.LinearHertz, 0.0f);
			def.angularHertz = std::max(j.AngularHertz, 0.0f);
			def.linearDampingRatio = std::max(j.LinearDampingRatio, 0.0f);
			def.angularDampingRatio = std::max(j.AngularDampingRatio, 0.0f);
			return b2CreateWeldJoint(world, &def);
		}

		template <typename T>
		void SyncType(Scene& scene, Box2DWorld& world) {
			auto& registry = scene.GetRegistry();
			for (auto [entity, joint] : registry.view<T>(entt::exclude<DisabledTag>).each()) {
				if (joint.m_Owner != entity) {
					// A copy of another entity's component: the joint id belongs to the source.
					joint.m_JointId = b2_nullJointId;
					joint.m_BodyA = b2_nullBodyId;
					joint.m_BodyB = b2_nullBodyId;
					joint.m_BuiltRevision = 0;
					joint.m_Owner = entity;
				}

				const b2BodyId bodyA = ResolveBody(registry, entity);
				b2BodyId bodyB = b2_nullBodyId;
				if (joint.ConnectedBody == entt::null) {
					bodyB = world.GetGroundBody();
				}
				else if (registry.valid(joint.ConnectedBody) && !registry.all_of<DisabledTag>(joint.ConnectedBody)) {
					bodyB = ResolveBody(registry, joint.ConnectedBody);
				}

				if (!b2Body_IsValid(bodyA) || !b2Body_IsValid(bodyB) || B2_ID_EQUALS(bodyA, bodyB)) {
					if (joint.IsCreated()) joint.DestroyJoint();
					continue;
				}

				const bool upToDate = joint.IsCreated()
					&& B2_ID_EQUALS(joint.m_BodyA, bodyA)
					&& B2_ID_EQUALS(joint.m_BodyB, bodyB)
					&& joint.m_BuiltRevision == joint.m_Revision;
				if (upToDate) continue;

				joint.DestroyJoint();
				joint.m_JointId = Create(world.GetWorldID(), joint, bodyA, bodyB);
				joint.m_BodyA = bodyA;
				joint.m_BodyB = bodyB;
				joint.m_BuiltRevision = joint.m_Revision;
			}
		}

		template <typename T>
		void DestroyType(Scene& scene) {
			for (auto [entity, joint] : scene.GetRegistry().view<T>().each()) {
				if (joint.m_Owner == entity && joint.IsCreated()) joint.DestroyJoint();
			}
		}
	}

	void Sync(Scene& scene, Box2DWorld& world) {
		SyncType<RevoluteJoint2DComponent>(scene, world);
		SyncType<PrismaticJoint2DComponent>(scene, world);
		SyncType<WeldJoint2DComponent>(scene, world);
	}

	void DestroyAll(Scene& scene) {
		DestroyType<RevoluteJoint2DComponent>(scene);
		DestroyType<PrismaticJoint2DComponent>(scene);
		DestroyType<WeldJoint2DComponent>(scene);
	}
}
