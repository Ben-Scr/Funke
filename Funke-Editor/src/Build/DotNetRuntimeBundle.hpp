#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace Funke::DotNetRuntimeBundle {
	struct Runtime {
		std::string Version;
		std::filesystem::path Root;
		std::filesystem::path FrameworkDirectory;
	};

	bool SelectRuntime(std::string_view installedRuntimes, std::string_view requiredVersion,
		Runtime& runtime, std::string& error);
	bool Stage(const Runtime& runtime, const std::filesystem::path& destination, std::string& error);
	bool StageInstalled(const std::filesystem::path& runtimeConfig,
		const std::filesystem::path& destination, std::string& error);
}
