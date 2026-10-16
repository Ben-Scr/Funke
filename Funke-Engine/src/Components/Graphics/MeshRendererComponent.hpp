#pragma once
#include "Core/Export.hpp"
#include "Collections/Color.hpp"
#include "Graphics/PrimitiveMesh.hpp"

#include <cstdint>

namespace Funke {

	// Draws a built-in primitive or a mesh asset with the entity's Transform3D and a .material asset.
	struct FUNKE_API MeshRendererComponent {
		PrimitiveMeshType Mesh = PrimitiveMeshType::Cube;
		// Mesh asset GUID (.obj); 0 = draw the primitive Mesh. An unloadable asset draws nothing.
		uint64_t CustomMesh = 0;
		// .material asset GUID; 0 = the default material (white, roughness 0.5, lit).
		uint64_t Material = 0;
		// Per-renderer tint multiplied into the material's base color.
		Color Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Without a material: false draws the flat color and ignores lights. A material's own Lit flag wins.
		bool Lit = true;
		bool CastShadows = true;
	};

}
