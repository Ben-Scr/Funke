#pragma once
#include <string>
#include <vector>

namespace Funke {

	// The order the Inspector shows this entity's components in, as arranged by hand. Keys are
	// serialized component names, "script:<Class>" for C# scripts and "managed:<Class>" for C#
	// components (see Scene/ComponentOrder.hpp). Components it doesn't list follow in the default
	// order. Only the editor reads it.
	struct ComponentOrderComponent {
		std::vector<std::string> Order;
	};

}
