#pragma once
#include "Core/Export.hpp"
#include "Physics/PhysicsTypes.hpp"

namespace Funke {

	// Plain data: PhysicsSystem3D owns the Jolt body and rebuilds it when a field changes.
	// Colliders on this entity and on descendants without their own Rigidbody3D form its shape.
	struct FUNKE_API Rigidbody3DComponent {
		BodyType Type = BodyType::Dynamic;
		float Mass = 1.0f;
		float LinearDrag = 0.05f;
		float AngularDrag = 0.05f;
		float GravityScale = 1.0f;
		// Swept collision so fast bodies don't tunnel through thin geometry.
		bool ContinuousCollision = false;
		// Renders a dynamic body between its last two physics steps, so it moves smoothly at frame
		// rates above the physics tick rate (one step of visual latency).
		bool Interpolate = true;
		bool FreezePositionX = false;
		bool FreezePositionY = false;
		bool FreezePositionZ = false;
		bool FreezeRotationX = false;
		bool FreezeRotationY = false;
		bool FreezeRotationZ = false;
	};

}
