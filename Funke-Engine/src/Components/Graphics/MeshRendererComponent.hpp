#pragma once
#include "Core/Export.hpp"
#include "Collections/Color.hpp"
#include "Graphics/ModelMaterialOverrides.hpp"
#include "Graphics/PrimitiveMesh.hpp"

#include <cstdint>

namespace Funke {

	// Draws a built-in primitive or a mesh asset with the entity's Transform3D and a .material asset.
	struct FUNKE_API MeshRendererComponent {
		PrimitiveMeshType Mesh = PrimitiveMeshType::Cube;
		// Mesh asset GUID (.obj/.gltf/.glb); 0 = draw the primitive Mesh. An unloadable asset draws nothing.
		uint64_t CustomMesh = 0;
		// Whole-model .material override; 0 uses slot overrides/imported materials, or the primitive default.
		uint64_t Material = 0;
		ModelMaterialOverrides MaterialOverrides;
		// Per-renderer tint multiplied into the material's base color.
		Funke::Color Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Without a material: false draws the flat color and ignores lights. A material's own Lit flag wins.
		bool Lit = true;
		bool CastShadows = true;
	};

}
