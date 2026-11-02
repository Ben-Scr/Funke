#include "pch.hpp"
#include "Serialization/FileWatcher.hpp"
#include "Serialization/Path.hpp"
#include "Core/Log.hpp"
#include "Profiling/Profiler.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <cctype>

namespace Funke {

	namespace {
		constexpr std::filesystem::directory_options kDirectoryOptions =
			std::filesystem::directory_options::skip_permission_denied;
		constexpr int kMaxIdlePollIntervalMs = 5000;
#ifdef FNK_PLATFORM_WINDOWS
		constexpr DWORD kNativeWatchTimeoutMs = 250;
#endif

		const std::unordered_set<std::string> kIgnoredDirectoryNames = {
			".git",
			".vs",
			".idea",
			".funke",
			"node_modules",
			"builds",
			"bin",
			"bin-int",
			"obj",
			"build",
			"out"
		};

		std::filesystem::path NormalizeWatchTargetPath(const std::filesystem::path& path) {
			return Path::CanonicalIdentity(path);
		}
	}

	FileWatcher::FileWatcher() = default;

	FileWatcher::~FileWatcher() {
		Stop();
	}

	void FileWatcher::Watch(const std::string& path, const std::string& pattern, Callback callback, bool recursive) {
		Watch(std::vector<std::string>{ path }, std::vector<std::string>{ pattern }, std::move(callback), recursive);
	}

	void FileWatcher::Watch(const std::vector<std::string>& paths, const std::vector<std::string>& patterns, Callback callback, bool recursive) {
		PathsCallback onPaths;
		if (callback) onPaths = [callback = std::move(callback)](const std::vector<std::string>&) { callback(); };
		Watch(paths, patterns, std::move(onPaths), recursive);
	}

	void FileWatcher::Watch(const std::vector<std::string>& paths, const std::vector<std::string>& patterns, PathsCallback callback, bool recursive) {
		Stop();

		ConfigurePatterns(patterns);
		m_Callback = std::move(callback);
		m_Targets.clear();
		m_WatchDescription.clear();

		for (const std::string& pathString : paths) {
			if (pathString.empty()) {
				continue;
			}

			const std::filesystem::path path = Path::FromUtf8(pathString);
			std::error_code ec;
			const bool exists = std::filesystem::exists(path, ec);
			const bool isDirectory = exists ? std::filesystem::is_directory(path, ec) : LooksLikeDirectory(path);
			m_Targets.push_back({ NormalizeWatchTargetPath(path), isDirectory, isDirectory && recursive });
		}

		if (m_Targets.empty() || !m_Callback) {
			m_Callback = nullptr;
			return;
		}

		// Baseline snapshot is built on the worker thread (WorkerMain), not here: Watch()
		// is on the startup critical path and walking a large Assets/ tree would block it.
		m_PendingChanges.store(false);
		m_Watching.store(true);
		m_WatchDescription = m_Targets.size() == 1
			? Path::ToUtf8(m_Targets.front().Path)
			: std::to_string(m_Targets.size()) + " targets";

#ifdef FNK_PLATFORM_WINDOWS
		// Created before the worker so Stop() can signal it to break WaitForMultipleObjects at once.
		m_NativeStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
#endif
		m_Worker = std::thread(&FileWatcher::WorkerMain, this);
		FNK_CORE_INFO_TAG("FileWatcher", "Watching {} for {} pattern(s)", m_WatchDescription, patterns.size());
	}

	void FileWatcher::Stop() {
		m_Watching.store(false);
#ifdef FNK_PLATFORM_WINDOWS
		if (m_NativeStopEvent) {
			SetEvent(static_cast<HANDLE>(m_NativeStopEvent));
		}
#endif
		m_WakeCondition.notify_all();

		if (m_Worker.joinable()) {
			m_Worker.join();
		}

#ifdef FNK_PLATFORM_WINDOWS
		if (m_NativeStopEvent) {
			CloseHandle(static_cast<HANDLE>(m_NativeStopEvent));
			m_NativeStopEvent = nullptr;
		}
#endif

		m_Targets.clear();
		m_Extensions.clear();
		m_Filenames.clear();
		m_FileTimestamps.clear();
		m_Callback = nullptr;
		m_PendingPaths.clear();
		m_PendingChanges.store(false);
		m_WatchDescription.clear();
	}

	void FileWatcher::Poll(float pollIntervalSeconds) {
		const int pollIntervalMs = std::max(50, static_cast<int>(pollIntervalSeconds * 1000.0f));
		m_PollIntervalMs.store(pollIntervalMs);

		if (!m_PendingChanges.load()) {
			return;
		}

		// Copy callback+description under lock (races Stop()), but do NOT hold the lock during invocation (callbacks re-enter FS/scene).
		PathsCallback callbackCopy;
		std::vector<std::string> changedPaths;
		std::string descriptionCopy;
		{
			std::scoped_lock<std::mutex> lock(m_StateMutex);
			if (!m_Watching.load()) {
				return;
			}
			callbackCopy = m_Callback;
			descriptionCopy = m_WatchDescription;
			changedPaths.assign(m_PendingPaths.begin(), m_PendingPaths.end());
			m_PendingPaths.clear();
			m_PendingChanges.store(false);
		}
		if (!callbackCopy || changedPaths.empty()) {
			return;
		}

		FNK_CORE_INFO_TAG("FileWatcher", "Changes detected in {}", descriptionCopy);
		callbackCopy(changedPaths);
	}

	bool FileWatcher::PublishSnapshot(Snapshot nextSnapshot) {
		std::vector<std::string> changedPaths;
		for (const auto& [path, fingerprint] : nextSnapshot) {
			const auto previous = m_FileTimestamps.find(path);
			if (previous == m_FileTimestamps.end() || previous->second != fingerprint) changedPaths.push_back(path);
		}
		for (const auto& [path, fingerprint] : m_FileTimestamps) {
			if (!nextSnapshot.contains(path)) changedPaths.push_back(path);
		}
		m_FileTimestamps = std::move(nextSnapshot);
		if (changedPaths.empty()) return false;

		std::scoped_lock lock(m_StateMutex);
		m_PendingPaths.insert(changedPaths.begin(), changedPaths.end());
		m_PendingChanges.store(true);
		return true;
	}

	void FileWatcher::WorkerMain() {
		FUNKE_PROFILE_THREAD_NAME("FileWatcher");

		// Build the baseline off the caller's thread (see Watch). Only this thread touches
		// m_FileTimestamps, and the waits below compare against it, so nothing reads it early.
		m_FileTimestamps = BuildSnapshot();

#ifdef FNK_PLATFORM_WINDOWS
		if (WaitForNativeChanges()) {
			return;
		}
#endif

		int idlePollIntervalMs = m_PollIntervalMs.load();

		while (m_Watching.load()) {
			std::unique_lock<std::mutex> lock(m_StateMutex);
			m_WakeCondition.wait_for(lock, std::chrono::milliseconds(idlePollIntervalMs), [this]() {
				return !m_Watching.load();
			});
			lock.unlock();

			if (!m_Watching.load()) {
				break;
			}

			if (PublishSnapshot(BuildSnapshot())) {
				idlePollIntervalMs = m_PollIntervalMs.load();
			}
			else {
				idlePollIntervalMs = std::min(kMaxIdlePollIntervalMs, std::max(m_PollIntervalMs.load(), idlePollIntervalMs * 2));
			}
		}
	}

	bool FileWatcher::WaitForNativeChanges() {
#ifndef FNK_PLATFORM_WINDOWS
		return false;
#else
		struct NativeWatchRoot {
			std::filesystem::path Path;
			bool Recursive = false;
		};

		std::unordered_map<std::string, NativeWatchRoot> rootsByKey;
		for (const WatchTarget& target : m_Targets) {
			std::filesystem::path rootPath = target.IsDirectory ? target.Path : target.Path.parent_path();
			if (rootPath.empty()) {
				continue;
			}

			std::error_code ec;
			if (!std::filesystem::exists(rootPath, ec) || !std::filesystem::is_directory(rootPath, ec)) {
				continue;
			}

			// File targets watch their parent dir non-recursively (we only
			// care about the file itself); directory targets honor the
			// caller-supplied Recursive flag.
			const bool wantRecursive = target.IsDirectory && target.Recursive;
			NativeWatchRoot root{ NormalizeWatchTargetPath(rootPath), wantRecursive };
			const std::string key = NormalizeKey(root.Path);
			auto [it, inserted] = rootsByKey.emplace(key, root);
			if (!inserted) {
				it->second.Recursive = it->second.Recursive || root.Recursive;
			}
		}

		if (rootsByKey.empty()) {
			return false;
		}

		if (rootsByKey.size() >= MAXIMUM_WAIT_OBJECTS) return false;

		struct NativeWatch {
			NativeWatchRoot Root;
			HANDLE Directory = INVALID_HANDLE_VALUE;
			OVERLAPPED Overlap{};
			alignas(DWORD) std::array<unsigned char, 32768> Buffer{};
			bool Pending = false;
			~NativeWatch() {
				if (Pending) {
					CancelIoEx(Directory, &Overlap);
					DWORD bytes = 0;
					GetOverlappedResult(Directory, &Overlap, &bytes, TRUE);
				}
				if (Directory != INVALID_HANDLE_VALUE) CloseHandle(Directory);
				if (Overlap.hEvent) CloseHandle(Overlap.hEvent);
			}
			bool Arm() {
				const HANDLE event = Overlap.hEvent;
				Overlap = {};
				Overlap.hEvent = event;
				ResetEvent(Overlap.hEvent);
				Pending = ReadDirectoryChangesW(Directory, Buffer.data(), static_cast<DWORD>(Buffer.size()),
					Root.Recursive, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
					| FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_SIZE,
					nullptr, &Overlap, nullptr) != FALSE;
				return Pending;
			}
		};
		std::vector<std::unique_ptr<NativeWatch>> watches;
		std::vector<HANDLE> waitHandles;
		for (const auto& [key, root] : rootsByKey) {
			auto watch = std::make_unique<NativeWatch>();
			watch->Root = root;
			watch->Directory = CreateFileW(root.Path.c_str(), FILE_LIST_DIRECTORY,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
			watch->Overlap.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (watch->Directory == INVALID_HANDLE_VALUE || !watch->Overlap.hEvent || !watch->Arm()) return false;
			waitHandles.push_back(watch->Overlap.hEvent);
			watches.push_back(std::move(watch));
		}
		const HANDLE stopEvent = static_cast<HANDLE>(m_NativeStopEvent);
		if (stopEvent) waitHandles.push_back(stopEvent);
		PublishSnapshot(BuildSnapshot());

		while (m_Watching.load()) {
			const DWORD result = WaitForMultipleObjects(static_cast<DWORD>(waitHandles.size()),
				waitHandles.data(), FALSE, stopEvent ? INFINITE : kNativeWatchTimeoutMs);
			if (result == WAIT_TIMEOUT) continue;
			if (stopEvent && result == WAIT_OBJECT_0 + watches.size()) break;
			if (result < WAIT_OBJECT_0 || result >= WAIT_OBJECT_0 + watches.size()) return false;
			auto& watch = *watches[result - WAIT_OBJECT_0];
			DWORD byteCount = 0;
			const bool completed = GetOverlappedResult(watch.Directory, &watch.Overlap, &byteCount, FALSE) != FALSE;
			const DWORD error = completed ? ERROR_SUCCESS : GetLastError();
			watch.Pending = false;
			if (!completed && error != ERROR_NOTIFY_ENUM_DIR) return false;
			alignas(DWORD) const auto buffer = watch.Buffer;
			if (!watch.Arm()) return false;
			bool rescan = !completed || byteCount == 0;
			std::unordered_set<std::string> changed;
			for (DWORD offset = 0; !rescan && offset < byteCount;) {
				if (byteCount - offset < offsetof(FILE_NOTIFY_INFORMATION, FileName)) { rescan = true; break; }
				const auto* notification = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
				if (notification->FileNameLength > byteCount - offset - offsetof(FILE_NOTIFY_INFORMATION, FileName)
					|| notification->FileNameLength % sizeof(wchar_t) != 0) { rescan = true; break; }
				const auto relative = std::filesystem::path(std::wstring(notification->FileName,
					notification->FileNameLength / sizeof(wchar_t)));
				const auto path = watch.Root.Path / relative;
				bool ignored = false;
				for (const auto& part : relative.parent_path()) if (ShouldIgnoreDirectory(part)) { ignored = true; break; }
				std::error_code ec;
				const bool directory = std::filesystem::is_directory(path, ec);
				if (!ignored && !ShouldIgnoreDirectory(path)) {
					if (notification->Action == FILE_ACTION_REMOVED || notification->Action == FILE_ACTION_RENAMED_OLD_NAME) {
						const std::string prefix = NormalizeKey(path) + static_cast<char>(std::filesystem::path::preferred_separator);
						for (const auto& [key, stamp] : m_FileTimestamps) if (key.starts_with(prefix)) { rescan = true; break; }
					}
					if (directory || (!MatchesFile(path) && (notification->Action == FILE_ACTION_REMOVED
						|| notification->Action == FILE_ACTION_RENAMED_OLD_NAME))) rescan = true;
					else if (MatchesFile(path)) {
						bool tracked = false;
						for (const WatchTarget& target : m_Targets) {
							if (!target.IsDirectory) { tracked |= path == target.Path; continue; }
							const auto rel = path.lexically_relative(target.Path);
							if (rel.empty() || *rel.begin() == L"..") continue;
							if (!target.Recursive && path.parent_path() != target.Path) continue;
							const auto resolved = Path::CanonicalIdentity(path).lexically_relative(target.Path);
							if (!resolved.empty() && *resolved.begin() != L"..") tracked = true;
						}
						if (tracked) {
							const std::string key = NormalizeKey(path);
							std::filesystem::directory_entry entry(path, ec);
							if (!ec && entry.is_regular_file(ec)) {
								const auto stamp = entry.last_write_time(ec);
								if (ec) { rescan = true; break; }
								const auto size = entry.file_size(ec);
								if (ec) { rescan = true; break; }
								const FileFingerprint next{ stamp, size };
								const auto previous = m_FileTimestamps.find(key);
								if (previous == m_FileTimestamps.end() || previous->second != next) changed.insert(key);
								m_FileTimestamps[key] = next;
							}
							else if (m_FileTimestamps.erase(key) != 0) changed.insert(key);
						}
					}
				}
				if (notification->NextEntryOffset == 0) break;
				if (notification->NextEntryOffset < offsetof(FILE_NOTIFY_INFORMATION, FileName)
					|| notification->NextEntryOffset > byteCount - offset) { rescan = true; break; }
				offset += notification->NextEntryOffset;
			}
			if (!changed.empty()) {
				std::lock_guard lock(m_StateMutex);
				m_PendingPaths.insert(changed.begin(), changed.end());
				m_PendingChanges.store(true);
			}
			if (rescan) PublishSnapshot(BuildSnapshot());
		}

		return true;
#endif
	}

	FileWatcher::Snapshot FileWatcher::BuildSnapshot() const {
		Snapshot snapshot;
		std::unordered_map<std::filesystem::path, std::filesystem::path> resolvedRoots;

		auto recordFile = [&](const std::filesystem::directory_entry& entry,
				const std::filesystem::path& watchRoot, std::error_code& ec) {
			if (!entry.is_regular_file(ec) || ec || !MatchesFile(entry.path())) {
				ec.clear();
				return;
			}

			// Symlink-escape guard: a malicious or unintended symlink inside the
			// watched tree could resolve to a file outside the asset root. Resolve
			// the path canonically and reject if it sits outside `watchRoot`.
			std::error_code resolveEc;
			std::filesystem::path resolved = std::filesystem::weakly_canonical(entry.path(), resolveEc);
			if (resolveEc) {
				resolved = entry.path();
				resolveEc.clear();
			}
			auto rootIt = resolvedRoots.find(watchRoot);
			if (rootIt == resolvedRoots.end()) rootIt = resolvedRoots.emplace(watchRoot, Path::CanonicalIdentity(watchRoot)).first;
			const std::filesystem::path& resolvedRoot = rootIt->second;
			const std::string resolvedStr = Path::ToUtf8(resolved);
			std::string rootStr = Path::ToUtf8(resolvedRoot);
			// Ensure trailing separator so "/proj/Assets" doesn't falsely match "/proj/Assets-Backup/foo.png".
			if (!rootStr.empty() && rootStr.back() != std::filesystem::path::preferred_separator) {
				rootStr.push_back(std::filesystem::path::preferred_separator);
			}
			if (!rootStr.empty() && (resolvedStr.size() < rootStr.size() ||
				resolvedStr.compare(0, rootStr.size(), rootStr) != 0)) {
				ec.clear();
				return;
			}

			const std::string key = NormalizeKey(entry.path());
			const std::filesystem::file_time_type writeTime = entry.last_write_time(ec);
			if (ec) {
				ec.clear();
				return;
			}
			std::error_code sizeEc;
			const std::uintmax_t size = entry.file_size(sizeEc);
			snapshot[key] = FileFingerprint{ writeTime, sizeEc ? std::uintmax_t{ 0 } : size };
			ec.clear();
		};

		for (const WatchTarget& target : m_Targets) {
			std::error_code ec;
			if (target.IsDirectory) {
				if (!std::filesystem::exists(target.Path, ec) || !std::filesystem::is_directory(target.Path, ec)) {
					continue;
				}

				if (target.Recursive) {
					for (std::filesystem::recursive_directory_iterator it(target.Path, kDirectoryOptions, ec), end;
						 it != end;
						 it.increment(ec)) {
						if (ec) {
							ec.clear();
							continue;
						}

						if (it->is_directory(ec)) {
							if (!ec && ShouldIgnoreDirectory(it->path())) {
								it.disable_recursion_pending();
							}
							ec.clear();
							continue;
						}

						recordFile(*it, target.Path, ec);
					}
				} else {
					for (std::filesystem::directory_iterator it(target.Path, kDirectoryOptions, ec), end;
						 it != end;
						 it.increment(ec)) {
						if (ec) {
							ec.clear();
							continue;
						}
						if (it->is_directory(ec)) {
							ec.clear();
							continue;
						}
						recordFile(*it, target.Path, ec);
					}
				}

				continue;
			}

			if (!std::filesystem::exists(target.Path, ec) || !std::filesystem::is_regular_file(target.Path, ec) || !MatchesFile(target.Path)) {
				continue;
			}

			const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(target.Path, ec);
			if (ec) {
				continue;
			}
			std::error_code sizeEc;
			const std::uintmax_t size = std::filesystem::file_size(target.Path, sizeEc);
			snapshot[NormalizeKey(target.Path)] = FileFingerprint{ writeTime, sizeEc ? std::uintmax_t{ 0 } : size };
		}

		return snapshot;
	}

	void FileWatcher::ConfigurePatterns(const std::vector<std::string>& patterns) {
		m_Extensions.clear();
		m_Filenames.clear();

		for (const std::string& pattern : patterns) {
			if (pattern.empty()) {
				continue;
			}

			const std::string normalizedPattern = ToLowerCopy(pattern);
			if (!normalizedPattern.empty() && normalizedPattern.front() == '.') {
				m_Extensions.insert(normalizedPattern);
			}
			else {
				m_Filenames.insert(normalizedPattern);
			}
		}
	}

	bool FileWatcher::MatchesFile(const std::filesystem::path& filePath) const {
		if (m_Extensions.empty() && m_Filenames.empty()) {
			return true;
		}

		const std::string fileName = ToLowerCopy(Path::ToUtf8(filePath.filename()));
		if (m_Filenames.contains(fileName)) {
			return true;
		}

		const std::string extension = ToLowerCopy(Path::ToUtf8(filePath.extension()));
		return !extension.empty() && m_Extensions.contains(extension);
	}

	bool FileWatcher::ShouldIgnoreDirectory(const std::filesystem::path& directoryPath) const {
		return kIgnoredDirectoryNames.contains(ToLowerCopy(Path::ToUtf8(directoryPath.filename())));
	}

	std::string FileWatcher::NormalizeKey(const std::filesystem::path& path) {
		std::error_code ec;
		std::filesystem::path normalized = std::filesystem::absolute(path, ec);
		if (ec) {
			normalized = path.lexically_normal();
		}
		else {
			normalized = normalized.lexically_normal();
		}

		return Path::ToUtf8(normalized.make_preferred());
	}

	std::string FileWatcher::ToLowerCopy(std::string value) {
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return value;
	}

	bool FileWatcher::LooksLikeDirectory(const std::filesystem::path& path) {
		const std::string native = Path::ToUtf8(path.lexically_normal().make_preferred());
		if (!native.empty()) {
			const char lastCharacter = native.back();
			if (lastCharacter == '/' || lastCharacter == '\\') {
				return true;
			}
		}

		return !path.has_extension();
	}

} // namespace Funke
