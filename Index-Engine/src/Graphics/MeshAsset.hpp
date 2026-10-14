#pragma once
#include "Core/Export.hpp"
#include "Graphics/PrimitiveMesh.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Index {

	// Geometry of a mesh asset in its authored units, with its object-space bounds.
	struct INDEX_API MeshAsset {
		PrimitiveMeshData Data;
		glm::vec3 BoundsMin{ 0.0f };
		glm::vec3 BoundsMax{ 0.0f };
		// Unique per load, so GPU buffers and physics shapes built from Data can tell a reload apart.
		uint32_t Revision = 0;
	};

	inline constexpr const char* k_ObjMeshExtension = ".obj";

	// Wavefront OBJ: v/vt/vn/f, polygons fan-triangulated, negative indices, all objects and groups merged.
	// Missing normals are generated smooth per position, missing UVs are 0; V is flipped to the engine's top-left origin.
	INDEX_API bool ParseObjMesh(std::string_view text, PrimitiveMeshData& outMesh, std::string* outError = nullptr);

	// Smooth per-vertex normals from the triangles (area weighted); vertices on no triangle point up.
	INDEX_API void RecalculateNormals(PrimitiveMeshData& mesh);

	// Runtime mesh GUIDs live in their own range so they never collide with asset or built-in GUIDs.
	inline constexpr uint64_t k_RuntimeMeshGuidBase = 0xF1D0000000000000ULL;
	inline constexpr bool IsRuntimeMeshGuid(uint64_t guid) { return (guid & 0xFFFF000000000000ULL) == k_RuntimeMeshGuidBase; }

	// Shared mesh assets by GUID (main thread), loaded on first use.
	class INDEX_API MeshLibrary {
	public:
		MeshLibrary() = delete;

		// nullptr for 0, unknown GUIDs, unreadable files (retried at most once per second) and runtime meshes without triangles.
		static const MeshAsset* Get(uint64_t guid);

		static bool LoadFromFile(const std::string& path, PrimitiveMeshData& outMesh);

		static void Invalidate(uint64_t guid);
		// Re-reads cached meshes whose file changed outside the engine; returns how many were reloaded.
		static size_t ReloadChangedFromDisk();
		// Drops file-backed meshes; runtime meshes stay alive until destroyed.
		static void Clear();

		// In-memory mesh owned by code (e.g. a script); starts empty. Each SetRuntimeData bumps the revision so renderers re-upload.
		static uint64_t CreateRuntime();
		// False for GUIDs that are not live runtime meshes, or indices pointing past the vertex array.
		static bool SetRuntimeData(uint64_t guid, PrimitiveMeshData&& data);
		static bool DestroyRuntime(uint64_t guid);
		static void DestroyAllRuntime();
		static bool IsLiveRuntime(uint64_t guid);
		static size_t RuntimeCount();
	};

}
