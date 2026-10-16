#pragma once

#include <cstdint>

namespace Index::MaterialShaderInspector {

	// Shader picker of a .material and, with a user shader, its @property fields and current errors. Edits are written
	// to the file at once. Returns true when the material uses a user shader (the standard PBR fields don't apply).
	bool Draw(uint64_t materialGuid);

}
