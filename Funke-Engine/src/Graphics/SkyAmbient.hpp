#pragma once
#include "Core/Export.hpp"
#include "Collections/Color.hpp"
#include "Graphics/ImageData.hpp"

#include <cstdint>
#include <optional>

namespace Funke::SkyAmbient {

	// Hemispheric ambient colors, sRGB like Camera3DComponent's sky colors.
	struct Colors {
		Color Top;
		Color Horizon;
		Color Ground;
	};

	// Averages an equirectangular RGBA8 panorama (row 0 = zenith) into zenith / horizon / nadir bands,
	// weighted by solid angle and averaged in linear space.
	FUNKE_API Colors FromEquirect(const ImageData& image);

	// Colors for the sky ambient of a Skybox material: a gradient shader's top/horizon/ground, a panorama
	// (`panorama` texture × tint × exposure) or six faces (`up`/`down`/sides × their colors × exposure).
	// nullopt when the material or its shader has none of these. Texture averages are computed once per texture.
	FUNKE_API std::optional<Colors> FromMaterial(uint64_t materialGuid);

	// Drops cached texture averages, e.g. after a texture changed on disk.
	FUNKE_API void ClearCache();

}
