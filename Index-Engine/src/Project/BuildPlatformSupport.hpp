#pragma once
#include "Core/Export.hpp"
#include "Project/IndexBuildProfile.hpp"
#include "Project/PlatformPackages.hpp"

#include <optional>
#include <string>

namespace Index {

	// The editor's own desktop platform builds natively; every other target comes from an installed
	// platform package (see PlatformPackages).
	struct INDEX_API BuildPlatformSupport {
		static bool IsAvailable(BuildPlatform platform);

		// The desktop platform the editor itself runs on. Only its profiles may change the project's
		// render backend, which the editor uses for its own device too.
		static bool IsHostDesktop(BuildPlatform platform);
		// Empty on hosts without a desktop export (mobile, Web).
		static std::optional<BuildPlatform> HostDesktopPlatform();

		// The package that builds this platform; empty for the host desktop platform.
		static std::optional<PlatformPackageInfo> GetPackage(BuildPlatform platform);

		// Human-readable explanation for the warning row. Empty when the
		// platform is available.
		static std::string UnavailableReason(BuildPlatform platform);
	};

} // namespace Index
