#pragma once
#include <string>

namespace Funke {
	// Tooling a game creator needs on this machine. Probed off the UI thread at launcher start.
	struct PrerequisiteStatus {
		bool Checked = false;
		bool DotNetSdk = false;     // Stable .NET 10.0 SDK matching generated global.json.
		std::string DotNetSdkVersion;
		bool MsvcBuildTools = false; // MSVC v143: only for native C++ scripts and packages
		bool NativeDevelopment = false;
	};

	PrerequisiteStatus ProbePrerequisites();

	inline constexpr const char* k_DotNetSdkDownloadUrl = "https://dotnet.microsoft.com/download/dotnet/10.0";
	inline constexpr const char* k_MsvcBuildToolsDownloadUrl = "https://visualstudio.microsoft.com/visual-cpp-build-tools/";
}
