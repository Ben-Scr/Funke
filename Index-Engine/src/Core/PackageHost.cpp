#include "pch.hpp"
#include "Core/PackageHost.hpp"

#include "Core/EngineModules.hpp"
#include "Core/Log.hpp"
#include "Core/Platform.hpp"
#include "Packages/PackageManifest.hpp"
#include "Packages/PackageResolver.hpp"
#include "Project/IndexProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/Path.hpp"
#include "Utils/Hash.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

#ifdef IDX_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Index {

	namespace {
		std::vector<LoadedPackage> s_LoadedPackages;
		bool s_LoadAllRan = false;
		uint32_t s_NextOwnerId = 1;
		constexpr std::string_view k_NativeAbiSchema = "1";
		constexpr std::string_view k_EngineCppBoundary = "engine_cpp";
		constexpr std::string_view k_StandaloneCBoundary = "standalone_c";

		std::string TrimAscii(std::string_view value) {
			auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
			while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
				value.remove_prefix(1);
			}
			while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
				value.remove_suffix(1);
			}
			return std::string(value);
		}

		bool IsSha256(std::string_view value) {
			if (value.size() != 64) return false;
			return std::all_of(value.begin(), value.end(), [](unsigned char c) {
				return std::isxdigit(c) != 0;
			});
		}

#ifdef IDX_PLATFORM_WINDOWS
		bool GetRunningEngineSha256(std::string& outSha256, std::string& outError) {
			struct CachedFingerprint {
				std::string Sha256;
				std::string Error;
			};

			static const CachedFingerprint fingerprint = [] {
				CachedFingerprint result;
				const HMODULE engineModule = ::GetModuleHandleW(L"Index-Engine.dll");
				if (!engineModule) {
					result.Error = "running Index-Engine.dll module was not found";
					return result;
				}

				std::vector<wchar_t> pathBuffer(32768);
				const DWORD pathLength = ::GetModuleFileNameW(
					engineModule, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
				if (pathLength == 0 || pathLength >= pathBuffer.size()) {
					result.Error = "running Index-Engine.dll path could not be resolved";
					return result;
				}

				Hash::Sha256OfFile(
					std::filesystem::path(pathBuffer.data(), pathBuffer.data() + pathLength),
					result.Sha256, result.Error);
				return result;
			}();

			outSha256 = fingerprint.Sha256;
			outError = fingerprint.Error;
			return !outSha256.empty();
		}
#endif

		void* PlatformResolve(void* module, const char* symbol) {
#ifdef IDX_PLATFORM_WINDOWS
			return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(module), symbol));
#else
			return Platform::GetSharedLibrarySymbol(module, symbol);
#endif
		}

		void PlatformUnload(void* module) {
#ifdef IDX_PLATFORM_WINDOWS
			FreeLibrary(static_cast<HMODULE>(module));
#else
			Platform::UnloadSharedLibrary(module);
#endif
		}

#ifdef IDX_PLATFORM_WINDOWS
		// A bare native path keeps SEH valid while containing stale-ABI faults from DllMain/static initialization.
		void* LoadLibraryGuarded(const wchar_t* path) {
			__try {
				return reinterpret_cast<void*>(::LoadLibraryW(path));
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				return nullptr;
			}
		}
#else
		void* LoadLibraryGuarded(const std::filesystem::path& path) {
			std::string error;
			void* module = Platform::LoadSharedLibrary(path, &error);
			if (!module) IDX_CORE_WARN_TAG("PackageHost", "Loading '{}' failed: {}", path.string(), error);
			return module;
		}
#endif

		std::string PathToUtf8(const std::filesystem::path& path) {
			const auto encoded = path.u8string();
			return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
		}

		// Same SEH rationale as LoadLibraryGuarded: OnLoad calls back into the engine and a stale ABI would crash the editor on every launch. *outCrashed distinguishes a trapped fault from a clean OnLoad return of 0.
		int InvokeOnLoadGuarded(void* moduleHandle, bool* outCrashed, bool* outHadExport) {
			*outCrashed = false;
			*outHadExport = false;
			using OnLoadFn = int (*)();
#ifdef IDX_PLATFORM_WINDOWS
			OnLoadFn fn = reinterpret_cast<OnLoadFn>(
				::GetProcAddress(static_cast<HMODULE>(moduleHandle), "IndexPackage_OnLoad"));
			if (!fn) return 0;
			*outHadExport = true;
			__try {
				return fn();
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				*outCrashed = true;
				return 0;
			}
#else
			OnLoadFn fn = reinterpret_cast<OnLoadFn>(PlatformResolve(moduleHandle, "IndexPackage_OnLoad"));
			if (!fn) return 0;
			*outHadExport = true;
			return fn();
#endif
		}

		void InvokeOnUnloadGuarded(void* moduleHandle, bool* outCrashed) {
			*outCrashed = false;
			using OnUnloadFn = void (*)();
#ifdef IDX_PLATFORM_WINDOWS
			OnUnloadFn fn = reinterpret_cast<OnUnloadFn>(
				::GetProcAddress(static_cast<HMODULE>(moduleHandle), "IndexPackage_OnUnload"));
			if (!fn) return;
			__try {
				fn();
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				*outCrashed = true;
			}
#else
			OnUnloadFn fn = reinterpret_cast<OnUnloadFn>(PlatformResolve(moduleHandle, "IndexPackage_OnUnload"));
			if (fn) fn();
#endif
		}

		std::string PackageNameFromFilename(const std::string& filename) {
			// "Pkg.Index.Hello.Native.dll"  ->  "Index.Hello"
			std::string stem = filename;
			const auto dot = stem.find_last_of('.');
			if (dot != std::string::npos) {
				stem = stem.substr(0, dot);
			}
			constexpr std::string_view k_Prefix = "Pkg.";
			constexpr std::string_view k_Suffix = ".Native";
			if (stem.rfind(k_Prefix, 0) == 0) {
				stem = stem.substr(k_Prefix.size());
			}
			if (stem.size() > k_Suffix.size() &&
				stem.compare(stem.size() - k_Suffix.size(), k_Suffix.size(), k_Suffix) == 0) {
				stem = stem.substr(0, stem.size() - k_Suffix.size());
			}
			return stem;
		}

		// Loading managed assemblies here would pin them against C# hot reload.
		bool LooksLikeNativePackageFolder(const std::string& folderName) {
			if (folderName.rfind("Pkg.", 0) != 0) return false;
			constexpr std::string_view k_NativeSuffix = ".Native";
			return folderName.size() > k_NativeSuffix.size()
				&& folderName.compare(folderName.size() - k_NativeSuffix.size(),
					k_NativeSuffix.size(), k_NativeSuffix) == 0;
		}

		void DiscoverIn(const std::filesystem::path& root, std::vector<std::filesystem::path>& outCandidates) {
			std::error_code ec;
			if (!std::filesystem::is_directory(root, ec) || ec) {
				return;
			}

			for (const auto& entry : std::filesystem::directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec)) {
				if (ec) break;

				const auto& entryPath = entry.path();
				const std::string name = PathToUtf8(entryPath.filename());

				if (entry.is_directory(ec) && !ec) {
					if (LooksLikeNativePackageFolder(name)) {
						const std::filesystem::path candidate = entryPath / Platform::SharedLibraryFileName(name);
						if (std::filesystem::exists(candidate, ec) && !ec) {
							outCandidates.push_back(candidate);
						}
					}
				}
			}
		}
	}

	namespace PackageHostDetail {
		void* LoadNativeModule(const std::filesystem::path& path) {
#ifdef IDX_PLATFORM_WINDOWS
			return LoadLibraryGuarded(path.c_str());
#else
			return LoadLibraryGuarded(path);
#endif
		}

		void UnloadNativeModule(void* module) {
			if (module) PlatformUnload(module);
		}

		std::filesystem::path GetNativePackageAbiMetadataPath(
			const std::filesystem::path& packageLibrary) {
			std::filesystem::path metadataPath = packageLibrary;
			metadataPath += ".indexabi";
			return metadataPath;
		}

		bool ValidateNativePackageCompatibility(
			const std::filesystem::path& packageLibrary,
			std::string_view runningEngineSha256,
			std::string& outReason) {
			outReason.clear();
			const std::filesystem::path metadataPath =
				GetNativePackageAbiMetadataPath(packageLibrary);
			std::ifstream metadata(metadataPath, std::ios::binary);
			if (!metadata.is_open()) {
				outReason = "missing " + PathToUtf8(metadataPath.filename());
				return false;
			}

			std::unordered_map<std::string, std::string> values;
			std::string line;
			while (std::getline(metadata, line)) {
				if (line.empty()) continue;
				const size_t separator = line.find('=');
				if (separator == std::string::npos) {
					outReason = "malformed ABI metadata line";
					return false;
				}
				const std::string key = TrimAscii(std::string_view(line).substr(0, separator));
				const std::string value = TrimAscii(std::string_view(line).substr(separator + 1));
				if (key.empty() || !values.emplace(key, value).second) {
					outReason = "malformed or duplicate ABI metadata key";
					return false;
				}
			}

			if (values["schema"] != k_NativeAbiSchema) {
				outReason = "unsupported native package ABI metadata schema";
				return false;
			}

			const std::string& boundary = values["boundary"];
			if (boundary == k_StandaloneCBoundary) return true;
			if (boundary != k_EngineCppBoundary) {
				outReason = "unknown native package ABI boundary";
				return false;
			}

			const std::string packageEngineSha256 = Hash::ToLowerHex(values["engine_sha256"]);
			if (!IsSha256(packageEngineSha256)) {
				outReason = "missing or invalid engine_sha256";
				return false;
			}
			if (!IsSha256(runningEngineSha256)) {
				outReason = "running engine fingerprint is unavailable";
				return false;
			}
			if (packageEngineSha256 != Hash::ToLowerHex(runningEngineSha256)) {
				outReason = "package was built against a different Index-Engine binary";
				return false;
			}
			return true;
		}

		void AppendProjectNativeCandidates(const IndexProject& project,
			std::string_view configuration,
			std::vector<std::filesystem::path>& outCandidates) {
			AppendProjectNativeCandidates(project, configuration, project.Packages, outCandidates);
		}

		std::vector<PackageManifest> DiscoverPackageManifests(const IndexProject& project) {
			std::vector<PackageManifest> manifests;
			auto scan = [&](const std::filesystem::path& root) {
				std::error_code ec;
				if (root.empty() || !std::filesystem::is_directory(root, ec)) return;
				std::vector<std::filesystem::path> folders;
				for (const auto& entry : std::filesystem::directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec)) {
					if (entry.is_directory(ec) && std::filesystem::exists(entry.path() / std::string(PackageManifest::k_FileName), ec)) {
						folders.push_back(entry.path());
					}
				}
				std::sort(folders.begin(), folders.end());
				for (const auto& folder : folders) {
					PackageManifest manifest;
					std::string error;
					if (PackageManifest::TryLoad(PathToUtf8(folder), manifest, &error)) {
						manifests.push_back(std::move(manifest));
					}
					else {
						IDX_CORE_WARN_TAG("PackageHost", "Ignoring unreadable package manifest: {}", error);
					}
				}
			};
			const std::string engineRoot = IndexProject::GetEngineRootDir();
			if (!engineRoot.empty()) scan(std::filesystem::path(Path::FromUtf8(engineRoot)) / "packages");
			if (!project.PackagesDirectory.empty()) scan(Path::FromUtf8(project.PackagesDirectory));
			return manifests;
		}

		std::vector<std::string> FindStaleProjectNativePackages(const IndexProject& project,
			std::string_view configuration, std::string_view runningEngineSha256) {
			std::vector<std::string> stale;
			if (project.Packages.empty() || project.PackagesDirectory.empty()) return stale;

			const std::vector<PackageManifest> manifests = DiscoverPackageManifests(project);
			const PackageResolution resolution =
				ResolvePackages(manifests, project.Packages, GetEngineCompatibilityVersion());
			const std::filesystem::path projectPackages = Path::FromUtf8(project.PackagesDirectory);
			for (const std::string& name : resolution.LoadOrder) {
				const auto manifest = std::find_if(manifests.begin(), manifests.end(),
					[&](const PackageManifest& candidate) { return candidate.Name == name; });
				if (manifest == manifests.end()
					|| !(manifest->HasNativeLayer || manifest->HasNativeStandaloneLayer)) {
					continue;
				}
				// Engine packages are built by the engine solution; only project sources rebuild per project.
				std::error_code ec;
				if (!std::filesystem::equivalent(
						Path::FromUtf8(manifest->PackageDir).parent_path(), projectPackages, ec) || ec) {
					continue;
				}

				std::vector<std::filesystem::path> candidates;
				AppendProjectNativeCandidates(project, configuration, { name }, candidates);
				const bool loadable = std::any_of(candidates.begin(), candidates.end(),
					[&](const std::filesystem::path& candidate) {
						if (runningEngineSha256.empty()) return true;
						std::string reason;
						return ValidateNativePackageCompatibility(candidate, runningEngineSha256, reason);
					});
				if (!loadable) stale.push_back(name);
			}
			return stale;
		}

		std::vector<std::string> BuildNativeLoadOrder(const PackageResolution& resolution,
			const std::vector<std::string>& requested, const std::vector<PackageManifest>& manifests) {
			std::vector<std::string> order = resolution.LoadOrder;
			for (const std::string& name : requested) {
				const bool hasManifest = std::any_of(manifests.begin(), manifests.end(),
					[&](const PackageManifest& manifest) { return manifest.Name == name; });
				if (!hasManifest && std::find(order.begin(), order.end(), name) == order.end()) {
					order.push_back(name);
				}
			}
			return order;
		}

		void AppendProjectNativeCandidates(const IndexProject& project,
			std::string_view configuration,
			const std::vector<std::string>& packageNames,
			std::vector<std::filesystem::path>& outCandidates) {
			const std::filesystem::path projectPackages(project.PackagesDirectory);
			for (const std::string& packageName : packageNames) {
				const std::filesystem::path workspaceCandidate =
					IndexProject::GetProjectNativePackageLibraryPath(
						project.RootDirectory, packageName, std::string(configuration));
				std::error_code ec;
				if (!workspaceCandidate.empty()
					&& std::filesystem::exists(workspaceCandidate, ec) && !ec) {
					outCandidates.push_back(workspaceCandidate);
				}

				// Prebuilt binaries ship per platform: Bin/Windows-x64, Bin/macOS-arm64, Bin/Linux-x64, ...
				const std::filesystem::path prebuiltCandidate = projectPackages
					/ packageName
					/ "Bin" / (std::string(IDX_PLATFORM_NAME) + "-" + IDX_ARCH_NAME)
					/ Platform::SharedLibraryFileName("Pkg." + packageName + ".Native");
				ec.clear();
				if (std::filesystem::exists(prebuiltCandidate, ec) && !ec) {
					outCandidates.push_back(prebuiltCandidate);
				}
			}
		}
	}

	namespace {
		void LogResolutionIssues(const PackageResolution& resolution, const std::vector<std::string>& requested,
			const std::vector<PackageManifest>& manifests) {
			for (const PackageIssue& issue : resolution.Issues) {
				const bool hasManifest = std::any_of(manifests.begin(), manifests.end(),
					[&](const PackageManifest& manifest) { return manifest.Name == issue.Package; });
				const bool isRequested = std::find(requested.begin(), requested.end(), issue.Package) != requested.end();
				// Shipped games stage DLLs without manifests; those load in project order, not as errors.
				if (!hasManifest && isRequested) continue;
				if (issue.Level == PackageIssue::Severity::Error) {
					IDX_CORE_ERROR_TAG("PackageHost", "Package '{}' {}", issue.Package, issue.Message);
				}
				else {
					IDX_CORE_WARN_TAG("PackageHost", "Package '{}' {}", issue.Package, issue.Message);
				}
			}
			if (!resolution.AutoIncluded.empty()) {
				std::string names;
				for (const std::string& name : resolution.AutoIncluded) names += (names.empty() ? "" : ", ") + name;
				IDX_CORE_INFO_TAG("PackageHost", "Also loading dependencies not listed in project.json: {}", names);
			}
		}

		size_t LoadInternal(bool isInitialLoadAll) {
			std::vector<std::filesystem::path> candidates;
			const IndexProject* activeProject = ProjectManager::GetCurrentProject();

			// Dependencies first; packages outside this list (and blocked ones) never load.
			std::vector<std::string> loadOrder;
			std::unordered_map<std::string, std::string> manifestVersions;
			if (activeProject && !activeProject->Packages.empty()) {
				const std::vector<PackageManifest> manifests = PackageHostDetail::DiscoverPackageManifests(*activeProject);
				// Against the active modules: a package registering 2D components can't load without Index.2D.
				const std::vector<std::string> activeModules = EngineModules::GetActiveIds();
				const PackageResolution resolution = ResolvePackages(manifests, activeProject->Packages,
					GetEngineCompatibilityVersion(), &activeModules);
				loadOrder = PackageHostDetail::BuildNativeLoadOrder(resolution, activeProject->Packages, manifests);
				for (const PackageManifest& manifest : manifests) manifestVersions.emplace(manifest.Name, manifest.VersionText);
				if (isInitialLoadAll) {
					LogResolutionIssues(resolution, activeProject->Packages, manifests);
				}
			}

			if (activeProject && !activeProject->PackagesDirectory.empty()) {
				PackageHostDetail::AppendProjectNativeCandidates(
					*activeProject, IndexProject::GetActiveBuildConfiguration(), loadOrder, candidates);
			}

			const std::filesystem::path exeDir(Path::ExecutableDir());
			// Dev / build layout: bin/<config>/<Consumer>/<exe>; package DLLs are siblings in
			// bin/<config>/Pkg.<Name>.Native/. Scan one level up from the exe directory.
			std::filesystem::path searchRoot = exeDir.parent_path();
			DiscoverIn(searchRoot, candidates);

			// Distribution layout (future): packages alongside the exe in a Packages/ folder.
			DiscoverIn(exeDir / "Packages", candidates);

			// INDEX_LOAD_ALL_PACKAGES is an engine-development escape hatch for the project allow-list.
			std::unordered_map<std::string, size_t> allowList;
			for (size_t i = 0; i < loadOrder.size(); ++i) {
				allowList.emplace(loadOrder[i], i);
			}

			bool loadEverything = false;
			if (const char* env = std::getenv("INDEX_LOAD_ALL_PACKAGES")) {
				if (env[0] == '1' || env[0] == 't' || env[0] == 'T') {
					loadEverything = true;
					if (isInitialLoadAll) {
						IDX_CORE_INFO_TAG("PackageHost",
							"INDEX_LOAD_ALL_PACKAGES is set — overriding the project's `packages` allow-list and loading every discovered package.");
					}
				}
			}

			if (!loadEverything && allowList.empty()) {
				// Listed packages that all failed resolution were already reported above.
				if (isInitialLoadAll && !(activeProject && !activeProject->Packages.empty())) {
					if (activeProject) {
						IDX_CORE_INFO_TAG("PackageHost",
							"No packages installed for project '{}' — skipping all {} discovered package DLL(s). Add entries to project.json's `packages` array to install them.",
							activeProject->Name, candidates.size());
					}
					else {
						IDX_CORE_INFO_TAG("PackageHost",
							"No project loaded — skipping all {} discovered package DLL(s). Pass --project=<path> or set INDEX_LOAD_ALL_PACKAGES=1 to load packages.",
							candidates.size());
					}
				}
				return 0;
			}

#ifdef IDX_PLATFORM_WINDOWS
			std::string runningEngineSha256;
			std::string engineFingerprintError;
			GetRunningEngineSha256(runningEngineSha256, engineFingerprintError);
#endif

			std::unordered_set<std::string> alreadyLoadedByName;
			std::unordered_set<std::string> alreadyLoadedByPath;
			for (const LoadedPackage& existing : s_LoadedPackages) {
				alreadyLoadedByName.insert(existing.Name);
				alreadyLoadedByPath.insert(existing.ModulePath);
			}

			// A dependency's OnLoad registers what its dependents' OnLoad looks up.
			if (!loadEverything) {
				std::stable_sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
					auto rank = [&](const std::filesystem::path& candidate) {
						const auto it = allowList.find(PackageNameFromFilename(PathToUtf8(candidate.filename())));
						return it == allowList.end() ? allowList.size() : it->second;
					};
					return rank(a) < rank(b);
				});
			}

			size_t newlyLoaded = 0;
			for (const auto& candidate : candidates) {
				const std::string pathStr = PathToUtf8(candidate);
				const std::string fileName = PathToUtf8(candidate.filename());
				const std::string packageName = PackageNameFromFilename(fileName);

				if (!loadEverything && allowList.find(packageName) == allowList.end()) {
					if (isInitialLoadAll) {
						IDX_CORE_INFO_TAG("PackageHost",
							"Skipping package '{}' — not in project's `packages` allow-list.", packageName);
					}
					continue;
				}

				if (alreadyLoadedByName.count(packageName) || alreadyLoadedByPath.count(pathStr)) {
					// Quietly skip — common during LoadInstalled() rescans.
					continue;
				}

#ifdef IDX_PLATFORM_WINDOWS
				std::string compatibilityReason;
				if (!PackageHostDetail::ValidateNativePackageCompatibility(
					candidate, runningEngineSha256, compatibilityReason)) {
					if (compatibilityReason == "running engine fingerprint is unavailable"
						&& !engineFingerprintError.empty()) {
						compatibilityReason += ": " + engineFingerprintError;
					}
					IDX_CORE_WARN_TAG("PackageHost",
						"Skipping native package '{}' from '{}': {}. Rebuild the package against "
						"this engine version.", packageName, pathStr, compatibilityReason);
					continue;
				}
#endif

				void* module = PackageHostDetail::LoadNativeModule(candidate);
				if (!module) {
					IDX_CORE_WARN_TAG("PackageHost",
						"Failed to load package '{}' from '{}' (the OS loader returned null — likely binary "
						"incompatibility with the running engine, or a static-init crash). Skipping.",
						packageName, pathStr);
					continue;
				}

				const uint32_t ownerId = s_NextOwnerId++;
				bool onLoadCrashed = false;
				bool onLoadHadExport = false;
				int onLoadResult = 0;
				{
					struct OwnerScope {
						explicit OwnerScope(uint32_t owner) { SceneManager::SetRegistrationOwner(owner); }
						~OwnerScope() { SceneManager::SetRegistrationOwner(0); }
					} ownerScope(ownerId);
					onLoadResult = InvokeOnLoadGuarded(module, &onLoadCrashed, &onLoadHadExport);
				}
				if (onLoadCrashed) {
					IDX_CORE_ERROR_TAG("PackageHost",
						"Package '{}' crashed during IndexPackage_OnLoad (likely binary incompatibility "
						"with the running engine — try uninstalling and reinstalling). Unloaded; editor continues.",
						packageName);
					SceneManager::UnregisterOwner(ownerId);
					PlatformUnload(module);
					continue;
				}
				if (onLoadHadExport && onLoadResult != 0) {
					IDX_CORE_WARN_TAG("PackageHost",
						"Package '{}' IndexPackage_OnLoad returned {} (non-zero); keeping module loaded.",
						packageName, onLoadResult);
				}
				else if (!onLoadHadExport) {
					IDX_CORE_INFO_TAG("PackageHost", "Loaded package '{}' (no IndexPackage_OnLoad export).", packageName);
				}

				LoadedPackage loaded;
				loaded.Name = packageName;
				if (const auto version = manifestVersions.find(packageName); version != manifestVersions.end()) {
					loaded.Version = version->second;
				}
				loaded.ModulePath = pathStr;
				loaded.ModuleHandle = module;
				loaded.OwnerId = ownerId;
				s_LoadedPackages.push_back(std::move(loaded));
				alreadyLoadedByName.insert(packageName);
				alreadyLoadedByPath.insert(pathStr);
				++newlyLoaded;
			}

			return newlyLoaded;
		}
	} // namespace

	void PackageHost::LoadAll() {
		if (s_LoadAllRan) {
			return;
		}

		const size_t loaded = LoadInternal(/*isInitialLoadAll=*/true);
		IDX_CORE_INFO_TAG("PackageHost", "Loaded {} package(s).", loaded);

		// Set the guard AFTER the load loop so a partial init (exception
		// thrown out of an OnLoad, etc.) leaves LoadAll callable again.
		s_LoadAllRan = true;
	}

	size_t PackageHost::LoadInstalled() {
		// No s_LoadAllRan check — this is the path that exists *because*
		// LoadAll already ran but didn't see the just-built DLL.
		const size_t loaded = LoadInternal(/*isInitialLoadAll=*/false);
		if (loaded > 0) {
			IDX_CORE_INFO_TAG("PackageHost",
				"LoadInstalled: loaded {} new package(s) (total now {}).",
				loaded, s_LoadedPackages.size());
		}
		return loaded;
	}

	// Safe order (Application::Shutdown): SceneManager is destroyed first, taking scenes, EnTT pools and the registry with it.
	// If it is still alive, registry entries are dropped here, but live entities of package types keep EnTT storage whose code is in the module.
	void PackageHost::UnloadAll() {
		if (!s_LoadedPackages.empty() && SceneManager::GetLoadedSceneCount() > 0) {
			IDX_CORE_WARN_TAG("PackageHost",
				"Unloading {} package(s) while {} scene(s) are loaded; unload scenes first so package component storage is released.",
				s_LoadedPackages.size(), SceneManager::GetLoadedSceneCount());
		}
		for (auto it = s_LoadedPackages.rbegin(); it != s_LoadedPackages.rend(); ++it) {
			bool onUnloadCrashed = false;
			InvokeOnUnloadGuarded(it->ModuleHandle, &onUnloadCrashed);
			if (onUnloadCrashed) {
				IDX_CORE_ERROR_TAG("PackageHost",
					"Package '{}' crashed during IndexPackage_OnUnload; continuing teardown.",
					it->Name);
			}
			SceneManager::UnregisterOwner(it->OwnerId);
			PlatformUnload(it->ModuleHandle);
		}
		s_LoadedPackages.clear();
		s_LoadAllRan = false;
	}

	const std::vector<LoadedPackage>& PackageHost::GetLoaded() {
		return s_LoadedPackages;
	}

	bool PackageHost::IsPackageLoaded(const std::string& packageName) {
		for (const LoadedPackage& loaded : s_LoadedPackages) {
			if (loaded.Name == packageName) return true;
		}
		return false;
	}

	std::vector<std::string> PackageHost::FindStaleProjectNativePackages() {
		const IndexProject* project = ProjectManager::GetCurrentProject();
		if (!project) return {};
		std::string runningEngineSha256;
#ifdef IDX_PLATFORM_WINDOWS
		std::string fingerprintError;
		GetRunningEngineSha256(runningEngineSha256, fingerprintError);
#endif
		return PackageHostDetail::FindStaleProjectNativePackages(
			*project, IndexProject::GetActiveBuildConfiguration(), runningEngineSha256);
	}

}			
