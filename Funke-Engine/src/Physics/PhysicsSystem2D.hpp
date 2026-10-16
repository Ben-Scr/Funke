#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entity/registry.hpp>

#include "Core/Export.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Physics/Collision2D.hpp"
#include "Physics/IPhysicsEngine.hpp"
#include "Scene/EntityHandle.hpp"

namespace Funke {
	class Scene;
	class Transform2DComponent;

	// A 2D physics backend added by a package, run around the built-in Box2D step.
	struct PhysicsExtension2D {
		using CollisionHandler = std::function<void(const Collision2D&)>;

		std::string Name;
		// entt::type_hash of the component whose presence means the backend drives the
		// entity's world transform (TransformHierarchySystem leaves it alone).
		entt::id_type BodyComponent = 0;
		// Pushes transform and shape edits into the backend and clears the Transform2D
		// dirty flags it consumes. Runs in edit mode too.
		void (*SyncToPhysics)(Scene& scene) = nullptr;
		void (*Step)(float dt) = nullptr;
		// Writes simulated positions back to Transform2D (see DecomposeWorldToLocal).
		void (*SyncFromPhysics)(Scene& scene) = nullptr;
		void (*DispatchCollisions)(const CollisionHandler& onEnter, const CollisionHandler& onStay,
			const CollisionHandler& onExit) = nullptr;
		// Physics overlay (F8): draw the backend's colliders with Gizmo (layer preset) using
		// lines of the given world-space width.
		void (*DrawDebug)(Scene& scene, float lineWidth) = nullptr;
		void (*Shutdown)() = nullptr;
	};

	class FUNKE_API PhysicsSystem2D : public IPhysicsEngine {
	public:
		~PhysicsSystem2D() override = default;

		void FixedUpdate(float dt) override;
		// Editor-mode + play-mode sync. No simulation step.
		void Update() override;

		// Reset Box2D's sleep timer on every Rigidbody2D body across all
		// loaded scenes. Call when entering play mode so bodies that sat
		// in editor mode aren't immediately considered idle.
		static void WakeAllBodies();

		void Initialize() override;
		void Shutdown() override;

		static Box2DWorld& GetMainPhysicsWorld() { return s_MainWorld.value(); }
		static bool IsInitialized() { return s_MainWorld.has_value(); }
		static bool IsEnabled() { return s_IsEnabled; };
		static void SetEnabled(bool enabled) { s_IsEnabled = enabled; }

		// Replaces an extension with the same Name.
		static void RegisterExtension(PhysicsExtension2D extension);
		static void UnregisterExtension(std::string_view name);
		static const std::vector<PhysicsExtension2D>& GetExtensions();

		// True when a Rigidbody2D or a registered extension drives the entity's world transform.
		static bool IsTransformOwnedByPhysics(const entt::registry& registry, EntityHandle entity);
		// Inspector edits only touch Local*; compose the world transform before a body reads it.
		static void ComposeWorldFromLocal(entt::registry& registry, EntityHandle entity, Transform2DComponent& tf);
		// After a step, rebuild Local* from the physics-driven world transform.
		static void DecomposeWorldToLocal(entt::registry& registry, EntityHandle entity, Transform2DComponent& tf);

	private:
		void SyncTransformsToPhysics();

		static std::optional<Box2DWorld> s_MainWorld;
		static bool s_IsEnabled;
	};
}
