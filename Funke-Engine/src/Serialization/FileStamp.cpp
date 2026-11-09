#include "pch.hpp"
#include "Serialization/FileStamp.hpp"
#include "Serialization/Path.hpp"
#include "Assets/AssetBundle.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

namespace Funke {

	namespace {
		std::mutex s_Mutex;
		std::unordered_map<std::string, FileStamp> s_Stamps;

		std::string MakeKey(const std::string& path) {
			std::string key = Path::ToUtf8(Path::FromUtf8(path).lexically_normal().make_preferred());
#ifdef FNK_PLATFORM_WINDOWS
			std::transform(key.begin(), key.end(), key.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
			return key;
		}
	}

	FileStamp FileStamp::Read(const std::string& path) {
		FileStamp stamp;
		if (path.empty()) return stamp;
		const std::filesystem::path fsPath = Path::FromUtf8(path);
		if (AssetBundle::GetMountedStamp(fsPath, stamp.Size, stamp.WriteTime)) {
			stamp.Exists = true;
			return stamp;
		}
		std::error_code ec;
#ifdef FNK_PLATFORM_WINDOWS
		// MSVC caches size and write time in one directory-entry query for ordinary files.
		static const bool referenceReads = [] {
			const char* value = std::getenv("FUNKE_REFERENCE_FILE_STAMP");
			return value && std::string_view(value) == "1";
		}();
		if (!referenceReads) {
			const std::filesystem::directory_entry entry(fsPath, ec);
			if (!ec && std::filesystem::is_regular_file(entry.symlink_status(ec))) {
				stamp.WriteTime = entry.last_write_time(ec);
				if (ec) return FileStamp{};
				stamp.Size = entry.file_size(ec);
				if (ec) return FileStamp{};
				stamp.Exists = true;
				return stamp;
			}
		}
#endif
		stamp.WriteTime = std::filesystem::last_write_time(fsPath, ec);
		if (ec) return FileStamp{};
		stamp.Size = std::filesystem::file_size(fsPath, ec);
		if (ec) return FileStamp{};
		stamp.Exists = true;
		return stamp;
	}

	void KnownFileStamps::Record(const std::string& path) {
		if (path.empty()) return;
		const FileStamp stamp = FileStamp::Read(path);
		std::lock_guard<std::mutex> lock(s_Mutex);
		s_Stamps[MakeKey(path)] = stamp;
	}

	void KnownFileStamps::Forget(const std::string& path) {
		std::lock_guard<std::mutex> lock(s_Mutex);
		s_Stamps.erase(MakeKey(path));
	}

	bool KnownFileStamps::HasChangedOnDisk(const std::string& path) {
		if (path.empty()) return false;
		FileStamp known;
		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			const auto it = s_Stamps.find(MakeKey(path));
			if (it == s_Stamps.end()) return false;
			known = it->second;
		}
		return FileStamp::Read(path) != known;
	}

}
