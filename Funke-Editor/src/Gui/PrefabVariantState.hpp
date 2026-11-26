#pragma once
#include "Serialization/Json.hpp"
#include <cstdint>

namespace Funke::PrefabAuthoringDetail {
	struct VariantEditState {
		uint64_t BaseGuid = 0;
		Json::Value BaseSource;
		Json::Value EditableBase;
		Json::Value PreviousSource;
	};
}
