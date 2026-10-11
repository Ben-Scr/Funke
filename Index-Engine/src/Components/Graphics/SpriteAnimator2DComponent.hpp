#pragma once
#include "Core/Export.hpp"

#include <string>
#include <vector>

namespace Index {
	// Flip-book animation over sprite-sheet slices of the entity's SpriteRenderer texture.
	// Each frame names a slice (see the Sprite Editor); the system writes it to SpriteRenderer::SpriteName.
	struct INDEX_API SpriteAnimator2DComponent {
		std::vector<std::string> Frames;
		float FramesPerSecond = 12.0f;
		bool Loop = true;
		bool PlayOnStart = true;

		// Runtime state; not serialized, so every Play session starts from frame 0.
		bool Playing = false;
		bool Started = false;
		int CurrentFrame = 0;
		float FrameTime = 0.0f;

		void Play() { Playing = true; Started = true; }
		void Stop() { Playing = false; Started = true; CurrentFrame = 0; FrameTime = 0.0f; }
		void Pause() { Playing = false; Started = true; }
	};
}
