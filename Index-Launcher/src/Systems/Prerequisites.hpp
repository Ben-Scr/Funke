#pragma once
#include <string>

namespace Index {
	// Tooling a game creator needs on this machine. Probed off the UI thread at launcher start.
	struct PrerequisiteStatus {
		bool Checked = false;
		bool DotNetSdk = false;     // .NET 10+ SDK: required for C# scripts
		std::string DotNetSdkVersion;
		bool MsvcBuildTools = false; // MSVC v143: only for native C++ scripts and packages
	};

	PrerequisiteStatus ProbePrerequisites();

	inline constexpr const char* k_DotNetSdkDownloadUrl = "https://dotnet.microsoft.com/download/dotnet/10.0";
	inline constexpr const char* k_MsvcBuildToolsDownloadUrl = "https://visualstudio.microsoft.com/visual-cpp-build-tools/";
}
