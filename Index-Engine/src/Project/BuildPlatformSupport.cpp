#include "pch.hpp"
#include "Project/BuildPlatformSupport.hpp"

#include "Project/PlatformPackages.hpp"

namespace Index {

	std::optional<BuildPlatform> BuildPlatformSupport::HostDesktopPlatform() {
#if defined(IDX_PLATFORM_WINDOWS)
		return BuildPlatform::Windows;
#elif defined(IDX_PLATFORM_LINUX)
		return BuildPlatform::Linux;
#elif defined(IDX_PLATFORM_MACOS)
		return BuildPlatform::MacOS;
#else
		return std::nullopt;
#endif
	}

	bool BuildPlatformSupport::IsHostDesktop(BuildPlatform platform) {
		const std::optional<BuildPlatform> host = HostDesktopPlatform();
		return host && *host == platform;
	}

	bool BuildPlatformSupport::IsAvailable(BuildPlatform platform) {
		if (IsHostDesktop(platform)) return true;
		const std::optional<PlatformPackageInfo> package = PlatformPackages::FindForPlatform(platform);
		return package && package->SupportsHost(PlatformPackages::CurrentHostName());
	}

	std::optional<PlatformPackageInfo> BuildPlatformSupport::GetPackage(BuildPlatform platform) {
		if (IsHostDesktop(platform)) return std::nullopt;
		return PlatformPackages::FindForPlatform(platform);
	}

	std::string BuildPlatformSupport::UnavailableReason(BuildPlatform platform) {
		if (IsAvailable(platform)) return {};
		const std::string displayName = IndexBuildProfile::PlatformDisplayName(platform);
		if (const std::optional<PlatformPackageInfo> package = PlatformPackages::FindForPlatform(platform)) {
			return package->Name + " is installed but cannot build on this computer (supported hosts: "
				+ package->Hosts + ").";
		}
		return displayName + " platform support is not installed. Install the Index.Platform."
			+ IndexBuildProfile::PlatformToString(platform) + " package (Package Manager > Platforms).";
	}

} // namespace Index
