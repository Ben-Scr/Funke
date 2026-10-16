#include <pch.hpp>
#include "Utils/PackageToolPath.hpp"

#include "Serialization/Path.hpp"

#include <system_error>
#include <vector>

namespace Funke::PackageTool {

	std::string ExecutableName() {
#if defined(FNK_PLATFORM_WINDOWS)
		return "Funke-PackageTool.exe";
#else
		return "Funke-PackageTool";
#endif
	}

	std::filesystem::path ResolveExecutable() {
		const std::filesystem::path exeDir(Path::ExecutableDir());

		std::vector<std::filesystem::path> candidates;
		candidates.push_back(exeDir / ExecutableName());
		candidates.push_back(exeDir / "Funke-PackageTool.dll");
		const std::filesystem::path siblingOutput = exeDir.parent_path() / "Funke-PackageTool";
		candidates.push_back(siblingOutput / ExecutableName());
		candidates.push_back(siblingOutput / "Funke-PackageTool.dll");

		const std::filesystem::path projectDir = exeDir / ".." / ".." / ".." / "Funke-PackageTool";
		for (const char* configuration : { "Debug", "Release", "Dist" }) {
			const std::filesystem::path outputDirectory = projectDir / "bin" / configuration / "net10.0";
			candidates.push_back(outputDirectory / ExecutableName());
			candidates.push_back(outputDirectory / "Funke-PackageTool.dll");
		}

		for (const auto& candidate : candidates) {
			std::error_code ec;
			if (std::filesystem::exists(candidate, ec) && !ec) {
				return std::filesystem::canonical(candidate, ec);
			}
		}
		return {};
	}

} // namespace Funke::PackageTool
