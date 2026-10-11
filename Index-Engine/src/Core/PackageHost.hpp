#pragma once

#include "Core/Export.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Index {
	struct IndexProject;

	struct LoadedPackage {
		std::string Name;          // Without the "Pkg." prefix or ".Native" suffix, e.g. "Index.Hello".
		std::string ModulePath;    // Absolute path to the loaded shared library.
		void*       ModuleHandle = nullptr; // HMODULE on Windows; void* dlopen handle on POSIX.
	};

	class INDEX_API PackageHost {
	public:
		// Scan and load all packages reachable from the current executable.
		// Idempotent — calling twice is a no-op.
		static void LoadAll();

		static size_t LoadInstalled();

		// Unload all packages in reverse order. Safe to call even if LoadAll() never ran.
		static void UnloadAll();

		static const std::vector<LoadedPackage>& GetLoaded();

		static bool IsPackageLoaded(const std::string& packageName);
	};

	namespace PackageHostDetail {
		INDEX_API void* LoadNativeModule(const std::filesystem::path& path);
		INDEX_API void UnloadNativeModule(void* module);
		INDEX_API std::filesystem::path GetNativePackageAbiMetadataPath(
			const std::filesystem::path& packageLibrary);
		INDEX_API bool ValidateNativePackageCompatibility(
			const std::filesystem::path& packageLibrary,
			std::string_view runningEngineSha256,
			std::string& outReason);
		INDEX_API void AppendProjectNativeCandidates(const IndexProject& project,
			std::string_view configuration,
			std::vector<std::filesystem::path>& outCandidates);
	}

}
