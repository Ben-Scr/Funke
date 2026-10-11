#include "pch.hpp"
#include "Assets/AssetRegistry.hpp"

namespace Index {

	AssetRegistry::StateData& AssetRegistry::State() {
		static StateData s_State;
		return s_State;
	}

}
