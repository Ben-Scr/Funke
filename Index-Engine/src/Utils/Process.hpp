#pragma once

#include "Core/Export.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
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

	// Same as Run, but forwards output chunks to onOutput as they arrive (on the calling thread).
	INDEX_API Result Run(const std::vector<std::string>& command, const std::filesystem::path& workingDirectory,
		std::chrono::milliseconds timeout, const std::function<void(std::string_view chunk)>& onOutput);

	INDEX_API bool LaunchDetached(const std::vector<std::string>& command,
		const std::filesystem::path& workingDirectory = {});
	// Same, and reports the started process's id (0 when unknown) for IsProcessRunning.
	INDEX_API bool LaunchDetached(const std::vector<std::string>& command,
		const std::filesystem::path& workingDirectory, unsigned long& outProcessId);
	INDEX_API bool IsProcessRunning(unsigned long processId);
	// Like LaunchDetached, but waits up to `watch` for the child to die in the OS loader (before main: a missing
	// DLL, a stale Index-Engine.dll without an export it needs, a wrong-architecture image). Then it returns
	// false with the cause in outError; a child still running or exiting any other way counts as launched.
	INDEX_API bool LaunchDetachedChecked(const std::vector<std::string>& command,
		const std::filesystem::path& workingDirectory, unsigned long& outProcessId, std::string& outError,
		std::chrono::milliseconds watch = std::chrono::milliseconds(300));

	// Readable cause for an exit code the Windows loader sets before main(); empty for any other code.
	INDEX_API std::string DescribeLoaderFailure(int exitCode);

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
