#pragma once

namespace Index {

	enum class AssetKind {
		Unknown = 0,
		Texture,
		Audio,
		Scene,
		Prefab,
		Script,
		Font,
		Shader,
		DataAsset,
		Other,
		// Appended after Other: C# AssetManager mirrors these values.
		Material,
		Mesh
	};

}
