#include <pch.hpp>
#include "Assets/AssetRegistry.hpp"
#include "Directory.hpp"
#include "Path.hpp"
#include <filesystem>

namespace Funke {

	void Directory::Create(const std::string& dir, bool recursive) {
		if (recursive)
			std::filesystem::create_directories(Path::FromUtf8(dir));
		else
			std::filesystem::create_directory(Path::FromUtf8(dir));
	}

	bool Directory::Exists(const std::string& dir) {
		std::error_code ec;
		const std::filesystem::path nativeDir = Path::FromUtf8(dir);
		return std::filesystem::exists(nativeDir, ec) && std::filesystem::is_directory(nativeDir, ec);
	}

	bool Directory::Delete(const std::string& path) {
		std::error_code ec;
		const std::filesystem::path nativePath = Path::FromUtf8(path);
		if (!std::filesystem::exists(nativePath, ec) || ec)
			return false;

		if (std::filesystem::is_directory(nativePath, ec))
			return std::filesystem::remove_all(nativePath, ec) > 0 && !ec;

		const bool removed = std::filesystem::remove(nativePath, ec) && !ec;
		if (removed) {
			AssetRegistry::DeleteCompanionMetadata(path);
		}
		return removed;
	}

	bool Directory::Move(const std::string& from, const std::string& to) {
		std::error_code ec;
		const std::filesystem::path source = Path::FromUtf8(from);
		if (!std::filesystem::exists(source, ec) || ec)
			return false;

		std::filesystem::path dest = Path::FromUtf8(to);
		if (std::filesystem::is_directory(dest, ec)) {
			dest /= source.filename();
		}

		std::filesystem::rename(source, dest, ec);
		if (!ec && std::filesystem::is_regular_file(dest, ec)) {
			AssetRegistry::MoveCompanionMetadata(from, Path::ToUtf8(dest));
		}
		return !ec;
	}

	bool Directory::Rename(const std::string& path, const std::string& newName) {
		std::error_code ec;
		const std::filesystem::path nativePath = Path::FromUtf8(path);
		if (!std::filesystem::exists(nativePath, ec) || ec)
			return false;

		std::filesystem::path target = nativePath.parent_path() / Path::FromUtf8(newName);
		std::filesystem::rename(nativePath, target, ec);
		if (!ec && std::filesystem::is_regular_file(target, ec)) {
			AssetRegistry::MoveCompanionMetadata(path, Path::ToUtf8(target));
		}
		return !ec;
	}

	std::vector<std::string> Directory::GetAllFiles(const std::string& dir) {
		std::vector<std::string> files;
		std::error_code ec;

		const std::filesystem::path base = Path::FromUtf8(dir);
		if (!std::filesystem::exists(base, ec) || ec) return {};
		if (!std::filesystem::is_directory(base, ec) || ec) return {};

		for (std::filesystem::directory_iterator it(base, ec), end; it != end && !ec; it.increment(ec)) {
			const std::filesystem::directory_entry& entry = *it;
			std::error_code ec2;
			if (entry.is_regular_file(ec2) && !ec2) {
				const std::string entryPath = Path::ToUtf8(entry.path());
				if (AssetRegistry::IsMetaFilePath(entryPath)) {
					continue;
				}
				files.push_back(entryPath);
			}
		}

		std::sort(files.begin(), files.end());
		return files;
	}

	std::vector<DirectoryEntry> Directory::GetEntries(const std::string& dir) {
		std::vector<DirectoryEntry> entries;
		std::error_code ec;

		const std::filesystem::path base = Path::FromUtf8(dir);
		if (!std::filesystem::exists(base, ec) || ec) return {};
		if (!std::filesystem::is_directory(base, ec) || ec) return {};

		for (std::filesystem::directory_iterator it(base, ec), end; it != end && !ec; it.increment(ec)) {
			const std::filesystem::directory_entry& entry = *it;
			std::error_code ec2;

			DirectoryEntry e;
			e.Path = Path::ToUtf8(entry.path());
			e.Name = Path::ToUtf8(entry.path().filename());
			e.IsDirectory = entry.is_directory(ec2) && !ec2;
			if (!e.IsDirectory && AssetRegistry::IsMetaFilePath(e.Path)) {
				continue;
			}

			entries.push_back(std::move(e));
		}

		// Sort: directories first, then alphabetical within each group
		std::sort(entries.begin(), entries.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
			if (a.IsDirectory != b.IsDirectory)
				return a.IsDirectory > b.IsDirectory;
			return a.Name < b.Name;
		});

		return entries;
	}
}
