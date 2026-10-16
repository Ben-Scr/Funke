#pragma once

#include "Packages/PackageLock.hpp"
#include "Packages/PackageManifest.hpp"
#include "Packages/PackageResolver.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Funke {
	struct FunkeProject;

	// Lightweight projection of funke-package.lua's top-level fields.
	// Used to populate the package-manager UI lists; not the full premake-loader manifest.
	struct FunkePackageManifest {
		std::string Name;          // "name" field — required
		std::string Version;       // "version" field — required
		std::string Description;   // "description" field — optional
		std::string PackageDir;    // Absolute path to the package directory
		bool        IsEngine = false; // true if under <engine-root>/packages/, else project-local

		bool HasNativeLayer = false;            // canonical "native" (legacy alias: "engine_core")
		bool HasNativeStandaloneLayer = false;  // canonical "native_standalone" (legacy alias: "standalone_cpp")
		bool HasCSharpLayer = false;

		bool CSharpAllowUnsafe = false;

		std::string EngineRange;                    // "engine" field; empty = any engine
		std::vector<PackageDependency> Dependencies;
		std::vector<std::string> Modules;           // "modules" field: engine modules the package needs

		PackageManifest ToPackageManifest() const;
	};

	// Install/validate helpers for funke-package.lua packages (engine packages under <engine>/packages/, project packages under <project>/Packages/).
	class FunkePackageInstaller {
	public:
		struct InstallResult {
			bool        Success = false;
			std::string Message;
			// On successful GitHub/Local install, the package name parsed from the
			// cloned manifest. Caller can pass this to InstallToProject() to also add
			// the package to the project's allow-list.
			std::string PackageName;
			// Engine modules the operation added to project.json `modules`; they take effect after an editor reload.
			std::vector<std::string> EnabledModules;
		};

		// Engine modules (Funke.2D, Funke.3D) live in project.json `modules` and are offered next to
		// packages. Enabling saves the project; the editor applies it on reload.
		static InstallResult EnableModule(FunkeProject& project, const std::string& moduleId);
		// Refuses while an installed package needs the module.
		static InstallResult DisableModule(FunkeProject& project, const std::string& moduleId);

		// Read funke-package.lua at `<packageDir>/funke-package.lua` and project-relevant
		// fields. Returns nullopt if the file is missing or required fields (name, version)
		// are absent. Manifest parsing is pattern-based; not a full Lua interpreter.
		static std::optional<FunkePackageManifest> ReadManifest(const std::string& packageDir);
		static bool RequiresNativeRuntimeLoad(const FunkePackageManifest& manifest);
		static bool ShouldRollbackAfterLoad(const FunkePackageManifest& manifest, bool isLoaded);

		// Convenience: returns true if `<packageDir>/funke-package.lua` exists and parses.
		// If invalid, fills outError with a human-readable reason.
		static bool ValidatePackageDir(const std::string& packageDir, std::string& outError);

		// Enumerate every <rootDir>/<Name>/funke-package.lua and return parsed manifests.
		// Skips entries that fail validation (silently — caller logs if it cares).
		static std::vector<FunkePackageManifest> EnumeratePackages(const std::string& rootDir, bool isEngine);

		// Convenience that scans both engine + project roots in one call.
		// projectRootDir may be empty (no project loaded).
		static std::vector<FunkePackageManifest> EnumerateAll(const std::string& projectRootDir = {});
		// Clone a GitHub repo into <projectPackagesDir>/<repoName>/. Validates the clone
		// has funke-package.lua at root; on failure, removes the partial clone.
		static InstallResult InstallFromGitHub(const std::string& url, const std::string& projectPackagesDir);

		// Copy a local directory into <projectPackagesDir>/<dirName>/. Validates the source
		// is a valid Funke package before copying.
		static InstallResult InstallFromLocal(const std::string& srcDir, const std::string& projectPackagesDir);

		// Remove a project-local package directory. Refuses to touch engine packages.
		static InstallResult Uninstall(const std::string& projectPackagesDir, const std::string& packageName);

		// Add a package name to project.Packages and persist project.json.
		// Validates the package exists either in <engine-root>/packages/ or
		// <project>/Packages/. After mutating the project, regenerates the
		// FunkePackages.props that the .csproj imports.
		// Caller is responsible for running premake regen + msbuild afterwards.
		static InstallResult InstallToProject(FunkeProject& project, const std::string& packageName);

		// Remove a package name from project.Packages, persist, and regenerate
		// FunkePackages.props. Does NOT delete the package's files on disk.
		static InstallResult UninstallFromProject(FunkeProject& project, const std::string& packageName);

		static void RegeneratePackageReferences(const FunkeProject& project);

		// The project's package list resolved against what is on disk (dependencies, versions, engine range).
		static PackageResolution ResolveProject(const FunkeProject& project, const std::vector<FunkePackageManifest>& manifests);
		static std::vector<PackageManifest> ToPackageManifests(const std::vector<FunkePackageManifest>& manifests);

		// Fails with the names of loaded packages that still need `packageName`.
		static InstallResult CheckCanUninstall(const FunkeProject& project, const std::string& packageName);

		// Rewrites packages-lock.json from the current resolution. `sourceHints` record where freshly
		// installed versions came from (registry URL + SHA-256, git URL, local copy).
		static bool UpdateLock(const FunkeProject& project, const std::vector<PackageLockEntry>& sourceHints = {});
	};

}
