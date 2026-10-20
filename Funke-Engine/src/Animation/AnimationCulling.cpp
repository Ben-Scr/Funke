#include "pch.hpp"
#include "Animation/AnimationCulling.hpp"

#include <algorithm>

namespace Funke {
	namespace AnimationCulling {
		uint32_t UpdateInterval(float distance, float halfRateDistance, float quarterRateDistance) {
			if (distance >= quarterRateDistance) return 4;
			if (distance >= halfRateDistance) return 2;
			return 1;
		}

		uint32_t PhaseOf(uint32_t entityId) {
			// Murmur3 finalizer: roots spawned every few ids (prefab hierarchies) still spread over all phases.
			uint32_t h = entityId;
			h ^= h >> 16;
			h *= 0x85ebca6bu;
			h ^= h >> 13;
			h *= 0xc2b2ae35u;
			h ^= h >> 16;
			return h;
		}

		bool IsDue(uint64_t frame, uint32_t phase, uint32_t interval) {
			return interval <= 1 || ((frame + phase) & (interval - 1)) == 0;
		}

		bool ResolveDistanceRate(const AnimationCullingSettings& settings, const GraphicsSettings& project,
			float& outHalfRateDistance, float& outQuarterRateDistance) {
			switch (settings.DistanceRate) {
			case AnimationDistanceRate::Off:
				return false;
			case AnimationDistanceRate::Custom:
				outHalfRateDistance = settings.HalfRateDistance;
				outQuarterRateDistance = settings.QuarterRateDistance;
				return true;
			case AnimationDistanceRate::ProjectSetting:
			default:
				if (!project.AnimationDistanceRateEnabled) return false;
				outHalfRateDistance = project.AnimationHalfRateDistance;
				outQuarterRateDistance = project.AnimationQuarterRateDistance;
				return true;
			}
		}

		bool NeedsCamera(const AnimationCullingSettings& settings, const GraphicsSettings& project) {
			float half = 0.0f;
			float quarter = 0.0f;
			return settings.CullingMode != AnimationCullingMode::AlwaysAnimate || ResolveDistanceRate(settings, project, half, quarter);
		}
	}

	AnimationStepClock::Step AnimationStepClock::Advance(float dt, AnimationCullingMode mode, bool visible, uint32_t interval,
		uint64_t frame, uint32_t phase) {
		const bool hidden = !visible && mode != AnimationCullingMode::AlwaysAnimate;
		const bool reappeared = Hidden && !hidden;
		Hidden = hidden;
		// Paused: the hidden time is not owed.
		if (hidden && mode == AnimationCullingMode::CullCompletely) return {};
		PendingTime += dt;
		if (hidden) interval = std::max(interval, AnimationCulling::k_HiddenInterval);
		if (Started && !reappeared && !AnimationCulling::IsDue(frame, phase, interval)) return {};
		Started = true;
		const Step step{ true, PendingTime, !hidden };
		PendingTime = 0.0f;
		return step;
	}

	AnimationPoseGate::Update AnimationPoseGate::Plan(AnimationCullingMode mode, bool visible, uint32_t interval, bool due, bool poseHeld) {
		const bool hidden = !visible && mode != AnimationCullingMode::AlwaysAnimate;
		const bool reappeared = Hidden && !hidden;
		Hidden = hidden;
		Update update;
		update.Advance = !(hidden && mode == AnimationCullingMode::CullCompletely);
		update.Live = !hidden && interval <= 1;
		update.Capture = !update.Live && (!poseHeld || reappeared || (!hidden && due));
		return update;
	}
}
