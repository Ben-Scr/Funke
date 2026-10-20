#pragma once
#include "Core/Export.hpp"
#include "Graphics/SkeletalAnimation.hpp"
#include "Scene/SceneSystem.hpp"

#include <string>

namespace Funke {
	struct SkinnedMeshRendererComponent;

	// Advances every SkinnedMeshRenderer's clip in play mode; in edit mode (OnPreRender) only with PreviewInEditMode.
	// Culling Mode and Distance Rate hold the drawn pose back (no re-skinning) off screen or far from the game camera,
	// while the play head keeps its exact time.
	class FUNKE_API SkinnedMeshAnimationSystem : public SceneSystem {
	public:
		void Update(Scene& scene) override;
		void OnPreRender(Scene& scene) override;

		// The clip index the renderer's Clip name selects; empty = the first clip, -1 = none or unknown.
		static int ResolveClip(const SkinnedMeshRendererComponent& renderer, const SkinnedMeshData& data);
		// Advances playback and the cross-fade by dt (scaled by Speed). Exposed for tests.
		static void Step(SkinnedMeshRendererComponent& renderer, const SkinnedMeshData* data, float dt);
		// Switches to clip, fading from the current pose over crossFade seconds; replaying the running clip only restarts
		// it when it has finished. data may be null (model not loaded yet): the switch then happens without a fade.
		static void Play(SkinnedMeshRendererComponent& renderer, const SkinnedMeshData* data, const std::string& clip, float crossFade);
		// The play head's pose.
		static SkeletalPoseRequest MakePoseRequest(const SkinnedMeshRendererComponent& renderer, const SkinnedMeshData& data);
		// The pose that is drawn (skinning, picking, bones for scripts): the held one while it is held back.
		static SkeletalPoseRequest GetDrawnPose(const SkinnedMeshRendererComponent& renderer, const SkinnedMeshData& data);
		// Draws the play head's current pose until the next hold or release.
		static void HoldPose(SkinnedMeshRendererComponent& renderer, const SkinnedMeshData& data);
	};
}
