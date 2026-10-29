#include "Systems/Prerequisites.hpp"

#include "Project/ManagedSdk.hpp"
#include "Project/EngineSdk.hpp"
#include "Project/FunkeProject.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Process.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>

namespace Funke {
	namespace {
		// `dotnet --list-sdks` prints one "10.0.100 [C:\Program Files\dotnet\sdk]" line per SDK.
		bool FindDotNetSdk(std::string& outVersion) {
			const Process::Result result = Process::Run({ "dotnet", "--list-sdks" }, {}, std::chrono::seconds(15));
			if (!result.Succeeded()) return false;

			return ManagedSdk::FindCompatibleSdk(result.Output, outVersion);
		}

		bool FindMsvcBuildTools() {
#ifdef FNK_PLATFORM_WINDOWS
			const char* programFilesX86 = std::getenv("ProgramFiles(x86)");
			if (!programFilesX86) return false;
			const std::filesystem::path vswhere = Path::FromUtf8(programFilesX86)
				/ "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
			std::error_code ec;
			if (!std::filesystem::exists(vswhere, ec)) return false;

			const Process::Result result = Process::Run({
				Path::ToUtf8(vswhere), "-products", "*", "-latest",
				"-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
				"-property", "installationPath" }, {}, std::chrono::seconds(15));
			return result.Succeeded() && result.Output.find_first_not_of(" \r\n\t") != std::string::npos;
#else
			return true; // native toolchains are the system compiler outside Windows
#endif
		}
	}

	PrerequisiteStatus ProbePrerequisites() {
		PrerequisiteStatus status;
		try {
			status.DotNetSdk = FindDotNetSdk(status.DotNetSdkVersion);
			status.NativeDevelopment = FunkeProject::GetEngineInstallation().NativeDevelopment;
			status.MsvcBuildTools = status.NativeDevelopment && FindMsvcBuildTools();
		}
		catch (...) {
			// A failed probe must never block the launcher; it only hides the hint.
			status.DotNetSdk = true;
			status.MsvcBuildTools = true;
		}
		status.Checked = true;
		return status;
	}
}
