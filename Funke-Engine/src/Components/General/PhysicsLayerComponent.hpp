#pragma once
#include <cstdint>

namespace Funke {
	// Optional entity-wide membership. Absent on legacy entities, preserving their per-collider layers.
	struct PhysicsLayerComponent {
		uint64_t Mask = 1;
	};
}
