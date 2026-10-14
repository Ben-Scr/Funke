#pragma once
#include "Core/Export.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Index {

	// (mtime, size): mtime alone misses a save-via-rename that lands inside the timer resolution.
	struct FileStamp {
		std::filesystem::file_time_type WriteTime{};
		std::uintmax_t Size = 0;
		bool Exists = false;

		bool operator==(const FileStamp&) const = default;

		INDEX_API static FileStamp Read(const std::string& path);
	};

	// Last on-disk state the engine itself read or wrote per file, so a file watcher can tell
	// external edits (another tool, an AI agent, a git pull) apart from the engine's own saves.
	class INDEX_API KnownFileStamps {
	public:
		KnownFileStamps() = delete;

		static void Record(const std::string& path);
		static void Forget(const std::string& path);
		// False for paths never recorded: nothing loaded from them can be stale.
		static bool HasChangedOnDisk(const std::string& path);
	};

}
