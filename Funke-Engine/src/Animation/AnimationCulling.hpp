#pragma once
#include "Core/Export.hpp"
#include "Core/GraphicsSettings.hpp"

#include <cstdint>

namespace Funke {
	// Serialized as its integer value; append only.
	enum class AnimationCullingMode : uint8_t {
		AlwaysAnimate = 0,
		// Off screen the clock, transitions and events keep running, but no pose is written or skinned.
		CullPose = 1,
		// Off screen the animation pauses; back in view it continues where it stopped.
		CullCompletely = 2
	};

	// Serialized as its integer value; append only.
	enum class AnimationDistanceRate : uint8_t {
		// Project Settings > Graphics > Animation decides.
		ProjectSetting = 0,
		Off = 1,
		// The component's own Half / Quarter Rate Distance.
		Custom = 2
	};

	// The culling fields Animator and Skinned Mesh Renderer share.
	struct AnimationCullingSettings {
		AnimationCullingMode CullingMode = AnimationCullingMode::AlwaysAnimate;
		AnimationDistanceRate DistanceRate = AnimationDistanceRate::ProjectSetting;
		float HalfRateDistance = 20.0f;
		float QuarterRateDistance = 40.0f;
	};

	namespace AnimationCulling {
		// Hidden animations that keep their clock (Cull Pose) step at most this often.
		inline constexpr uint32_t k_HiddenInterval = 4;

		// Frames between two updates at this distance from the camera: 4 from quarterRateDistance on, 2 from
		// halfRateDistance on, otherwise 1.
		FUNKE_API uint32_t UpdateInterval(float distance, float halfRateDistance, float quarterRateDistance);
		// Stable frame offset per entity, so animations with the same interval update on different frames.
		FUNKE_API uint32_t PhaseOf(uint32_t entityId);
		// interval must be a power of two.
		FUNKE_API bool IsDue(uint64_t frame, uint32_t phase, uint32_t interval);

		// The distances settings.DistanceRate selects; false when it is off.
		FUNKE_API bool ResolveDistanceRate(const AnimationCullingSettings& settings, const GraphicsSettings& project,
			float& outHalfRateDistance, float& outQuarterRateDistance);
		// Nothing to measure: always animated at full rate, so the camera is never looked at.
		FUNKE_API bool NeedsCamera(const AnimationCullingSettings& settings, const GraphicsSettings& project);
	}

	// Frames an Animator skips: the time they owe it, so the next step keeps tempo, transitions and events.
	struct FUNKE_API AnimationStepClock {
		struct Step {
			bool Run = false;
			// This frame's dt plus every frame skipped since the last step.
			float DeltaTime = 0.0f;
			// False while hidden under Cull Pose: only the clock, transitions and events advance.
			bool WritePose = false;
		};

		float PendingTime = 0.0f;
		bool Hidden = false;
		bool Started = false;

		// visible only matters when mode culls. The first frame and the frame it comes back into view always step.
		Step Advance(float dt, AnimationCullingMode mode, bool visible, uint32_t interval, uint64_t frame, uint32_t phase);
	};

	// Renderers that skin on the CPU advance their cheap play head every frame and only hold back the pose they draw.
	struct FUNKE_API AnimationPoseGate {
		struct Update {
			// False while hidden under Cull Completely: the play head pauses.
			bool Advance = true;
			// Draw the play head as it is (near, visible).
			bool Live = true;
			// Hold the play head's pose from now on until the next capture.
			bool Capture = false;
		};

		bool Hidden = false;

		Update Plan(AnimationCullingMode mode, bool visible, uint32_t interval, bool due, bool poseHeld);
	};
}
