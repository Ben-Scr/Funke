#include "pch.hpp"
#include "Audio/AudioSpatial.hpp"

#include <algorithm>
#include <cmath>

namespace Funke::AudioSpatial {
	float Attenuation(float distance, const AudioSpatialSettings& settings) {
		const float minDistance = settings.MinDistance;
		const float maxDistance = settings.MaxDistance;
		if (minDistance >= maxDistance || minDistance <= 0.0f) return 1.0f;
		const float clamped = std::clamp(distance, minDistance, maxDistance);
		if (settings.Rolloff == AudioRolloff::Linear) return 1.0f - (clamped - minDistance) / (maxDistance - minDistance);
		// miniaudio's inverse model with rolloff 1: min / (min + (d - min)).
		return minDistance / clamped;
	}

	float Pan(const AudioListenerState& listener, const glm::vec3& sourcePosition, float panning) {
		const glm::vec3 offset = sourcePosition - listener.Position;
		const float distance = glm::length(offset);
		if (!(distance > 0.001f)) return 0.0f;
		const glm::vec3 right = glm::cross(listener.Forward, listener.Up);
		const float rightLength = glm::length(right);
		if (!(rightLength > 1e-6f)) return 0.0f;
		const float side = glm::dot(offset / distance, right / rightLength);
		return std::clamp(side * std::clamp(panning, 0.0f, 1.0f), -1.0f, 1.0f);
	}

	Result Evaluate(const AudioListenerState& listener, const glm::vec3& sourcePosition, const AudioSpatialSettings& settings) {
		Result result;
		result.Distance = glm::length(sourcePosition - listener.Position);
		result.Gain = Attenuation(result.Distance, settings);
		result.Pan = Pan(listener, sourcePosition, settings.Panning);
		// miniaudio's balance pan scales only the far channel.
		result.Left = result.Gain * (result.Pan > 0.0f ? 1.0f - result.Pan : 1.0f);
		result.Right = result.Gain * (result.Pan < 0.0f ? 1.0f + result.Pan : 1.0f);
		return result;
	}

	AudioSpatialSettings Sanitize(AudioSpatialSettings settings) {
		const AudioSpatialSettings defaults{};
		if (!std::isfinite(settings.MinDistance)) settings.MinDistance = defaults.MinDistance;
		if (!std::isfinite(settings.MaxDistance)) settings.MaxDistance = defaults.MaxDistance;
		settings.MinDistance = std::max(settings.MinDistance, 0.01f);
		settings.MaxDistance = std::max(settings.MaxDistance, settings.MinDistance);
		settings.Panning = std::isfinite(settings.Panning) ? std::clamp(settings.Panning, 0.0f, 1.0f) : defaults.Panning;
		settings.DopplerLevel = std::isfinite(settings.DopplerLevel) ? std::clamp(settings.DopplerLevel, 0.0f, 10.0f) : 0.0f;
		if (static_cast<uint8_t>(settings.Rolloff) > static_cast<uint8_t>(AudioRolloff::Linear)) settings.Rolloff = defaults.Rolloff;
		return settings;
	}
}
