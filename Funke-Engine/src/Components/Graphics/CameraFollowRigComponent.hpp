#pragma once
#include "Collections/Vec2.hpp"
#include "Scene/EntityHandle.hpp"

namespace Funke {
	struct CameraFollowRigComponent {
		EntityHandle Target = entt::null;
		Vec3 Offset{0.0f, 2.0f, 5.0f};
		float Damping = 8.0f;
		bool Orbit = false;
		float Yaw = 0.0f;
		float Pitch = 15.0f;
		float Distance = 5.0f;
		bool LookAtTarget = true;
		// Runtime only. Changing or losing the target starts a fresh follow on reacquisition.
		EntityHandle LastTarget = entt::null;
		bool Initialized = false;
		Vec3 FollowPosition{0.0f};
	};
}
