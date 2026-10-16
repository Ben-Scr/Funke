#pragma once
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"
#include "Scene/SceneSystem.hpp"

namespace Funke {
	struct AnimatorComponent;

	// Plays Animator components after the scripts (so parameters they set this frame take effect) and before 3D physics
	// picks up kinematic bodies.
	class FUNKE_API AnimatorSystem : public SceneSystem {
	public:
		void Update(Scene& scene) override;
		void FixedUpdate(Scene& scene) override;

		// Advances one animator by dt (before its Speed) and writes its pose. Exposed for tests and the editor preview.
		static void Step(Scene& scene, EntityHandle entity, AnimatorComponent& animator, float dt, bool fireEvents);
	};
}
