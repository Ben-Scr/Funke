#pragma once
#include "Core/Export.hpp"
#include "Collections/Color.hpp"

namespace Index {

	// Omnidirectional light at the entity's Transform3D position; inverse-square falloff that reaches zero at Range.
	struct INDEX_API PointLightComponent {
		Color Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		float Intensity = 4.0f;
		float Range = 10.0f;    // world units
	};

}
