#pragma once

#include <cstdint>

namespace Funke {

	inline constexpr uint64_t k_RuntimeMeshGuidBase = 0xF1D0000000000000ULL;
	inline constexpr bool IsRuntimeMeshGuid(uint64_t guid) { return (guid & 0xFFFF000000000000ULL) == k_RuntimeMeshGuidBase; }

	template<typename Generate, typename IsUsed>
	uint64_t GenerateAssetGuid(Generate&& generate, IsUsed&& isUsed) {
		uint64_t guid;
		do {
			guid = generate();
		} while (guid == 0 || IsRuntimeMeshGuid(guid) || isUsed(guid));
		return guid;
	}

}
