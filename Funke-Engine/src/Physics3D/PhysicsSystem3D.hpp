#pragma once
#include "Core/Export.hpp"
#include "Physics3D/PhysicsWorld3D.hpp"
#include "Scene/SceneSystem.hpp"

#include <memory>

namespace Funke {

	// Runs the scene's Jolt world in play mode. The world is created on the first fixed step that
	// finds a 3D physics component and dropped again outside play mode.
	class FUNKE_API PhysicsSystem3D : public SceneSystem {
	public:
		void FixedUpdate(Scene& scene) override;
		void OnPreRender(Scene& scene) override;
		void OnDestroy(Scene& scene) override;

		// Null outside play mode, or before the scene had any 3D physics component when create is false.
		PhysicsWorld3D* GetWorld(Scene& scene, bool create);

		// The scene's world via its PhysicsSystem3D; null when the scene has none.
		static PhysicsWorld3D* FindWorld(Scene& scene, bool create = true);

		static bool HasPhysicsComponents(Scene& scene);

	private:
		std::unique_ptr<PhysicsWorld3D> m_World;
	};

	// Blends interpolating bodies between their last two physics steps. Registered before the script
	// system so scripts (a follow camera) read the same smooth pose that gets rendered.
	class FUNKE_API PhysicsInterpolationSystem3D : public SceneSystem {
	public:
		void Update(Scene& scene) override;
	};

}
