#pragma once
#include "Core/Export.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace Funke {
	struct MeshAsset;

	inline constexpr const char* k_GltfMeshExtension = ".gltf";
	inline constexpr const char* k_GlbMeshExtension = ".glb";

	// Loads a file a .gltf references by (already percent-decoded) relative uri.
	using GltfExternalReader = std::function<bool(const std::string& uri, std::vector<uint8_t>& outBytes)>;

	// glTF 2.0, text (.gltf, buffers external or base64 data uris) or binary (.glb). Every triangle primitive of every
	// mesh node is merged into one mesh. Skins, node hierarchy and TRS animations land in outAsset.Skin (also for
	// rigid node animation); outAsset.Data holds the rest pose. Materials, textures and morph targets are ignored.
	FUNKE_API bool ParseGltf(std::span<const uint8_t> bytes, const GltfExternalReader& readExternal, MeshAsset& outAsset,
		std::string* outError = nullptr);

	FUNKE_API bool DecodeBase64(std::string_view text, std::vector<uint8_t>& outBytes);
}
