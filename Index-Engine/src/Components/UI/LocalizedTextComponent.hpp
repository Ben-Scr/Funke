#pragma once
#include "Core/Export.hpp"

#include <cstdint>
#include <string>

namespace Index {

	// Fills the entity's TextRenderer with the translation of Key (Assets/Localization/<language>.json) during
	// play and again on every language switch. Outside play the TextRenderer keeps its authored text.
	struct INDEX_API LocalizedTextComponent {
		std::string Key;

		// Runtime only: what was written last, so text a script sets survives until the key or the language changes.
		std::string AppliedKey;
		std::uint32_t AppliedRevision = 0;
	};

}
