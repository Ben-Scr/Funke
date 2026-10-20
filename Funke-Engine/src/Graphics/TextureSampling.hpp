#pragma once

#include "Core/GraphicsSettings.hpp"
#include "Graphics/Filter.hpp"

#include <cstdint>

namespace Funke {

	// What the texture graphics settings add to a texture's own filter.
	struct TextureSamplingQuality {
		float LodMinClamp = 0.0f;
		uint16_t MaxAnisotropy = 1;

		bool operator==(const TextureSamplingQuality&) const = default;
	};

	// Point textures (pixel art) are never touched. Anisotropy needs linear mip filtering, so Bilinear only gets the mip limit.
	constexpr TextureSamplingQuality ResolveTextureSampling(Filter filter, TextureQualityLevel quality,
		AnisotropicFilteringLevel anisotropy) noexcept
	{
		TextureSamplingQuality result;
		switch (filter) {
			case Filter::Bilinear:
				result.LodMinClamp = static_cast<float>(GetTextureMipSkip(quality));
				break;
			case Filter::Trilinear:
			case Filter::Anisotropic:
				result.LodMinClamp = static_cast<float>(GetTextureMipSkip(quality));
				result.MaxAnisotropy = GetMaxAnisotropy(anisotropy);
				break;
			case Filter::Point:
			case Filter::Default:
				break;
		}
		return result;
	}

} // namespace Funke
