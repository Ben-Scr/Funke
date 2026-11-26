#pragma once
#include "Scene/EntityHandle.hpp"
#include <string>
#include <vector>

namespace Funke {
	// Optional display bindings on a widget. Authored labels/options/placeholders remain the source values.
	struct LocalizedUIComponent {
		EntityHandle LabelEntity = kNullEntity;
		std::string LabelKey;
		std::string PlaceholderKey;
		// Index-aligned with Dropdown.Options. Empty/missing keys leave that option literal.
		std::vector<std::string> OptionKeys;
	};
}
