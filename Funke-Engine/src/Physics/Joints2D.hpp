#pragma once
#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"

#include <box2d/id.h>
#include <entt/entity/registry.hpp>

namespace Funke {
	class Scene;
	class Box2DWorld;
}

namespace Funke::Joints2D {
	// The Box2D body that represents the entity: its Rigidbody2D's, else the static body of one of its colliders.
	FUNKE_API b2BodyId ResolveBody(const entt::registry& registry, EntityHandle entity);

	// Builds the joints whose bodies exist and rebuilds those whose bodies or structural fields changed.
	void Sync(Scene& scene, Box2DWorld& world);

	// Removes every live joint of the scene (leaving play mode keeps no joints in edit mode).
	void DestroyAll(Scene& scene);
}
