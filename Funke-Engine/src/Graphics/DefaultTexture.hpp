#pragma once

#include "Core/Export.hpp"

#include <cstdint>

namespace Funke {

	enum class FUNKE_API DefaultTexture : uint8_t {
		Square,
		Pixel,
		Circle,
		Capsule,
		IsometricDiamond,
		HexagonFlatTop,
		HexagonPointedTop,
		_9Sliced,
		Invisible
	};

} // namespace Funke
