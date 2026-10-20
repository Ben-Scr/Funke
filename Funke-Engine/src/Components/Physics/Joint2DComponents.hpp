#pragma once
#include "Collections/Vec2.hpp"
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"

#include <box2d/id.h>
#include <cstdint>

namespace Funke {
	// Fields shared by the 2D joints. The entity's body (its Rigidbody2D, or a collider's static body) is body A;
	// ConnectedBody is body B, or the world when null. PhysicsSystem2D builds the Box2D joint during play once both
	// bodies exist and rebuilds it when one of them is recreated or a structural field changes.
	struct FUNKE_API Joint2DBase {
		EntityHandle ConnectedBody = entt::null;
		// In this body's frame (meters, unaffected by the transform's scale).
		Vec2 Anchor{ 0.0f, 0.0f };
		// In ConnectedBody's frame, or world space without one. Ignored while AutoConfigureConnectedAnchor is on:
		// the joint then pins the anchor where it is when the joint is built.
		Vec2 ConnectedAnchor{ 0.0f, 0.0f };
		bool AutoConfigureConnectedAnchor = true;
		bool CollideConnected = false;

		bool IsCreated() const;
		// Force (N) and torque (N·m) the joint applied to body B in the last step.
		Vec2 GetReactionForce() const;
		float GetReactionTorque() const;

		// Call after changing ConnectedBody, the anchors, Axis or CollideConnected from code.
		void MarkStructuralChange() { ++m_Revision; }
		void DestroyJoint();

		// Runtime state, never serialized. m_Owner guards against a copied component reusing the source's joint.
		b2JointId m_JointId = b2_nullJointId;
		EntityHandle m_Owner = entt::null;
		b2BodyId m_BodyA = b2_nullBodyId;
		b2BodyId m_BodyB = b2_nullBodyId;
		uint32_t m_Revision = 1;
		uint32_t m_BuiltRevision = 0;
	};

	// Hinge: body B turns around the anchor. Angles are relative to the pose the bodies had when the joint was built.
	struct FUNKE_API RevoluteJoint2DComponent : Joint2DBase {
		bool EnableLimit = false;
		float LowerAngle = 0.0f;          // radians
		float UpperAngle = 0.0f;          // radians
		bool EnableMotor = false;
		float MotorSpeed = 0.0f;          // radians per second
		float MaxMotorTorque = 0.0f;      // N·m
		bool EnableSpring = false;
		float SpringHertz = 0.0f;
		float SpringDampingRatio = 0.0f;

		// Pushes limits, motor and spring into the live joint.
		void ApplyLive();
		float GetJointAngle() const;
		float GetJointSpeed() const;
		float GetMotorTorque() const;
	};

	// Slider: body B moves along Axis (in body A's frame) through the anchor, without turning relative to A.
	struct FUNKE_API PrismaticJoint2DComponent : Joint2DBase {
		Vec2 Axis{ 1.0f, 0.0f };
		bool EnableLimit = false;
		float LowerTranslation = 0.0f;    // meters
		float UpperTranslation = 0.0f;
		bool EnableMotor = false;
		float MotorSpeed = 0.0f;          // meters per second
		float MaxMotorForce = 0.0f;       // N
		bool EnableSpring = false;
		float SpringHertz = 0.0f;
		float SpringDampingRatio = 0.0f;

		void ApplyLive();
		float GetJointTranslation() const;
		float GetJointSpeed() const;
		float GetMotorForce() const;
	};

	// Glues body B to body A in their current relative pose. 0 Hz keeps it rigid; above 0 it springs.
	struct FUNKE_API WeldJoint2DComponent : Joint2DBase {
		float LinearHertz = 0.0f;
		float AngularHertz = 0.0f;
		float LinearDampingRatio = 1.0f;
		float AngularDampingRatio = 1.0f;

		void ApplyLive();
	};
}
