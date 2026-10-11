#pragma once

#include "Core/Export.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

namespace Index::Process {

	struct INDEX_API Result {
		int ExitCode = -1;
		std::string Output;
		bool TimedOut = false;
		bool Cancelled = false;

		bool Succeeded() const { return ExitCode == 0 && !TimedOut && !Cancelled; }
	};

	// A zero timeout opts into an unbounded wait; the default prevents stalled tools from blocking shutdown forever.
	INDEX_API Result Run(const std::vector<std::string>& command,
		const std::filesystem::path& workingDirectory = {},
		std::chrono::milliseconds timeout = std::chrono::minutes(10));

	template <typename String,
		std::enable_if_t<std::is_same_v<std::remove_cvref_t<String>, std::string>, int> = 0>
	Result Run(const std::vector<std::string>& command, String&& workingDirectory,
		std::chrono::milliseconds timeout = std::chrono::minutes(10)) {
		const std::filesystem::path nativeWorkingDirectory = std::filesystem::u8path(
			workingDirectory.begin(), workingDirectory.end());
		return Run(command, nativeWorkingDirectory, timeout);
	}

	INDEX_API bool LaunchDetached(const std::vector<std::string>& command,
		const std::filesystem::path& workingDirectory = {});

	template <typename String,
		std::enable_if_t<std::is_same_v<std::remove_cvref_t<String>, std::string>, int> = 0>
	bool LaunchDetached(const std::vector<std::string>& command, String&& workingDirectory) {
		const std::filesystem::path nativeWorkingDirectory = std::filesystem::u8path(
			workingDirectory.begin(), workingDirectory.end());
		return LaunchDetached(command, nativeWorkingDirectory);
	}
	INDEX_API void CancelAll();
	INDEX_API void ResetCancellation();

} // namespace Index::Process
