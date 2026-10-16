#pragma once
#include "Project/PlatformPackages.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Funke::PlatformBuilder {

	// Runs a platform package's builder (Funke Platform Builder protocol v1, see
	// scripts/platform/funke_platform_sdk/protocol.py). The editor stages the platform-neutral content;
	// the package compiles the engine for its target and packages the artifact.

	struct BuildRequest {
		std::filesystem::path EngineRoot;
		std::filesystem::path ProjectRoot;
		std::string ProjectName;
		std::filesystem::path ContentDir;
		std::filesystem::path OutputDir;
		std::string Configuration;   // "Development" | "Release"
		std::string AppName;
		std::string AppId;
		std::string Version;
		std::string Company;
		std::filesystem::path IconPath;
		int EntityBits = 20;
		// project.json `modules`; the package compiles the engine without the others.
		std::vector<std::string> Modules;
		std::vector<std::pair<std::string, std::string>> Options;
	};

	struct BuildResult {
		bool Succeeded = false;
		std::filesystem::path Artifact;
		std::string Error;
	};

	struct CheckIssue {
		std::string Severity;  // "error" | "warn"
		std::string Message;
		std::string Fix;
	};

	struct CheckResult {
		bool Ran = false;      // false: Python or the builder could not be started
		bool Ok = false;
		std::vector<CheckIssue> Issues;
		std::string Error;
	};

	// The builder's `describe` answer: which platformOptions it understands.
	struct Description {
		bool Ran = false;
		std::string DisplayName;
		std::vector<std::pair<std::string, std::string>> Options;  // name -> help text
		std::string Error;
	};

	using ProgressFn = std::function<void(float progress, std::string_view stage)>;

	// Python 3.10+ interpreter command ("py -3" / "python3" / "python"); empty when none is installed.
	std::vector<std::string> FindPython();

	// Blocking; call from a worker thread.
	CheckResult Check(const PlatformPackageInfo& package);
	Description Describe(const PlatformPackageInfo& package);
	BuildResult Build(const PlatformPackageInfo& package, const BuildRequest& request, const ProgressFn& onProgress);

}
