#pragma once
#include "Core/Export.hpp"
#include "Serialization/SpecialFolder.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace Funke {
	class FUNKE_API Path {
	public:
		static std::string GetSpecialFolderPath(SpecialFolder folder);
		// Resolves existing ancestors while retaining virtual or deleted descendants.
		static std::filesystem::path CanonicalIdentity(const std::filesystem::path& path);
		static std::filesystem::path FromUtf8(std::string_view path) {
			return std::filesystem::u8path(path.begin(), path.end());
		}
		static std::string ToUtf8(const std::filesystem::path& path) {
			const auto utf8 = path.u8string();
			return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
		}
		static std::string ToGenericUtf8(const std::filesystem::path& path) {
			const auto utf8 = path.generic_u8string();
			return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
		}

		template <typename... Args>
		static std::string Combine(Args&&... args) {
			std::filesystem::path combined;
			((combined /= ToFilesystemPath(std::forward<Args>(args))), ...);
			return ToUtf8(combined.make_preferred());
		}

		static std::string Current() {
			return ToUtf8(std::filesystem::current_path());
		}

		// Content base: executable directory, macOS bundle Resources, or a platform override.
		static std::string ExecutableDir();
		static void SetExecutableDirOverride(const std::string& directory);

		// Relative to the content base or its parent (dev layout); empty when absent.
		static std::string ResolveFunkeAssets(const std::string& subdirectory);

		/// Converts an absolute path to a display-friendly relative string (project root, FunkeAssets, or filename-only). Always uses forward slashes.
		static std::string ToProjectRelativeDisplay(const std::string& absolutePath);

	private:
		static std::filesystem::path ToFilesystemPath(const std::filesystem::path& path) { return path; }
		static std::filesystem::path ToFilesystemPath(std::filesystem::path&& path) { return std::move(path); }
		static std::filesystem::path ToFilesystemPath(const std::string& path) { return FromUtf8(path); }
		static std::filesystem::path ToFilesystemPath(std::string_view path) { return FromUtf8(path); }
		static std::filesystem::path ToFilesystemPath(const char* path) { return FromUtf8(path ? std::string_view(path) : std::string_view()); }
	};
}
