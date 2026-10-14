#pragma once
#include "Core/Export.hpp"
#include "Collections/Color.hpp"

namespace Index {

	// Cone light along the entity's Transform3D forward (-Z); same falloff as PointLight plus a soft cone edge.
	struct INDEX_API SpotLightComponent {
		Color Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		float Intensity = 6.0f;
		float Range = 15.0f;          // world units
		float InnerAngle = 20.0f;     // half angle in degrees, full intensity inside
		float OuterAngle = 30.0f;     // half angle in degrees, zero outside
	};

}
