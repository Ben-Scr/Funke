#pragma once

#include <charconv>
#include <string>
#include <string_view>

namespace Funke::ManagedSdk {
	inline constexpr const char* k_Version = "10.0.100";
	inline constexpr const char* k_RollForward = "latestFeature";

	inline bool IsCompatibleVersion(std::string_view version, unsigned* patchVersion = nullptr) {
		if (!version.starts_with("10.0.")) return false;
		version.remove_prefix(5);
		unsigned patch = 0;
		const auto parsed = std::from_chars(version.data(), version.data() + version.size(), patch);
		if (parsed.ec != std::errc{} || parsed.ptr != version.data() + version.size() || patch < 100) return false;
		if (patchVersion) *patchVersion = patch;
		return true;
	}

	inline bool FindCompatibleSdk(std::string_view listing, std::string& version) {
		version.clear();
		unsigned bestPatch = 0;
		while (!listing.empty()) {
			const auto end = listing.find('\n');
			auto line = listing.substr(0, end);
			const auto start = line.find_first_not_of(" \t\r");
			if (start != std::string_view::npos) {
				line.remove_prefix(start);
				const auto token = line.substr(0, line.find_first_of(" \t\r"));
				unsigned patch = 0;
				if (IsCompatibleVersion(token, &patch) && patch > bestPatch) {
					version = token;
					bestPatch = patch;
				}
			}
			if (end == std::string_view::npos) break;
			listing.remove_prefix(end + 1);
		}
		return !version.empty();
	}
}
