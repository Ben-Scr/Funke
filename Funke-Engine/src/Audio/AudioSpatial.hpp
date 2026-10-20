#pragma once
#include "Core/Export.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace Funke {
	// Serialized as its integer value; append only.
	enum class AudioRolloff : uint8_t {
		// Gain MinDistance / distance (inverse distance), the natural falloff; never quite silent.
		Logarithmic = 0,
		// Gain falls in a straight line from 1 at MinDistance to 0 at MaxDistance.
		Linear = 1
	};

	// How a spatial sound fades with distance and pans with direction.
	struct AudioSpatialSettings {
		float MinDistance = 1.0f;
		float MaxDistance = 50.0f;
		AudioRolloff Rolloff = AudioRolloff::Logarithmic;
		// 0 = no left/right panning; 1 = a sound fully to one side is silent on the other. 0.8 keeps 20% there.
		float Panning = 0.8f;
		// Pitch shift from the relative velocity of source and listener; 0 = off, 1 = physical.
		float DopplerLevel = 0.0f;

		bool operator==(const AudioSpatialSettings&) const = default;
	};

	// Where spatial sounds are heard from: the main camera, updated every frame by AudioUpdateSystem.
	struct AudioListenerState {
		glm::vec3 Position{ 0.0f };
		glm::vec3 Forward{ 0.0f, 0.0f, -1.0f };
		glm::vec3 Up{ 0.0f, 1.0f, 0.0f };
		glm::vec3 Velocity{ 0.0f };
	};

	namespace AudioSpatial {
		struct Result {
			float Distance = 0.0f;
			// Distance attenuation, 0..1.
			float Gain = 1.0f;
			// -1 = left .. +1 = right, already scaled by Panning.
			float Pan = 0.0f;
			// Channel gains the listener hears (attenuation times the balance pan), before the sound's volume.
			float Left = 1.0f;
			float Right = 1.0f;
		};

		// Same falloff as miniaudio's spatializer (inverse / linear, rolloff 1) between Min and Max Distance.
		FUNKE_API float Attenuation(float distance, const AudioSpatialSettings& settings);
		FUNKE_API float Pan(const AudioListenerState& listener, const glm::vec3& sourcePosition, float panning);
		FUNKE_API Result Evaluate(const AudioListenerState& listener, const glm::vec3& sourcePosition, const AudioSpatialSettings& settings);
		// Values out of range (negative distances, Max below Min, NaN) replaced by usable ones.
		FUNKE_API AudioSpatialSettings Sanitize(AudioSpatialSettings settings);
	}
}
