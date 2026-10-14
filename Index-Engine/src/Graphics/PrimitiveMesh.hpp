#pragma once
#include "Core/Export.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Index {

	// Serialized as its integer value; append only.
	enum class PrimitiveMeshType : uint8_t {
		Cube = 0,
		Sphere = 1,
		Plane = 2,
		Cylinder = 3
	};
	inline constexpr size_t k_PrimitiveMeshTypeCount = 4;

	struct MeshVertex {
		float Position[3];
		float Normal[3];
		float UV[2];
	};

	// CPU-side geometry, CCW front faces, unit sized and centred on the origin (Plane lies in XZ facing +Y).
	struct INDEX_API PrimitiveMeshData {
		std::vector<MeshVertex> Vertices;
		std::vector<uint32_t> Indices;
	};

	INDEX_API PrimitiveMeshData BuildPrimitiveMesh(PrimitiveMeshType type);
	INDEX_API std::string_view PrimitiveMeshTypeName(PrimitiveMeshType type);

}
