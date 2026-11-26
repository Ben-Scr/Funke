#pragma once

#include "Project/FunkeProject.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace Funke::ProjectInternal {
	bool IsValidSceneName(std::string_view value);
	void ResolvePaths(FunkeProject& project);
	void ReportCreateProgress(const FunkeProject::CreateProgressCallback& callback, float progress, std::string_view stage);
	std::filesystem::path ResolveEngineRoot();
	std::string GetSharedLibraryFilename(const std::string& targetName);
	std::string GetNativeBuildPlatformDirectory();
	std::string ToLowerAscii(std::string value);
	bool ConfigurePackages(const FunkeProject& project, std::string* outError);
	bool EnsureManagedSdkGlobalJson(const FunkeProject& project, std::string* outError);
	bool EnsureManagedSolutionHeader(const FunkeProject& project, std::string* outError);
	Json::Value BuildProjectJson(const FunkeProject& project);
}
