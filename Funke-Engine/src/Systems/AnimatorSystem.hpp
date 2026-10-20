#pragma once
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"
#include "Scene/SceneSystem.hpp"

#include <cstdint>

namespace Funke {
	struct AnimatorComponent;
	struct GraphicsSettings;

	// Plays Animator components after the scripts (so parameters they set this frame take effect) and before 3D physics
	// picks up kinematic bodies. Culling Mode and Distance Rate let animators off screen or far from the game camera
	// skip frames; the skipped time is added to their next step.
	class FUNKE_API AnimatorSystem : public SceneSystem {
	public:
		void Update(Scene& scene) override;
		void FixedUpdate(Scene& scene) override;

		// Advances one animator by dt (before its Speed) and writes its pose (unless writePose is false: state machine
		// and events only). Exposed for tests and the editor preview.
		static void Step(Scene& scene, EntityHandle entity, AnimatorComponent& animator, float dt, bool fireEvents, bool writePose = true);
		// One frame of the scene's Normal and Unscaled Time animators (fixed: its Fixed ones) with culling and distance
		// rate; Update / FixedUpdate pass the frame's values. Returns how many wrote a pose. Exposed for tests.
		static uint32_t StepScene(Scene& scene, bool fixed, float dt, float unscaledDt, uint64_t frame, const GraphicsSettings& project);
	};
}
