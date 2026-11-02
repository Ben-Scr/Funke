#include <pch.hpp>
#include "Undo/AssetFileHistory.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Hash.hpp"
#include <algorithm>
#include <stdexcept>
#if defined(FNK_PLATFORM_LINUX)
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#elif defined(FNK_PLATFORM_MACOS)
#include <stdio.h>
#endif

namespace Funke {
	namespace {
		namespace fs = std::filesystem;
		bool Exists(const fs::path& path) {
			std::error_code error;
			const auto status = fs::symlink_status(path, error);
			if (error && error != std::errc::no_such_file_or_directory) throw fs::filesystem_error("Inspect asset", path, error);
			return fs::exists(status);
		}
		void RejectLinks(const fs::path& path) {
			for (auto part = path; !part.empty(); part = part.parent_path()) {
				if (Exists(part) && fs::is_symlink(fs::symlink_status(part))) {
#ifdef FNK_PLATFORM_MACOS
					// macOS system aliases are outside the asset tree and have fixed destinations.
					if (part == "/var" || part == "/tmp" || part == "/etc") {
						std::error_code error;
						const auto target = fs::canonical(part, error);
						if (!error && target == fs::path("/private") / part.filename()) continue;
					}
#endif
					throw std::runtime_error("Asset undo does not follow links: " + Path::ToUtf8(part));
				}
#ifdef FNK_PLATFORM_WINDOWS
				const auto attributes = GetFileAttributesW(part.c_str());
				if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("Asset undo does not follow junctions: " + Path::ToUtf8(part));
#endif
				if (part == part.parent_path()) break;
			}
		}
		void RenameExclusive(const fs::path& from, const fs::path& to) {
			RejectLinks(from);
			RejectLinks(to.parent_path());
#ifdef FNK_PLATFORM_WINDOWS
			if (!MoveFileW(from.c_str(), to.c_str())) throw fs::filesystem_error("Move asset without replacing files", from, to, std::error_code(GetLastError(), std::system_category()));
#elif defined(FNK_PLATFORM_LINUX)
			if (syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), 1) != 0) throw fs::filesystem_error("Move asset without replacing files", from, to, std::error_code(errno, std::generic_category()));
#elif defined(FNK_PLATFORM_MACOS)
			if (renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) != 0) throw fs::filesystem_error("Move asset without replacing files", from, to, std::error_code(errno, std::generic_category()));
#else
			throw std::runtime_error("Exclusive asset moves are unavailable on this platform");
#endif
		}
		void Copy(const fs::path& from, const fs::path& to) {
			if (!Exists(from)) return;
			fs::copy(from, to, fs::copy_options::recursive);
		}
		std::string Key(const fs::path& path) {
			auto key = path.generic_string();
#ifdef FNK_PLATFORM_WINDOWS
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
			return key;
		}
		bool Inside(const fs::path& path, const fs::path& parent) {
			const auto key = Key(path), prefix = Key(parent) + "/";
			return key.starts_with(prefix);
		}
		void CollectFiles(const fs::path& physical, const fs::path& logical, std::vector<std::string>& paths) {
			if (!Exists(physical)) return;
			if (fs::is_regular_file(physical)) {
				if (!AssetRegistry::IsMetaFilePath(Path::ToUtf8(logical))) paths.push_back(Path::ToUtf8(logical));
				return;
			}
			for (const auto& entry : fs::recursive_directory_iterator(physical)) {
				if (entry.is_regular_file()) {
					const auto path = logical / entry.path().lexically_relative(physical);
					if (!AssetRegistry::IsMetaFilePath(Path::ToUtf8(path))) paths.push_back(Path::ToUtf8(path));
				}
			}
		}
	}

	AssetFileHistory::Change::~Change() {
		if (!Storage.empty() && !KeepRecoveryFiles) {
			std::error_code error;
			std::filesystem::remove_all(Storage, error);
		}
	}

	void AssetFileHistory::Initialize(const std::filesystem::path& assetsRoot) {
		Clear();
		m_Storage = std::filesystem::absolute(assetsRoot).lexically_normal().parent_path()
			/ ".funke" / "AssetUndo" / std::to_string(static_cast<uint64_t>(UUID()));
	}
	void AssetFileHistory::Clear() { m_Undo.clear(); m_Redo.clear(); }

	bool AssetFileHistory::DestinationAvailable(const std::filesystem::path& path) {
		auto metadata = path; metadata += ".meta";
		return !Exists(path) && !Exists(metadata);
	}

	bool AssetFileHistory::MoveAsset(const std::filesystem::path& from, const std::filesystem::path& to) {
		try {
			if (!Exists(from) || !DestinationAvailable(to)) return false;
			auto fromMeta = from; fromMeta += ".meta";
			auto toMeta = to; toMeta += ".meta";
			const bool metadata = Exists(fromMeta);
			RenameExclusive(from, to);
			try { if (metadata) RenameExclusive(fromMeta, toMeta); }
			catch (...) { RenameExclusive(to, from); throw; }
			return true;
		}
		catch (const std::exception& error) {
			FNK_ERROR_TAG("AssetUndo", "Cannot move asset: {}", error.what());
			return false;
		}
	}

	std::string AssetFileHistory::Fingerprint(const std::filesystem::path& path) {
		RejectLinks(path);
		if (!Exists(path)) return {};
		if (std::filesystem::is_regular_file(path)) {
			std::string digest, error;
			if (!Hash::Sha256OfFile(path, digest, error)) throw std::runtime_error(error);
			return "F" + digest;
		}
		if (!std::filesystem::is_directory(path)) throw std::runtime_error("Unsupported asset file: " + Path::ToUtf8(path));
		std::vector<std::filesystem::path> children;
		for (const auto& entry : std::filesystem::directory_iterator(path)) children.push_back(entry.path());
		std::sort(children.begin(), children.end());
		std::string contents;
		for (const auto& child : children) {
			const auto name = Path::ToUtf8(child.filename());
			contents += std::to_string(name.size()) + ":" + name + Fingerprint(child);
		}
		return "D" + Hash::Sha256OfBytes(contents.data(), contents.size());
	}

	void AssetFileHistory::Capture(Change& change, bool after) {
		auto& signatures = after ? change.After : change.Before;
		const auto directory = change.Storage / (after ? "after" : "before");
		std::filesystem::create_directories(directory);
		for (size_t i = 0; i < change.Paths.size(); ++i) {
			const auto signature = Fingerprint(change.Paths[i]);
			const auto backup = directory / std::to_string(i);
			Copy(change.Paths[i], backup);
			if (Fingerprint(backup) != signature || Fingerprint(change.Paths[i]) != signature) throw std::runtime_error("Asset changed while recording undo");
			signatures.push_back(signature);
		}
	}

	std::shared_ptr<AssetFileHistory::Change> AssetFileHistory::Begin(const std::vector<std::string>& paths, std::string label) {
		if (m_Storage.empty()) throw std::runtime_error("Asset history has no project");
		auto change = std::make_shared<Change>();
		change->Label = std::move(label);
		change->Storage = m_Storage / std::to_string(static_cast<uint64_t>(UUID()));
		RejectLinks(change->Storage);
		for (const auto& path : paths) {
			const auto absolute = std::filesystem::absolute(Path::FromUtf8(path)).lexically_normal();
			if (Inside(change->Storage, absolute) || Key(change->Storage) == Key(absolute)) throw std::runtime_error("Cannot snapshot the asset recovery directory");
			change->Paths.push_back(absolute);
			auto metadata = absolute; metadata += ".meta";
			change->Paths.push_back(metadata);
		}
		std::sort(change->Paths.begin(), change->Paths.end(), [](const auto& a, const auto& b) { return Key(a) < Key(b); });
		change->Paths.erase(std::unique(change->Paths.begin(), change->Paths.end(), [](const auto& a, const auto& b) { return Key(a) == Key(b); }), change->Paths.end());
		const auto candidates = change->Paths;
		std::erase_if(change->Paths, [&](const auto& path) {
			return std::any_of(candidates.begin(), candidates.end(), [&](const auto& parent) { return Inside(path, parent); });
		});
		Capture(*change, false);
		return change;
	}

	void AssetFileHistory::Commit(const std::shared_ptr<Change>& change) {
		if (!change) return;
		Capture(*change, true);
		if (change->Before == change->After) return;
		m_Undo.push_back(change);
		m_Redo.clear();
		if (m_Undo.size() > 256) m_Undo.erase(m_Undo.begin());
	}

	void AssetFileHistory::RecordCreated(const std::vector<std::string>& paths, std::string label) {
		auto change = Begin(paths, std::move(label));
		change->After = std::move(change->Before);
		change->Before.assign(change->Paths.size(), {});
		std::filesystem::rename(change->Storage / "before", change->Storage / "after");
		std::filesystem::create_directory(change->Storage / "before");
		m_Undo.push_back(std::move(change));
		m_Redo.clear();
		if (m_Undo.size() > 256) m_Undo.erase(m_Undo.begin());
	}

	void AssetFileHistory::Apply(Change& change, bool redo) {
		if (change.Validate) change.Validate(redo);
		const auto& expected = redo ? change.Before : change.After;
		const auto& desired = redo ? change.After : change.Before;
		const auto snapshot = change.Storage / (redo ? "after" : "before");
		for (size_t i = 0; i < change.Paths.size(); ++i) {
			if (Fingerprint(change.Paths[i]) != expected[i]) throw std::runtime_error("Undo conflict: " + Path::ToUtf8(change.Paths[i]));
			if (Fingerprint(snapshot / std::to_string(i)) != desired[i]) throw std::runtime_error("Asset undo backup is incomplete");
		}
		const auto transaction = change.Storage / std::to_string(static_cast<uint64_t>(UUID()));
		std::filesystem::create_directories(transaction / "stage");
		std::filesystem::create_directory(transaction / "parked");
		std::vector<std::string> registryPaths;
		for (size_t i = 0; i < change.Paths.size(); ++i) {
			CollectFiles(change.Paths[i], change.Paths[i], registryPaths);
			CollectFiles(snapshot / std::to_string(i), change.Paths[i], registryPaths);
			Copy(snapshot / std::to_string(i), transaction / "stage" / std::to_string(i));
		}
		std::vector<size_t> parked, published;
		parked.reserve(change.Paths.size());
		published.reserve(change.Paths.size());
		try {
			for (size_t i = 0; i < change.Paths.size(); ++i) {
				if (Fingerprint(change.Paths[i]) != expected[i]) throw std::runtime_error("Asset changed during undo");
				if (!expected[i].empty()) {
					RenameExclusive(change.Paths[i], transaction / "parked" / std::to_string(i));
					parked.push_back(i);
				}
			}
			for (size_t i = 0; i < change.Paths.size(); ++i) {
				if (!desired[i].empty()) {
					RenameExclusive(transaction / "stage" / std::to_string(i), change.Paths[i]);
					published.push_back(i);
				}
			}
			if (!AssetRegistry::RestorePathsIncremental(registryPaths)) throw std::runtime_error("Asset registry identity conflict");
		}
		catch (...) {
			try {
				for (auto it = published.rbegin(); it != published.rend(); ++it) RenameExclusive(change.Paths[*it], transaction / "stage" / std::to_string(*it));
				for (auto it = parked.rbegin(); it != parked.rend(); ++it) RenameExclusive(transaction / "parked" / std::to_string(*it), change.Paths[*it]);
			}
			catch (...) {
				change.KeepRecoveryFiles = true;
				FNK_ERROR_TAG("AssetUndo", "Recovery files retained at {}", Path::ToUtf8(transaction));
			}
			throw;
		}
		std::error_code error;
		std::filesystem::remove_all(transaction, error);
		if (change.Applied) {
			try { change.Applied(redo); }
			catch (const std::exception& callbackError) { FNK_ERROR_TAG("AssetUndo", "Files restored, editor refresh failed: {}", callbackError.what()); }
		}
	}
	std::string AssetFileHistory::Undo() {
		if (m_Undo.empty()) return {};
		m_Redo.reserve(m_Redo.size() + 1);
		Apply(*m_Undo.back(), false);
		auto label = m_Undo.back()->Label;
		m_Redo.push_back(std::move(m_Undo.back())); m_Undo.pop_back();
		return label;
	}
	std::string AssetFileHistory::Redo() {
		if (m_Redo.empty()) return {};
		m_Undo.reserve(m_Undo.size() + 1);
		Apply(*m_Redo.back(), true);
		auto label = m_Redo.back()->Label;
		m_Undo.push_back(std::move(m_Redo.back())); m_Redo.pop_back();
		return label;
	}
}
