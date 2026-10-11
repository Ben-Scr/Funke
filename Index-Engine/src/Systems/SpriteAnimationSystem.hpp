#pragma once
#include "Core/Export.hpp"
#include "Scene/SceneSystem.hpp"

namespace Index {
	struct SpriteAnimator2DComponent;
	struct SpriteRendererComponent;

	class INDEX_API SpriteAnimationSystem : public SceneSystem {
	public:
		virtual void Update(Scene& scene);

		// Advances one animator by dt and writes the current frame to the sprite. Exposed for tests.
		static void Step(SpriteAnimator2DComponent& animator, SpriteRendererComponent& sprite, float dt);
	};
}
