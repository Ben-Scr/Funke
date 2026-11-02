#include "pch.hpp"
#include "Assets/AssetBundle.hpp"
#include "Serialization/Path.hpp"

#define LZ4LIB_VISIBILITY
#include "../../../vendor/lz4/lz4.c"

#include <algorithm>
#include <array>
#include <fstream>
#include <memory>
#include <mutex>

namespace Funke {
	namespace {
		constexpr std::array<char, 8> k_Magic = { 'F', 'N', 'K', 'B', 'N', 'D', 'L', '1' };
		constexpr std::uint64_t k_HeaderSize = 20;
		constexpr std::uint64_t k_MaxEntrySize = LZ4_MAX_INPUT_SIZE;
		struct MountedBundle {
			std::string Root;
			std::filesystem::path NativeRoot;
			std::shared_ptr<const AssetBundle> Bundle;
		};
		std::mutex g_MountMutex;
		std::vector<MountedBundle> g_Mounts;

		std::string Key(std::string path) {
			std::replace(path.begin(), path.end(), '\\', '/');
#ifdef FNK_PLATFORM_WINDOWS
			std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
#endif
			return path;
		}

		std::string AbsoluteKey(const std::filesystem::path& path) {
			auto key = Key(Path::ToGenericUtf8(Path::CanonicalIdentity(path)));
			while (key.size() > 1 && key.back() == '/') key.pop_back();
			return key;
		}

		bool SafeName(const std::string& name) {
			if (name.empty() || name.size() > 4096 || name.front() == '/' || name.back() == '/'
				|| name.find_first_of("\\:") != std::string::npos || name.find('\0') != std::string::npos) return false;
			for (const auto& part : Path::FromUtf8(name)) {
				if (part == "." || part == ".." || part.empty()) return false;
			}
			return name.find("//") == std::string::npos;
		}

		template<typename T> void WriteNumber(std::ostream& out, T value) {
			for (size_t i = 0; i < sizeof(T); ++i) out.put(static_cast<char>((value >> (i * 8)) & 0xff));
		}
		template<typename T> bool ReadNumber(std::istream& in, T& value) {
			value = 0;
			for (size_t i = 0; i < sizeof(T); ++i) {
				const int byte = in.get();
				if (byte == std::char_traits<char>::eof()) return false;
				value |= static_cast<T>(static_cast<unsigned char>(byte)) << (i * 8);
			}
			return true;
		}
		std::uint64_t Checksum(const std::vector<std::uint8_t>& bytes) {
			std::uint64_t hash = 14695981039346656037ull;
			for (const auto byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
			return hash;
		}
		std::vector<MountedBundle> Mounts() {
			std::lock_guard lock(g_MountMutex);
			return g_Mounts;
		}
		std::pair<std::shared_ptr<const AssetBundle>, std::string> Find(const std::filesystem::path& path) {
			const auto mounts = Mounts();
			if (mounts.empty() || path.empty()) return {};
			const auto key = AbsoluteKey(path);
			for (const auto& mount : mounts) {
				if (key.starts_with(mount.Root)) {
					auto relative = key.substr(mount.Root.size());
					if (mount.Bundle->Contains(relative)) return { mount.Bundle, std::move(relative) };
				}
			}
			return {};
		}
	}

	bool AssetBundle::Write(const std::filesystem::path& root, const std::filesystem::path& destination,
		const std::vector<std::filesystem::path>& relativeFiles, bool compress, std::string& error) {
		error.clear();
		auto temporary = destination;
		temporary += ".tmp";
		try {
			std::vector<std::string> names;
			for (const auto& file : relativeFiles) {
				auto name = Path::ToGenericUtf8(file);
				if (!SafeName(name)) throw std::runtime_error("Unsafe bundle path: " + name);
				names.push_back(std::move(name));
			}
			std::sort(names.begin(), names.end());
			if (names.size() > 1000000) throw std::runtime_error("Too many bundle entries");
			std::unordered_map<std::string, bool> unique;
			std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
			out.exceptions(std::ios::badbit | std::ios::failbit);
			out.write(k_Magic.data(), k_Magic.size());
			WriteNumber(out, static_cast<std::uint32_t>(names.size()));
			WriteNumber(out, std::uint64_t{0});
			std::vector<Entry> entries;
			for (const auto& name : names) {
				if (!unique.emplace(Key(name), true).second) throw std::runtime_error("Duplicate bundle path: " + name);
				const auto source = root / Path::FromUtf8(name);
				const auto canonicalRoot = std::filesystem::canonical(root);
				const auto canonicalSource = std::filesystem::canonical(source);
				if (!SafeName(Path::ToGenericUtf8(canonicalSource.lexically_relative(canonicalRoot))))
					throw std::runtime_error("Bundle source leaves the content root: " + name);
				const auto size = std::filesystem::file_size(source);
				if (size > k_MaxEntrySize) throw std::runtime_error("Bundle entry exceeds 2 GB: " + name);
				std::vector<std::uint8_t> bytes(static_cast<size_t>(size));
				std::ifstream in(source, std::ios::binary);
				if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
					throw std::runtime_error("Could not read bundle source: " + name);
				Entry entry;
				entry.Path = name;
				entry.Offset = static_cast<std::uint64_t>(out.tellp());
				entry.Size = size;
				entry.Checksum = Checksum(bytes);
				std::vector<char> packed;
				int packedSize = 0;
				if (compress && !bytes.empty()) {
					packed.resize(static_cast<size_t>(LZ4_compressBound(static_cast<int>(size))));
					packedSize = LZ4_compress_default(reinterpret_cast<const char*>(bytes.data()), packed.data(),
						static_cast<int>(size), static_cast<int>(packed.size()));
					if (packedSize <= 0) throw std::runtime_error("Could not compress bundle source: " + name);
				}
				if (packedSize > 0 && static_cast<std::uint64_t>(packedSize) < size) {
					entry.Codec = 1;
					entry.StoredSize = packedSize;
					out.write(packed.data(), packedSize);
				} else {
					entry.StoredSize = size;
					if (!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(size));
				}
				entries.push_back(std::move(entry));
			}
			const auto indexOffset = static_cast<std::uint64_t>(out.tellp());
			for (const auto& entry : entries) {
				WriteNumber(out, static_cast<std::uint32_t>(entry.Path.size()));
				out.write(entry.Path.data(), entry.Path.size());
				WriteNumber(out, entry.Offset);
				WriteNumber(out, entry.Size);
				WriteNumber(out, entry.StoredSize);
				WriteNumber(out, entry.Checksum);
				WriteNumber(out, entry.Codec);
			}
			out.seekp(12);
			WriteNumber(out, indexOffset);
			out.close();
			std::filesystem::rename(temporary, destination);
			return true;
		} catch (const std::exception& ex) {
			error = ex.what();
			std::error_code cleanup;
			std::filesystem::remove(temporary, cleanup);
			return false;
		}
	}

	bool AssetBundle::Open(const std::filesystem::path& path, std::string& error) {
		m_Entries.clear();
		m_Path.clear();
		error.clear();
		try {
			const auto fileSize = std::filesystem::file_size(path);
			std::ifstream in(path, std::ios::binary);
			std::array<char, 8> magic{};
			std::uint32_t count = 0;
			std::uint64_t indexOffset = 0;
			if (!in.read(magic.data(), magic.size()) || magic != k_Magic
				|| !ReadNumber(in, count) || !ReadNumber(in, indexOffset)
				|| indexOffset < k_HeaderSize || indexOffset > fileSize || count > 1000000
				|| count > (fileSize - indexOffset) / 41) throw std::runtime_error("Invalid asset bundle header");
			in.seekg(static_cast<std::streamoff>(indexOffset));
			std::uint64_t nextOffset = k_HeaderSize;
			for (std::uint32_t i = 0; i < count; ++i) {
				Entry entry;
				std::uint32_t length = 0;
				if (!ReadNumber(in, length) || length == 0 || length > 4096) throw std::runtime_error("Invalid bundle path length");
				entry.Path.resize(length);
				if (!in.read(entry.Path.data(), length) || !SafeName(entry.Path)
					|| !ReadNumber(in, entry.Offset) || !ReadNumber(in, entry.Size)
					|| !ReadNumber(in, entry.StoredSize) || !ReadNumber(in, entry.Checksum) || !ReadNumber(in, entry.Codec)
					|| entry.Offset != nextOffset || entry.Offset > indexOffset || entry.StoredSize > indexOffset - entry.Offset
					|| entry.Size > k_MaxEntrySize || entry.Codec > 1
					|| (entry.Codec == 0 && entry.StoredSize != entry.Size)
					|| (entry.Codec == 1 && (entry.StoredSize == 0 || entry.StoredSize >= entry.Size)))
					throw std::runtime_error("Invalid bundle entry");
				nextOffset += entry.StoredSize;
				const auto key = Key(entry.Path);
				if (!m_Entries.emplace(key, std::move(entry)).second) throw std::runtime_error("Duplicate bundle entry");
			}
			if (nextOffset != indexOffset || in.peek() != std::char_traits<char>::eof())
				throw std::runtime_error("Invalid bundle index size");
			m_Path = path;
			m_WriteTime = std::filesystem::last_write_time(path);
			return true;
		} catch (const std::exception& ex) {
			error = ex.what();
			m_Entries.clear();
			m_Path.clear();
			return false;
		}
	}

	bool AssetBundle::Read(const std::string& relativePath, std::vector<std::uint8_t>& bytes, std::string& error) const {
		bytes.clear();
		error.clear();
		try {
			const auto found = m_Entries.find(Key(relativePath));
			if (found == m_Entries.end()) throw std::runtime_error("Asset is not in the bundle");
			const auto& entry = found->second;
			std::ifstream in(m_Path, std::ios::binary);
			in.seekg(static_cast<std::streamoff>(entry.Offset));
			std::vector<std::uint8_t> stored(static_cast<size_t>(entry.StoredSize));
			if (!in.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size())))
				throw std::runtime_error("Could not read bundle payload");
			if (entry.Codec == 0) bytes = std::move(stored);
			else {
				bytes.resize(static_cast<size_t>(entry.Size));
				if (LZ4_decompress_safe(reinterpret_cast<const char*>(stored.data()), reinterpret_cast<char*>(bytes.data()),
					static_cast<int>(stored.size()), static_cast<int>(bytes.size())) != static_cast<int>(bytes.size()))
					throw std::runtime_error("Invalid compressed bundle payload");
			}
			if (Checksum(bytes) != entry.Checksum) throw std::runtime_error("Bundle payload checksum mismatch");
			return true;
		} catch (const std::exception& ex) {
			error = ex.what();
			bytes.clear();
			return false;
		}
	}

	bool AssetBundle::Contains(const std::string& relativePath) const { return m_Entries.contains(Key(relativePath)); }
	std::vector<std::string> AssetBundle::Files() const {
		std::vector<std::string> paths;
		paths.reserve(m_Entries.size());
		for (const auto& [key, entry] : m_Entries) paths.push_back(entry.Path);
		std::sort(paths.begin(), paths.end());
		return paths;
	}

	bool AssetBundle::MountProject(const std::filesystem::path& root, std::string& error) {
		std::vector<MountedBundle> mounts;
		error.clear();
		try {
			for (const char* folder : { "Assets", "FunkeAssets" }) {
				const auto archive = root / (std::string(folder) + ".fbundle");
				if (!std::filesystem::exists(archive)) continue;
				auto bundle = std::make_shared<AssetBundle>();
				if (!bundle->Open(archive, error)) throw std::runtime_error(Path::ToUtf8(archive) + ": " + error);
				mounts.push_back({ AbsoluteKey(root / folder) + "/", Path::CanonicalIdentity(root / folder), std::move(bundle) });
			}
			std::lock_guard lock(g_MountMutex);
			g_Mounts = std::move(mounts);
			return true;
		} catch (const std::exception& ex) {
			error = ex.what();
			UnmountAll();
			return false;
		}
	}
	void AssetBundle::UnmountAll() { std::lock_guard lock(g_MountMutex); g_Mounts.clear(); }
	bool AssetBundle::ContainsMounted(const std::filesystem::path& path) { return Find(path).first != nullptr; }
	bool AssetBundle::IsMountedDirectory(const std::filesystem::path& path) {
		const auto mounts = Mounts();
		if (mounts.empty() || path.empty()) return false;
		const auto key = AbsoluteKey(path) + "/";
		for (const auto& mount : mounts) {
			if (key == mount.Root) return true;
			if (!key.starts_with(mount.Root)) continue;
			const auto prefix = key.substr(mount.Root.size());
			for (const auto& [name, entry] : mount.Bundle->m_Entries) if (name.starts_with(prefix)) return true;
		}
		return false;
	}
	std::vector<std::filesystem::path> AssetBundle::ListMounted(const std::filesystem::path& directory, bool recursive) {
		const auto mounts = Mounts();
		if (mounts.empty() || directory.empty()) return {};
		const auto prefix = AbsoluteKey(directory) + "/";
		std::vector<std::filesystem::path> files;
		for (const auto& mount : mounts) {
			for (const auto& [name, entry] : mount.Bundle->m_Entries) {
				const auto full = mount.Root + name;
				if (full.starts_with(prefix) && (recursive || full.find('/', prefix.size()) == std::string::npos))
					files.push_back(mount.NativeRoot / Path::FromUtf8(entry.Path));
			}
		}
		std::sort(files.begin(), files.end());
		return files;
	}
	bool AssetBundle::ReadMounted(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes) {
		const auto [bundle, relative] = Find(path);
		if (!bundle) return false;
		std::string error;
		if (!bundle->Read(relative, bytes, error)) FNK_CORE_ERROR_TAG("Assets", "Bundle read failed for '{}': {}", Path::ToUtf8(path), error);
		return true;
	}
	bool AssetBundle::GetMountedStamp(const std::filesystem::path& path, std::uintmax_t& size,
		std::filesystem::file_time_type& writeTime) {
		const auto [bundle, relative] = Find(path);
		if (!bundle) return false;
		size = bundle->m_Entries.at(Key(relative)).Size;
		writeTime = bundle->m_WriteTime;
		return true;
	}
}
