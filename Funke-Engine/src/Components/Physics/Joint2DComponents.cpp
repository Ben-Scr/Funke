#include "pch.hpp"
#include "Components/Physics/Joint2DComponents.hpp"

#include <box2d/box2d.h>

namespace Funke {
	bool Joint2DBase::IsCreated() const {
		return b2Joint_IsValid(m_JointId);
	}

	Vec2 Joint2DBase::GetReactionForce() const {
		if (!b2Joint_IsValid(m_JointId)) return Vec2{ 0.0f, 0.0f };
		const b2Vec2 f = b2Joint_GetConstraintForce(m_JointId);
		return Vec2{ f.x, f.y };
	}

	float Joint2DBase::GetReactionTorque() const {
		return b2Joint_IsValid(m_JointId) ? b2Joint_GetConstraintTorque(m_JointId) : 0.0f;
	}

	void Joint2DBase::DestroyJoint() {
		if (b2Joint_IsValid(m_JointId)) {
			b2DestroyJoint(m_JointId, true);
		}
		m_JointId = b2_nullJointId;
		m_BodyA = b2_nullBodyId;
		m_BodyB = b2_nullBodyId;
		m_BuiltRevision = 0;
	}

	void RevoluteJoint2DComponent::ApplyLive() {
		if (!b2Joint_IsValid(m_JointId)) return;
		b2RevoluteJoint_SetLimits(m_JointId, std::min(LowerAngle, UpperAngle), std::max(LowerAngle, UpperAngle));
		b2RevoluteJoint_EnableLimit(m_JointId, EnableLimit);
		b2RevoluteJoint_SetMaxMotorTorque(m_JointId, std::max(MaxMotorTorque, 0.0f));
		b2RevoluteJoint_SetMotorSpeed(m_JointId, MotorSpeed);
		b2RevoluteJoint_EnableMotor(m_JointId, EnableMotor);
		b2RevoluteJoint_SetSpringHertz(m_JointId, std::max(SpringHertz, 0.0f));
		b2RevoluteJoint_SetSpringDampingRatio(m_JointId, std::max(SpringDampingRatio, 0.0f));
		b2RevoluteJoint_EnableSpring(m_JointId, EnableSpring);
		b2Joint_WakeBodies(m_JointId);
	}

	float RevoluteJoint2DComponent::GetJointAngle() const {
		return b2Joint_IsValid(m_JointId) ? b2RevoluteJoint_GetAngle(m_JointId) : 0.0f;
	}

	float RevoluteJoint2DComponent::GetJointSpeed() const {
		if (!b2Joint_IsValid(m_JointId)) return 0.0f;
		return b2Body_GetAngularVelocity(b2Joint_GetBodyB(m_JointId)) - b2Body_GetAngularVelocity(b2Joint_GetBodyA(m_JointId));
	}

	float RevoluteJoint2DComponent::GetMotorTorque() const {
		return b2Joint_IsValid(m_JointId) ? b2RevoluteJoint_GetMotorTorque(m_JointId) : 0.0f;
	}

	void PrismaticJoint2DComponent::ApplyLive() {
		if (!b2Joint_IsValid(m_JointId)) return;
		b2PrismaticJoint_SetLimits(m_JointId, std::min(LowerTranslation, UpperTranslation), std::max(LowerTranslation, UpperTranslation));
		b2PrismaticJoint_EnableLimit(m_JointId, EnableLimit);
		b2PrismaticJoint_SetMaxMotorForce(m_JointId, std::max(MaxMotorForce, 0.0f));
		b2PrismaticJoint_SetMotorSpeed(m_JointId, MotorSpeed);
		b2PrismaticJoint_EnableMotor(m_JointId, EnableMotor);
		b2PrismaticJoint_SetSpringHertz(m_JointId, std::max(SpringHertz, 0.0f));
		b2PrismaticJoint_SetSpringDampingRatio(m_JointId, std::max(SpringDampingRatio, 0.0f));
		b2PrismaticJoint_EnableSpring(m_JointId, EnableSpring);
		b2Joint_WakeBodies(m_JointId);
	}

	float PrismaticJoint2DComponent::GetJointTranslation() const {
		return b2Joint_IsValid(m_JointId) ? b2PrismaticJoint_GetTranslation(m_JointId) : 0.0f;
	}

	float PrismaticJoint2DComponent::GetJointSpeed() const {
		return b2Joint_IsValid(m_JointId) ? b2PrismaticJoint_GetSpeed(m_JointId) : 0.0f;
	}

	float PrismaticJoint2DComponent::GetMotorForce() const {
		return b2Joint_IsValid(m_JointId) ? b2PrismaticJoint_GetMotorForce(m_JointId) : 0.0f;
	}

	void WeldJoint2DComponent::ApplyLive() {
		if (!b2Joint_IsValid(m_JointId)) return;
		b2WeldJoint_SetLinearHertz(m_JointId, std::max(LinearHertz, 0.0f));
		b2WeldJoint_SetAngularHertz(m_JointId, std::max(AngularHertz, 0.0f));
		b2WeldJoint_SetLinearDampingRatio(m_JointId, std::max(LinearDampingRatio, 0.0f));
		b2WeldJoint_SetAngularDampingRatio(m_JointId, std::max(AngularDampingRatio, 0.0f));
		b2Joint_WakeBodies(m_JointId);
	}
}
