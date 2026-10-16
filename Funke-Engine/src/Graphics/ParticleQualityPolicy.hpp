#pragma once

#include "Core/GraphicsSettings.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Funke::ParticleQualityPolicy {

	inline float Density(ParticleQualityLevel quality) {
		switch (quality) {
			case ParticleQualityLevel::Off: return 0.0f;
			case ParticleQualityLevel::Low: return 0.25f;
			case ParticleQualityLevel::Medium: return 0.5f;
			case ParticleQualityLevel::High: return 1.0f;
		}
		return 1.0f;
	}

	inline uint32_t EffectiveLimit(uint32_t authoredLimit, ParticleQualityLevel quality) {
		if (authoredLimit == 0 || quality == ParticleQualityLevel::Off) return 0;
		return std::max(1u, static_cast<uint32_t>(std::floor(authoredLimit * Density(quality))));
	}

	inline size_t ScaleCount(size_t authoredCount, ParticleQualityLevel quality) {
		if (authoredCount == 0 || quality == ParticleQualityLevel::Off) return 0;
		return std::max<size_t>(1, static_cast<size_t>(std::floor(authoredCount * Density(quality))));
	}

} // namespace Funke::ParticleQualityPolicy
