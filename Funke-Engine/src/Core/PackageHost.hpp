#pragma once

#include "Core/Export.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Funke {
	struct FunkeProject;

	struct PackageManifest;
	struct PackageResolution;

	struct LoadedPackage {
		std::string Name;          // Without the "Pkg." prefix or ".Native" suffix, e.g. "Funke.Hello".
		std::string Version;       // From funke-package.lua; empty when the package shipped without one.
		std::string ModulePath;    // Absolute path to the loaded shared library.
		void*       ModuleHandle = nullptr; // HMODULE on Windows; void* dlopen handle on POSIX.
		uint32_t    OwnerId = 0;            // Tags this package's ComponentRegistry / entity-preset entries.
	};

	class FUNKE_API PackageHost {
	public:
		// Scan and load all packages reachable from the current executable.
		// Idempotent — calling twice is a no-op.
		static void LoadAll();

		static size_t LoadInstalled();

		// Unload all packages in reverse order. Safe to call even if LoadAll() never ran.
		static void UnloadAll();

		static const std::vector<LoadedPackage>& GetLoaded();

		static bool IsPackageLoaded(const std::string& packageName);

		// Active project's source packages under <project>/Packages/ with no native build loadable by this engine
		// (never built, or built against another Funke-Engine binary). Rebuilding the package workspace fixes them.
		static std::vector<std::string> FindStaleProjectNativePackages();
	};

	namespace PackageHostDetail {
		FUNKE_API void* LoadNativeModule(const std::filesystem::path& path);
		FUNKE_API void UnloadNativeModule(void* module);
		FUNKE_API std::filesystem::path GetNativePackageAbiMetadataPath(
			const std::filesystem::path& packageLibrary);
		FUNKE_API bool ValidateNativePackageCompatibility(
			const std::filesystem::path& packageLibrary,
			std::string_view runningEngineSha256,
			std::string& outReason);
		FUNKE_API void AppendProjectNativeCandidates(const FunkeProject& project,
			std::string_view configuration,
			std::vector<std::filesystem::path>& outCandidates);
		FUNKE_API void AppendProjectNativeCandidates(const FunkeProject& project,
			std::string_view configuration,
			const std::vector<std::string>& packageNames,
			std::vector<std::filesystem::path>& outCandidates);

		// Manifests under <engine root>/packages/ and <project>/Packages/; unreadable ones are logged and skipped.
		FUNKE_API std::vector<PackageManifest> DiscoverPackageManifests(const FunkeProject& project);
		// Native load order: resolved packages dependencies-first, then listed packages that have no manifest
		// (shipped games stage only the DLLs) in project.json order. Blocked packages are left out.
		FUNKE_API std::vector<std::string> BuildNativeLoadOrder(const PackageResolution& resolution,
			const std::vector<std::string>& requested, const std::vector<PackageManifest>& manifests);
		// An empty runningEngineSha256 only reports packages without any native build.
		FUNKE_API std::vector<std::string> FindStaleProjectNativePackages(const FunkeProject& project,
			std::string_view configuration, std::string_view runningEngineSha256);
	}

}
