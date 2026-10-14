#pragma once
#include "Core/Export.hpp"
#include "Packages/PackageManifest.hpp"
#include "Packages/SemanticVersion.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Index {

	struct RegistryPackageVersion {
		std::string VersionText;
		std::optional<SemanticVersion> Version;
		std::string DownloadUrl;
		std::string Sha256;
		std::string EngineRange;
		std::vector<PackageDependency> Dependencies;
		// Engine modules the release needs, as written (an id this engine doesn't know stays and blocks the install).
		std::vector<std::string> Modules;
		uint64_t SizeBytes = 0;
	};

	struct INDEX_API RegistryPackage {
		std::string Name;
		std::string Description;
		std::string Homepage;
		std::string License;
		bool HasNativeLayer = false;
		bool HasNativeStandaloneLayer = false;
		bool HasCSharpLayer = false;
		// Newest first.
		std::vector<RegistryPackageVersion> Versions;

		const RegistryPackageVersion* Latest() const { return Versions.empty() ? nullptr : &Versions.front(); }
		// Newest version inside `range` that supports `engineVersion`; pre-releases only when the range asks for them.
		const RegistryPackageVersion* FindBest(const VersionRange& range, const SemanticVersion& engineVersion) const;
		const RegistryPackageVersion* Find(std::string_view versionText) const;
	};

	// Parses the registry index.json. Each entry's top-level version/downloadUrl/sha256 is one release;
	// an optional "versions" array adds older (or newer) releases. "minEngineVersion" is read as ">=X".
	INDEX_API bool ParseRegistryIndex(std::string_view jsonText, std::vector<RegistryPackage>& outPackages, std::string& outError);
	INDEX_API const RegistryPackage* FindRegistryPackage(const std::vector<RegistryPackage>& packages, std::string_view name);

	struct PackageInstallStep {
		enum class Action : uint8_t { Install, Update };
		Action Kind = Action::Install;
		std::string Name;
		std::string Version;
		std::string FromVersion;
		std::string DownloadUrl;
		std::string Sha256;
		// Added to the project's package list (the root request); dependencies load transitively.
		bool Requested = false;
	};

	struct INDEX_API PackageInstallPlan {
		// Dependencies before the packages that need them.
		std::vector<PackageInstallStep> Steps;
		std::vector<std::string> Errors;
		bool IsValid() const { return Errors.empty(); }
	};

	// Plans installing (or, with `updateRoot`, updating) `name` from the registry. Installed packages whose
	// version satisfies every range stay; others are updated to the newest fitting release. A plan that would
	// push an installed package outside a range another installed package declares is rejected.
	INDEX_API PackageInstallPlan PlanRegistryInstall(const std::vector<RegistryPackage>& registry,
		const std::vector<PackageManifest>& installed, std::string_view name, std::string_view range,
		const SemanticVersion& engineVersion, bool updateRoot);

}
