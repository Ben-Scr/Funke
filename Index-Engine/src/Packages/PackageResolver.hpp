#pragma once
#include "Core/Export.hpp"
#include "Packages/PackageManifest.hpp"
#include "Packages/SemanticVersion.hpp"

#include <string>
#include <vector>

namespace Index {

	struct PackageIssue {
		enum class Severity : uint8_t { Warning, Error };
		Severity Level = Severity::Error;
		std::string Package;
		std::string Message;
	};

	struct INDEX_API PackageResolution {
		// Dependencies before dependents; requested order is kept where the graph allows.
		std::vector<std::string> LoadOrder;
		// Packages pulled in only because something in the request depends on them.
		std::vector<std::string> AutoIncluded;
		// Requested or required packages that cannot load, with everything that depends on them.
		std::vector<std::string> Blocked;
		std::vector<PackageIssue> Issues;

		bool HasErrors() const;
		bool IsBlocked(std::string_view name) const;
		std::vector<std::string> GetMessages(std::string_view package) const;
	};

	// Resolves the project's package list against the manifests on disk: transitive dependencies,
	// version ranges, engine compatibility, cycles and, when `enabledModules` is given, the engine modules
	// each package needs. A failing package blocks only itself and its dependents.
	INDEX_API PackageResolution ResolvePackages(const std::vector<PackageManifest>& available,
		const std::vector<std::string>& requested, const SemanticVersion& engineVersion,
		const std::vector<std::string>* enabledModules = nullptr);

	// Engine modules `package` and its dependencies need, in first-seen order.
	INDEX_API std::vector<std::string> CollectRequiredModules(const std::vector<PackageManifest>& available, std::string_view package);

	// Installed packages (and their dependencies) that need the engine module `moduleId`.
	INDEX_API std::vector<std::string> FindModuleDependents(const std::vector<PackageManifest>& available,
		const std::vector<std::string>& installed, std::string_view moduleId);

	// Installed packages that list `name` as a dependency (directly).
	INDEX_API std::vector<std::string> FindDependents(const std::vector<PackageManifest>& installed, std::string_view name);

}
