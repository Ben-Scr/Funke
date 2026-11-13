#pragma once

#include <chrono>
#include <format>
#include <string>

namespace Funke {

	// Wall-clock duration for progress UI, independent of the game's time scale.
	inline std::string FormatElapsedTime(std::chrono::steady_clock::time_point startedAt) {
		const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::steady_clock::now() - startedAt).count();
		if (seconds >= 3600) {
			return std::format("{:02}:{:02}:{:02}", seconds / 3600, (seconds / 60) % 60, seconds % 60);
		}
		return std::format("{:02}:{:02}", seconds / 60, seconds % 60);
	}

}
