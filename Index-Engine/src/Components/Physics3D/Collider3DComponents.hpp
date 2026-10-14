#pragma once
#include "Core/Export.hpp"
#include "Collections/Vec2.hpp"
#include "Graphics/PrimitiveMesh.hpp"

#include <cstdint>

namespace Index {

	// Shared by every 3D collider. Sizes are local: the entity's world scale is applied on top.
	struct INDEX_API Collider3DBase {
		// Shape offset in the entity's local space.
		Vec3 Center{ 0.0f, 0.0f, 0.0f };
		// Reports OnTrigger*3D overlaps instead of colliding.
		bool IsTrigger = false;
		float Friction = 0.5f;
		float Bounciness = 0.0f;
	};

	struct INDEX_API BoxCollider3DComponent : Collider3DBase {
		Vec3 Size{ 1.0f, 1.0f, 1.0f };
	};

	struct INDEX_API SphereCollider3DComponent : Collider3DBase {
		float Radius = 0.5f;
	};

	// Serialized as its integer value; append only.
	enum class CapsuleAxis : uint8_t { X = 0, Y = 1, Z = 2 };

	struct INDEX_API CapsuleCollider3DComponent : Collider3DBase {
		float Radius = 0.5f;
		// End to end, including both hemispheres.
		float Height = 2.0f;
		CapsuleAxis Direction = CapsuleAxis::Y;
	};

	struct INDEX_API MeshCollider3DComponent : Collider3DBase {
		// Takes the mesh from the entity's MeshRenderer when present, else CustomMesh or Mesh.
		bool UseMeshRenderer = true;
		PrimitiveMeshType Mesh = PrimitiveMeshType::Cube;
		// Mesh asset GUID (.obj); 0 = the primitive Mesh.
		uint64_t CustomMesh = 0;
		// Convex hull of the vertices. A non-convex (triangle) mesh only collides as a static or
		// kinematic body; on a dynamic Rigidbody3D it falls back to its convex hull.
		bool Convex = false;
	};

}
