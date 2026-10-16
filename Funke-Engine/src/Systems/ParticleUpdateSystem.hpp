#pragma once
#include "Scene/SceneSystem.hpp"

namespace Funke {
	class ParticleUpdateSystem : public SceneSystem {
	public:
		virtual void Awake(Scene& scene);
		virtual void Update(Scene& scene);
	};
}
