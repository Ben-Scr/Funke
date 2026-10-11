#pragma once
#include "Scene/SceneSystem.hpp"

namespace Index {
	class ParticleUpdateSystem : public SceneSystem {
	public:
		virtual void Awake(Scene& scene);
		virtual void Update(Scene& scene);
	};
}
