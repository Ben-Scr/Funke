#include "pch.hpp"
#include "Physics3D/PhysicsSystem3D.hpp"

#include "Components/Physics3D/Collider3DComponents.hpp"
#include "Components/Physics3D/Rigidbody3DComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Systems/Transform3DHierarchySystem.hpp"

namespace Index {

	bool PhysicsSystem3D::HasPhysicsComponents(Scene& scene) {
		const entt::registry& registry = scene.GetRegistry();
		return !registry.view<Rigidbody3DComponent>().empty()
			|| !registry.view<BoxCollider3DComponent>().empty()
			|| !registry.view<SphereCollider3DComponent>().empty()
			|| !registry.view<CapsuleCollider3DComponent>().empty()
			|| !registry.view<MeshCollider3DComponent>().empty();
	}

	PhysicsWorld3D* PhysicsSystem3D::GetWorld(Scene& scene, bool create) {
		if (!Application::GetIsPlaying()) return nullptr;
		if (!m_World && create && HasPhysicsComponents(scene)) {
			m_World = std::make_unique<PhysicsWorld3D>();
		}
		return m_World.get();
	}

	PhysicsWorld3D* PhysicsSystem3D::FindWorld(Scene& scene, bool create) {
		PhysicsSystem3D* system = scene.GetSystem<PhysicsSystem3D>();
		return system ? system->GetWorld(scene, create) : nullptr;
	}

	void PhysicsSystem3D::FixedUpdate(Scene& scene) {
		INDEX_PROFILE_SCOPE("Physics3D");
		PhysicsWorld3D* world = GetWorld(scene, true);
		if (!world) return;

		// Scripts may have moved entities in their fixed update; bodies follow the fresh matrices.
		Transform3DHierarchySystem::Propagate(scene);
		world->Simulate(scene, Time::GetUnscaledFixedDeltaTime());

		// A callback may unload this scene (LoadScene Single), destroying this system and its world:
		// keep the scene alive for the dispatch and stop once it is unloaded.
		std::shared_ptr<Scene> keepAlive;
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			if (auto loaded = weakScene.lock(); loaded.get() == &scene) {
				keepAlive = std::move(loaded);
				break;
			}
		}
		world->DispatchContacts([&scene](const Collision3D& collision, CollisionPhase3D phase) {
			if (scene.IsLoaded()) ScriptSystem::DispatchCollision3D(collision, phase);
		});
	}

	void PhysicsSystem3D::OnPreRender(Scene&) {
		// Stopping play restores the edit scene; its bodies and velocities must not leak into the next run.
		if (m_World && !Application::GetIsPlaying()) {
			m_World.reset();
		}
	}

	void PhysicsInterpolationSystem3D::Update(Scene& scene) {
		if (PhysicsWorld3D* world = PhysicsSystem3D::FindWorld(scene, false)) {
			world->Present(scene, Application::GetFixedStepAlpha());
		}
	}

	void PhysicsSystem3D::OnDestroy(Scene&) {
		m_World.reset();
	}

}
