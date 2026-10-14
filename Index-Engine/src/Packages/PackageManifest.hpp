#pragma once
#include "Core/Export.hpp"
#include "Packages/SemanticVersion.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Index {

	struct PackageDependency {
		std::string Name;
		// As written; "*" when the manifest lists a bare name.
		std::string Range = "*";
	};

	// The fields of an index-package.lua the engine and editor act on. premake/package-loader.lua owns the build-only fields.
	struct INDEX_API PackageManifest {
		std::string Name;
		std::string VersionText;
		std::optional<SemanticVersion> Version;
		std::string Description;
		// "" for game packages, "platform" / "build-component" for export targets.
		std::string Kind;
		// Engine versions the package supports, e.g. ">=2026.1.0 <2027.0.0". Empty = any.
		std::string EngineRange;
		std::vector<PackageDependency> Dependencies;
		// Engine modules the package builds on ("Index.2D", "Index.3D"); installing it enables them.
		std::vector<std::string> Modules;

		bool HasNativeLayer = false;
		bool HasNativeStandaloneLayer = false;
		bool HasCSharpLayer = false;
		bool CSharpAllowUnsafe = false;

		// Directory holding index-package.lua; set by TryLoad.
		std::string PackageDir;

		static constexpr std::string_view k_FileName = "index-package.lua";

		// Reads `return { ... }` (or `local m = { ... } return m`) with literal values only.
		// Fails on a missing or malformed name, version, engine range or dependency entry.
		static bool TryParse(std::string_view luaSource, PackageManifest& outManifest, std::string* outError = nullptr);
		static bool TryLoad(const std::string& packageDir, PackageManifest& outManifest, std::string* outError = nullptr);

		static bool IsValidName(std::string_view name);
		bool IsCompatibleWithEngine(const SemanticVersion& engineVersion) const;
		const PackageDependency* FindDependency(std::string_view name) const;
	};

	// The running engine's version (IDX_VERSION). Pre-release suffixes are dropped for compatibility
	// checks, so a package that needs ">=2026.1.0" also loads on 2026.1.0-beta.1.
	INDEX_API SemanticVersion GetEngineCompatibilityVersion();

}
