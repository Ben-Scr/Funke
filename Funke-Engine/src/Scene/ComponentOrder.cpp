#include "pch.hpp"
#include "Scene/ComponentOrder.hpp"

#include <algorithm>

namespace Funke::ComponentOrder {

	std::string ScriptKey(std::string_view className) {
		return "script:" + std::string(className);
	}

	std::string ManagedComponentKey(std::string_view className) {
		return "managed:" + std::string(className);
	}

	std::vector<std::string> Arrange(const std::vector<std::string>& defaultKeys,
		const std::vector<std::string>& saved, std::size_t pinnedCount) {
		pinnedCount = std::min(pinnedCount, defaultKeys.size());
		const auto isFree = [&](const std::string& key) {
			const auto it = std::find(defaultKeys.begin(), defaultKeys.end(), key);
			return it != defaultKeys.end() && static_cast<std::size_t>(it - defaultKeys.begin()) >= pinnedCount;
		};

		std::vector<std::string> result(defaultKeys.begin(), defaultKeys.begin() + static_cast<std::ptrdiff_t>(pinnedCount));
		for (const std::string& key : saved) {
			if (isFree(key) && std::find(result.begin(), result.end(), key) == result.end()) result.push_back(key);
		}
		for (std::size_t i = pinnedCount; i < defaultKeys.size(); ++i) {
			if (std::find(result.begin(), result.end(), defaultKeys[i]) == result.end()) result.push_back(defaultKeys[i]);
		}
		return result;
	}

	std::vector<std::string> Move(std::vector<std::string> keys, std::string_view key,
		std::string_view anchor, bool after, std::size_t pinnedCount) {
		pinnedCount = std::min(pinnedCount, keys.size());
		const auto from = std::find(keys.begin(), keys.end(), key);
		if (from == keys.end() || key == anchor) return keys;
		if (static_cast<std::size_t>(from - keys.begin()) < pinnedCount) return keys;
		if (std::find(keys.begin(), keys.end(), anchor) == keys.end()) return keys;

		std::string moved = std::move(*from);
		keys.erase(from);
		auto target = std::find(keys.begin(), keys.end(), anchor);
		if (after) ++target;
		const auto firstFree = keys.begin() + static_cast<std::ptrdiff_t>(pinnedCount);
		if (target < firstFree) target = firstFree;
		keys.insert(target, std::move(moved));
		return keys;
	}

}
