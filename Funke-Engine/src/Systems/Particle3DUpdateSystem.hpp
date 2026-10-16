#pragma once
#include "Scene/SceneSystem.hpp"

namespace Funke {
	// Ticks every ParticleSystem3DComponent with its Transform3D world matrix (identity without one).
	class Particle3DUpdateSystem : public SceneSystem {
	public:
		void Awake(Scene& scene) override;
		void Update(Scene& scene) override;
	};
}
