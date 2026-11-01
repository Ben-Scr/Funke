#pragma once
#include "Core/Export.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Funke {

	class FUNKE_API AssetBundle {
	public:
		static bool Write(const std::filesystem::path& root, const std::filesystem::path& destination,
			const std::vector<std::filesystem::path>& relativeFiles, bool compress, std::string& error);
		bool Open(const std::filesystem::path& path, std::string& error);
		bool Read(const std::string& relativePath, std::vector<std::uint8_t>& bytes, std::string& error) const;
		bool Contains(const std::string& relativePath) const;
		std::vector<std::string> Files() const;

		// Mount before starting asset workers; readers keep their snapshot across project switches.
		static bool MountProject(const std::filesystem::path& root, std::string& error);
		static void UnmountAll();
		static bool ContainsMounted(const std::filesystem::path& path);
		static bool IsMountedDirectory(const std::filesystem::path& path);
		static std::vector<std::filesystem::path> ListMounted(const std::filesystem::path& directory, bool recursive);
		static bool ReadMounted(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes);
		static bool GetMountedStamp(const std::filesystem::path& path, std::uintmax_t& size,
			std::filesystem::file_time_type& writeTime);

	private:
		struct Entry {
			std::string Path;
			std::uint64_t Offset = 0;
			std::uint64_t Size = 0;
			std::uint64_t StoredSize = 0;
			std::uint64_t Checksum = 0;
			std::uint32_t Codec = 0;
		};
		std::filesystem::path m_Path;
		std::filesystem::file_time_type m_WriteTime{};
		std::unordered_map<std::string, Entry> m_Entries;
	};
}
