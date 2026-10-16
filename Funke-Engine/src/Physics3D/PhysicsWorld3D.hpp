#pragma once
#include "Core/Export.hpp"
#include "Collections/Vec2.hpp"
#include "Scene/EntityHandle.hpp"

#include <functional>
#include <memory>

namespace Funke {
	class Scene;

	enum class ForceMode3D : int {
		Force = 0,          // mass * distance / time^2, applied over the next step
		Impulse = 1,        // mass * distance / time, applied instantly
		Acceleration = 2,   // like Force but ignores mass
		VelocityChange = 3  // like Impulse but ignores mass
	};

	struct RaycastHit3D {
		EntityHandle Entity = entt::null;
		Vec3 Point{ 0.0f };
		Vec3 Normal{ 0.0f };
		float Distance = 0.0f;
	};

	struct Collision3D {
		Scene* OwningScene = nullptr;
		EntityHandle EntityA = entt::null;
		EntityHandle EntityB = entt::null;
		Vec3 ContactPoint{ 0.0f };
		// Points from A to B.
		Vec3 Normal{ 0.0f };
		// At least one side is a trigger: routed to OnTrigger*3D.
		bool IsTrigger = false;
	};

	enum class CollisionPhase3D : int { Enter = 0, Stay = 1, Exit = 2 };

	// One Jolt world per scene. Entities stay the source of truth: Simulate() rebuilds bodies whose
	// Rigidbody3D/collider fields or scale changed, teleports bodies whose Transform3D was moved from
	// outside, steps, and writes dynamic bodies back into Transform3D.
	class FUNKE_API PhysicsWorld3D {
	public:
		PhysicsWorld3D();
		~PhysicsWorld3D();

		PhysicsWorld3D(const PhysicsWorld3D&) = delete;
		PhysicsWorld3D& operator=(const PhysicsWorld3D&) = delete;

		void Simulate(Scene& scene, float dt);

		// Building blocks of Simulate, public for tests.
		void SyncFromScene(Scene& scene, float dt);
		void Step(float dt);
		void WriteBackToScene(Scene& scene);
		// Per frame: moves interpolating bodies' Transform3D between their last two steps by alpha (0..1).
		void Present(Scene& scene, float alpha);

		// Contacts are paired per body, so a compound body touching another reports once.
		void DispatchContacts(const std::function<void(const Collision3D&, CollisionPhase3D)>& callback);
		// Every touching body pair with its latest point and normal (debug drawing); the callback must not touch the world.
		void ForEachActiveContact(const std::function<void(const Vec3& point, const Vec3& normal, bool isTrigger)>& callback) const;

		bool Raycast(const Vec3& origin, const Vec3& direction, float maxDistance, RaycastHit3D& outHit) const;

		Vec3 GetGravity() const;
		void SetGravity(const Vec3& gravity);

		size_t GetBodyCount() const;
		// The entity owning the body that holds `entity`'s colliders (itself or a Rigidbody3D ancestor).
		EntityHandle FindBodyOwner(Scene& scene, EntityHandle entity) const;
		// Creates or refreshes the entity's body now, so script calls before the first step aren't lost.
		bool EnsureBody(Scene& scene, EntityHandle owner);

		Vec3 GetLinearVelocity(EntityHandle owner) const;
		void SetLinearVelocity(EntityHandle owner, const Vec3& velocity);
		Vec3 GetAngularVelocity(EntityHandle owner) const;
		void SetAngularVelocity(EntityHandle owner, const Vec3& velocity);
		void AddForce(EntityHandle owner, const Vec3& force, ForceMode3D mode);
		void AddForceAtPosition(EntityHandle owner, const Vec3& force, const Vec3& worldPosition, ForceMode3D mode);
		void AddTorque(EntityHandle owner, const Vec3& torque, ForceMode3D mode);
		bool IsSleeping(EntityHandle owner) const;
		void WakeUp(EntityHandle owner);
		void Sleep(EntityHandle owner);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
	};

}
