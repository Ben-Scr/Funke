#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>

namespace Funke {
	// Stable indices: renaming a layer never changes scene/prefab masks.
	struct PhysicsLayerSettings {
		static constexpr uint32_t Count = 64;
		std::array<std::string, Count> Names{};
		std::array<uint64_t, Count> CollisionMasks{};
		uint32_t VisibleCount = 16;

		PhysicsLayerSettings() {
			for (uint32_t i = 0; i < Count; ++i) {
				Names[i] = i == 0 ? "Default" : "Layer " + std::to_string(i);
				CollisionMasks[i] = UINT64_MAX;
			}
		}

		int FindIndex(std::string_view name) const {
			for (uint32_t i = 0; i < Count; ++i)
				if (Names[i] == name) return static_cast<int>(i);
			return -1;
		}

		bool SetName(uint32_t index, std::string_view name) {
			if (index >= Count) return false;
			const auto first = name.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) return false;
			name = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
			if (name.size() > 63 || name.find_first_of("\r\n\t") != std::string_view::npos
				|| name.find('\0') != std::string_view::npos || name.find("##") != std::string_view::npos) return false;
			const int existing = FindIndex(name);
			if (existing >= 0 && existing != static_cast<int>(index)) return false;
			Names[index] = name;
			return true;
		}

		bool CanCollide(uint32_t a, uint32_t b) const {
			return a < Count && b < Count && (CollisionMasks[a] & (uint64_t{1} << b)) != 0;
		}

		void SetCollision(uint32_t a, uint32_t b, bool enabled) {
			if (a >= Count || b >= Count) return;
			const auto setBit = [enabled](uint64_t& mask, uint32_t bit) {
				if (enabled) mask |= uint64_t{1} << bit;
				else mask &= ~(uint64_t{1} << bit);
			};
			setBit(CollisionMasks[a], b);
			setBit(CollisionMasks[b], a);
		}

		// Membership in multiple layers allows any enabled pair. Per-collider masks can restrict this further.
		uint64_t GetCollisionMask(uint64_t layers) const {
			uint64_t result = 0;
			while (layers) {
				result |= CollisionMasks[std::countr_zero(layers)];
				layers &= layers - 1;
			}
			return result;
		}
	};
}
