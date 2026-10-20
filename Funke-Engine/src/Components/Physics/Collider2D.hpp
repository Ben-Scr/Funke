#pragma once
#include "Core/Export.hpp"
#include "Rigidbody2DComponent.hpp"
#include <Core/Assert.hpp>
#include "Physics/PhysicsSystem2D.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Scene/EntityHandle.hpp"
#include "Core/Exceptions.hpp"
#include <box2d/box2d.h>



namespace Funke {
	struct Collision2D;

	class FUNKE_API Collider2D {
	public:
		Collider2D() = default;
		explicit Collider2D(EntityHandle entity)
			: m_EntityHandle(entity) {}

		bool IsValid() const;
		void SetFriction(float friction);
		float GetFriction() const;
		void SetBounciness(float bounciness);
		float GetBounciness() const;
		// Mass per area (kg/m²); a Rigidbody2D without a pinned mass sums its colliders' density × area.
		void SetDensity(float density);
		float GetDensity() const;
		// Layers this collider belongs to (Box2D categoryBits).
		void SetLayer(uint64_t layer);
		uint64_t GetLayer() const;
		// Layers this collider collides with (maskBits). Two colliders touch only if each one's mask contains the other's layer.
		void SetCollisionMask(uint64_t mask);
		uint64_t GetCollisionMask() const;
		// Box2D group: colliders with the same negative group never touch, the same positive group always touches.
		void SetGroupIndex(int group);
		int GetGroupIndex() const;
		bool IsEnabled() const;
		bool IsSensor() const;

		Vec2 GetBodyPosition() const;
		float GetRotationDegrees() const;

		void SetRegisterContacts(bool enabled);
		bool CanRegisterContacts() const;

		template<typename F>
		void OnCollisionEnter(F&& callback) {
			FNK_ASSERT(CanRegisterContacts(), FunkeErrorCode::InvalidArgument,
				"Failed to register OnCollisionEnter event: contact events are not enabled for this shape. "
				"Make sure to call collider.SetRegisterContacts(true); before registering event callbacks."
			);

			ContactBeginCallback cb(callback);
			PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher().RegisterBegin(m_ShapeId, std::move(cb));
		}
		template<typename F>
		void OnCollisionExit(F&& callback) {
			FNK_ASSERT(CanRegisterContacts(), FunkeErrorCode::InvalidArgument,
				"Failed to register OnCollisionExit event: contact events are not enabled for this shape. "
				"Make sure to call collider.SetRegisterContacts(true); before registering event callbacks."
			);

			PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher().RegisterEnd(m_ShapeId, std::forward<F>(callback));
		}
		template<typename F>
		void OnCollisionHit(F&& callback) {
			FNK_ASSERT(CanRegisterContacts(), FunkeErrorCode::InvalidArgument,
				"Failed to register OnCollisionHit event: contact events are not enabled for this shape. "
				"Make sure to call collider.SetRegisterContacts(true); before registering event callbacks."
			);

			PhysicsSystem2D::GetMainPhysicsWorld().GetDispatcher().RegisterHit(m_ShapeId, std::forward<F>(callback));
		}

		void EnableRotation(bool enabled);


		float GetRotationRadiant() const;

		// Virtual so derived colliders (BoxCollider2DComponent, ...) can Destroy()-shadow
		// without slicing — calling Destroy() through a Collider2D* must reach the
		// derived override (e.g. so derived per-shape members get reset).
		virtual ~Collider2D() = default;
		virtual void Destroy();
		virtual void DestroyShape(bool updateBodyMass = true);

		b2BodyId m_BodyId{ b2_nullBodyId };
		b2ShapeId m_ShapeId{ b2_nullShapeId };

		EntityHandle m_EntityHandle{ entt::null };

		// Logical state — authoritative when there is no live b2 shape (prefab editing
		// runs in a detached scene that never creates one). Setters mirror into Box2D
		// when valid; getters read Box2D when valid, else these. Defaults match a
		// freshly created shape (see Box2DWorld::CreateShape).
		float m_Friction{ 0.3f };
		float m_Bounciness{ 0.0f };
		float m_Density{ 1.0f };
		uint64_t m_Layer{ 1 };
		uint64_t m_CollisionMask{ UINT64_MAX };
		int m_GroupIndex{ 0 };
		bool m_Sensor{ false };
		bool m_RegisterContacts{ false };
	protected:
		// Shapes besides m_ShapeId that belong to this collider (a decomposed polygon); setters apply to them too.
		virtual int GetExtraShapes(const b2ShapeId*& outShapes) const { outShapes = nullptr; return 0; }
	private:
		void SetRotation(float radiant);
		void SetPositionRotation(const Vec2& position, float radiant);
		void SetPosition(const Vec2& position);
		void SetTransform(const Transform2DComponent& tr);

		friend class Physics2D;
		friend class PhysicsSystem2D;
	};
}
