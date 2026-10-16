#pragma once
#include "Core/Export.hpp"
#include "Packages/PackageManifest.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace Funke {

	struct PackageLockEntry {
		std::string Name;
		std::string Version;
		// "registry", "git", "local", "engine" or "unknown".
		std::string Source = "unknown";
		// Registry: download URL; git: repository URL.
		std::string Url;
		// Registry archives only; restore refuses a download that does not match.
		std::string Sha256;
		std::vector<std::string> Dependencies;
	};

	// packages-lock.json in the project root: the exact package versions a project was last resolved with.
	// Packages/ is not committed, so this file is what lets a fresh clone restore the same versions.
	struct FUNKE_API PackageLock {
		static constexpr std::string_view k_FileName = "packages-lock.json";
		static constexpr int k_FormatVersion = 1;

		std::string EngineVersion;
		// Sorted by name so diffs stay small.
		std::vector<PackageLockEntry> Packages;

		static std::string GetPath(const std::string& projectRoot);
		// A missing file is not an error: outLock is left empty and true is returned.
		static bool TryLoad(const std::string& projectRoot, PackageLock& outLock, std::string* outError = nullptr);
		bool Save(const std::string& projectRoot) const;

		const PackageLockEntry* Find(std::string_view name) const;
		void Upsert(PackageLockEntry entry);
		void Remove(std::string_view name);

		// Lock for the resolved packages; source details carry over from `previous` while the version is unchanged.
		static PackageLock Build(const std::vector<std::string>& resolvedNames, const std::vector<PackageManifest>& manifests,
			const PackageLock& previous, std::string engineVersion);
	};

}
