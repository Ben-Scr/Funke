#pragma once
#include "Collections/Vec2.hpp"
#include "Core/Export.hpp"
#include "Physics/PhysicsTypes.hpp"
#include <box2d/types.h>

namespace Funke {
	class Scene;
	class Transform2DComponent;
}

namespace Funke {
	class FUNKE_API Rigidbody2DComponent {
	public:
		Rigidbody2DComponent() = default;

		void SetBodyType(BodyType bodyType);
		BodyType GetBodyType() const;

		bool IsAwake() const;

		void SetVelocity(const Vec2& velocity);
		Vec2 GetVelocity() const;

		void SetAngularVelocity(float velocity);
		float GetAngularVelocity() const;

		void SetGravityScale(float gravityScale);
		float GetGravityScale() const;

		void SetMass(float mass);
		void ResetMass();
		float GetMass() const;
		bool IsMassOverridden() const { return m_MassOverridden; }
		// Re-assert an explicit mass after shapes attach. Box2D recomputes a body's
		// mass from collider density*area on every shape create, which would discard
		// a SetMass value; call this after attaching/recreating shapes. No-op unless
		// the mass was explicitly overridden, so density-based auto mass is preserved.
		void ApplyMassFromState();

		// Push the byte-restored logical state (gravity scale, freeze locks, pinned mass) into the
		// live b2 body after an ECB fast-spawn, where the construct hook created the body with
		// defaults. Body type is already honored by the construct hook. Mirrors the slow-load setters.
		void RestoreBodyFromState();

		// Box2D velocity damping; new bodies get 0.1 linear and 0 angular.
		void SetLinearDrag(float value);
		void SetAngularDrag(float value);
		float GetLinearDrag() const;
		float GetAngularDrag() const;
		// Continuous collision against other dynamic bodies (fast projectiles, ragdoll limbs).
		void SetBullet(bool bullet);
		bool IsBullet() const;

		// Rotational inertia about the centre of mass (kg·m²) and the centre of mass in body and world space.
		float GetInertia() const;
		Vec2 GetLocalCenterOfMass() const;
		Vec2 GetWorldCenterOfMass() const;
		void ApplyForceAtPoint(const Vec2& force, const Vec2& worldPoint, bool wake);
		void ApplyImpulseAtPoint(const Vec2& impulse, const Vec2& worldPoint, bool wake);
		void ApplyTorque(float torque, bool wake);
		void ApplyAngularImpulse(float impulse, bool wake);

		void SetRotation(float radians);
		void SetPosition(const Vec2& position);
		Vec2 GetPosition() const;

		void SetTransform(const Transform2DComponent& tr);

		float GetRotation() const;

		void SetEnabled(bool enabled);

		// Getters return false before the body is spawned (safe for inspector reads on new entities).
		void SetFreezePositionX(bool freeze);
		void SetFreezePositionY(bool freeze);
		void SetFreezeRotation(bool freeze);
		bool GetFreezePositionX() const;
		bool GetFreezePositionY() const;
		bool GetFreezeRotation() const;

		b2BodyId GetBodyHandle() const;
		bool IsValid() const;

		void Destroy();
	private:
		b2BodyId m_BodyId{ b2_nullBodyId };

		// Logical state — authoritative when there is no live b2 body (prefab editing
		// happens in a detached scene that never creates one). Setters mirror into
		// Box2D when the body exists; getters read Box2D when valid, else these. This
		// lets the inspector edit + serialization round-trip work without a live body.
		BodyType m_BodyType{ BodyType::Dynamic };
		float m_GravityScale{ 1.0f };
		float m_Mass{ 1.0f };
		// When false, mass tracks Box2D's density*area auto computation; when true the
		// user/serialized m_Mass is authoritative and re-asserted after shape attach.
		bool m_MassOverridden{ false };
		bool m_FreezeX{ false };
		bool m_FreezeY{ false };
		bool m_FreezeRot{ false };
		bool m_Bullet{ false };
		float m_LinearDamping{ 0.1f };
		float m_AngularDamping{ 0.0f };

		friend class BoxCollider2DComponent;
		friend class Collider2D;
		friend class PhysicsSystem2D;
		friend class Physics2D;
		friend class Scene;
	};
}
