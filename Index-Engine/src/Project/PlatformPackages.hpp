#pragma once
#include "Core/Export.hpp"
#include "Project/IndexBuildProfile.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Index {

	// A platform package adds an export target (Android, iOS, Web, ...) to the editor without the
	// engine knowing anything about that platform. It is a folder with an index-package.lua of
	// kind "platform" and a builder script speaking the Index Platform Builder protocol
	// (scripts/platform/index_platform_sdk/protocol.py).
	//
	// Search roots, first match per platform wins:
	//   1. INDEX_PLATFORM_PACKAGES (path list), for package development
	//   2. <user data>/Index/Platforms/<Name>/  (what the Package Manager installs)
	//   3. <engine root>/platforms/<Name>/      (first-party packages in an engine checkout)
	struct PlatformPackageInfo {
		std::string Name;
		std::string Version;
		std::string Kind;          // "platform" or "build-component"
		std::string Description;
		std::optional<BuildPlatform> Platform;  // set for kind "platform"
		std::filesystem::path Directory;
		std::filesystem::path Builder;          // absolute path to the builder script
		int Protocol = 0;
		// Editor host OSes the builder runs on ("windows,macos,linux"); empty means all.
		std::string Hosts;

		bool SupportsHost(std::string_view host) const {
			return Hosts.empty() || Hosts.find(host) != std::string::npos;
		}
	};

	namespace PlatformPackages {

		constexpr int k_SupportedProtocol = 1;

		INDEX_API std::vector<std::filesystem::path> GetSearchRoots();
		INDEX_API std::filesystem::path GetUserInstallRoot();

		// Cached; refresh=true rescans after an install or removal.
		INDEX_API const std::vector<PlatformPackageInfo>& Discover(bool refresh = false);
		INDEX_API std::optional<PlatformPackageInfo> FindForPlatform(BuildPlatform platform);
		INDEX_API std::optional<PlatformPackageInfo> FindByName(const std::string& name);

		// Parses the string fields of an index-package.lua; nullopt when it is not a usable manifest.
		INDEX_API std::optional<PlatformPackageInfo> ReadManifest(const std::filesystem::path& packageDirectory);

		// "windows" / "macos" / "linux" for the running editor.
		INDEX_API const char* CurrentHostName();

	}

}
