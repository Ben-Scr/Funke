#pragma once
#include "Graphics/Filter.hpp"
#include "Graphics/Wrap.hpp"

#include <cstdint>
#include <cstddef>
#include <functional>

namespace Funke {

	struct TextureLoadKey {
		uint64_t AssetId = 0;
		Filter FilterMode = Filter::Point;
		Wrap WrapU = Wrap::Clamp;
		Wrap WrapV = Wrap::Clamp;
		bool operator==(const TextureLoadKey&) const = default;
	};

	struct TextureLoadKeyHash {
		size_t operator()(const TextureLoadKey& key) const noexcept {
			const uint64_t sampler = static_cast<uint64_t>(key.FilterMode)
				| (static_cast<uint64_t>(key.WrapU) << 8)
				| (static_cast<uint64_t>(key.WrapV) << 40);
			return std::hash<uint64_t>{}(key.AssetId) ^ std::hash<uint64_t>{}(sampler);
		}
	};

}
