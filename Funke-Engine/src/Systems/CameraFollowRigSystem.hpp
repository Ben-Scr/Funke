#pragma once
#include "Scene/SceneSystem.hpp"

namespace Funke {
	class FUNKE_API CameraFollowRigSystem : public SceneSystem {
	public:
		void OnPreRender(Scene& scene) override;
		// Caller supplies post-movement/interpolation world matrices and the render delta.
		static void Step(Scene& scene, float deltaTime);
	};
}
