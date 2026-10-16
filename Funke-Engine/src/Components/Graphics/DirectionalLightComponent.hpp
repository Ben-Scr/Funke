#pragma once
#include "Core/Export.hpp"
#include "Collections/Color.hpp"

namespace Funke {

	// Shines along the entity's Transform3D forward (-Z). The first enabled one is the scene's main light:
	// it drives the sun disc of the sky and is the only light that casts shadows.
	struct FUNKE_API DirectionalLightComponent {
		Color Color{ 1.0f, 0.96f, 0.9f, 1.0f };
		float Intensity = 1.0f;
		// Flat light added everywhere so faces turned away from the light are not black.
		Funke::Color Ambient{ 0.22f, 0.24f, 0.28f, 1.0f };
		bool CastShadows = true;
	};

}
