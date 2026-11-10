#pragma once

#include <cstdint>

namespace Funke {

	// Unset focus sprites inherit hover before normal; other unset sprite states inherit normal.
	enum class UITransitionMode : std::uint8_t {
		ColorSwap  = 0,
		SpriteSwap = 1,
		None       = 2,
	};

}
