#include "pch.hpp"
#include "Packages/PackageRegistryIndex.hpp"

#include "Core/EngineModules.hpp"
#include "Serialization/Json.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_map>

namespace Funke {
	namespace {
		bool IsSha256Hex(std::string_view value) {
			return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
				return std::isxdigit(static_cast<unsigned char>(c)) != 0;
			});
		}

		void ReadDependencies(const Json::Value& node, std::vector<PackageDependency>& out) {
			auto add = [&](std::string name, std::string range) {
				if (!PackageManifest::IsValidName(name)) return;
				if (range.empty()) range = "*";
				if (!VersionRange::Parse(range)) return;
				out.push_back({ std::move(name), std::move(range) });
			};
			if (node.IsArray()) {
				for (const Json::Value& entry : node.GetArray()) {
					if (entry.IsString()) {
						const std::string text = entry.AsStringOr();
						const size_t at = text.find('@');
						add(text.substr(0, at), at == std::string::npos ? std::string() : text.substr(at + 1));
					}
					else if (entry.IsObject()) {
						const Json::Value* name = entry.FindMember("name");
						const Json::Value* version = entry.FindMember("version");
						if (name) add(name->AsStringOr(), version ? version->AsStringOr() : std::string());
					}
				}
			}
			else if (node.IsObject()) {
				for (const auto& [name, range] : node.GetObject()) {
					add(name, range.AsStringOr());
				}
			}
		}

		std::string ReadEngineRange(const Json::Value& node) {
			if (const Json::Value* engine = node.FindMember("engine")) {
				const std::string range = engine->AsStringOr();
				if (!range.empty() && VersionRange::Parse(range)) return range;
			}
			if (const Json::Value* minimum = node.FindMember("minEngineVersion")) {
				const std::string version = minimum->AsStringOr();
				if (!version.empty() && VersionRange::Parse(">=" + version)) return ">=" + version;
			}
			return {};
		}

		bool ReadRelease(const Json::Value& node, RegistryPackageVersion& out) {
			const Json::Value* version = node.FindMember("version");
			const Json::Value* url = node.FindMember("downloadUrl");
			const Json::Value* sha = node.FindMember("sha256");
			if (!version || !url || !sha) return false;
			out.VersionText = version->AsStringOr();
			out.Version = SemanticVersion::Parse(out.VersionText);
			out.DownloadUrl = url->AsStringOr();
			out.Sha256 = sha->AsStringOr();
			if (out.VersionText.empty() || out.DownloadUrl.empty() || !IsSha256Hex(out.Sha256)) return false;
			out.EngineRange = ReadEngineRange(node);
			if (const Json::Value* dependencies = node.FindMember("dependencies")) ReadDependencies(*dependencies, out.Dependencies);
			if (const Json::Value* modules = node.FindMember("modules"); modules && modules->IsArray()) {
				for (const Json::Value& module : modules->GetArray()) {
					if (module.IsString() && !module.AsStringOr().empty()) out.Modules.push_back(module.AsStringOr());
				}
			}
			if (const Json::Value* size = node.FindMember("sizeBytes")) out.SizeBytes = size->AsUInt64Or(0);
			return true;
		}

		bool SupportsEngine(const RegistryPackageVersion& release, const SemanticVersion& engineVersion) {
			if (release.EngineRange.empty()) return true;
			const auto range = VersionRange::Parse(release.EngineRange);
			return range && range->IsSatisfiedBy(engineVersion.WithoutPreRelease());
		}
	}

	const RegistryPackageVersion* RegistryPackage::FindBest(const VersionRange& range, const SemanticVersion& engineVersion) const {
		for (const RegistryPackageVersion& release : Versions) {
			if (!release.Version) continue;
			const bool inRange = range.IsAny() ? !release.Version->IsPreRelease() : range.IsSatisfiedBy(*release.Version);
			if (inRange && SupportsEngine(release, engineVersion)) return &release;
		}
		return nullptr;
	}

	const RegistryPackageVersion* RegistryPackage::Find(std::string_view versionText) const {
		const auto wanted = SemanticVersion::Parse(versionText);
		for (const RegistryPackageVersion& release : Versions) {
			if (release.VersionText == versionText || (wanted && release.Version && *release.Version == *wanted)) return &release;
		}
		return nullptr;
	}

	bool ParseRegistryIndex(std::string_view jsonText, std::vector<RegistryPackage>& outPackages, std::string& outError) {
		outPackages.clear();
		Json::Value root;
		std::string parseError;
		if (!Json::TryParse(jsonText, root, &parseError)) {
			outError = "Registry JSON parse failed: " + parseError;
			return false;
		}
		const Json::Value* packages = root.IsObject() ? root.FindMember("packages") : nullptr;
		if (!packages || !packages->IsArray()) {
			outError = "Registry JSON missing 'packages' array.";
			return false;
		}

		for (const Json::Value& node : packages->GetArray()) {
			if (!node.IsObject()) continue;
			RegistryPackage package;
			if (const Json::Value* v = node.FindMember("name")) package.Name = v->AsStringOr();
			if (!PackageManifest::IsValidName(package.Name)) continue;
			if (const Json::Value* v = node.FindMember("description")) package.Description = v->AsStringOr();
			if (const Json::Value* v = node.FindMember("homepage")) package.Homepage = v->AsStringOr();
			if (const Json::Value* v = node.FindMember("license")) package.License = v->AsStringOr();
			if (const Json::Value* layers = node.FindMember("layers"); layers && layers->IsArray()) {
				for (const Json::Value& layer : layers->GetArray()) {
					const std::string name = layer.AsStringOr();
					if (name == "native" || name == "engine_core") package.HasNativeLayer = true;
					else if (name == "native_standalone" || name == "standalone_cpp") package.HasNativeStandaloneLayer = true;
					else if (name == "csharp") package.HasCSharpLayer = true;
				}
			}

			RegistryPackageVersion topLevel;
			if (ReadRelease(node, topLevel)) package.Versions.push_back(std::move(topLevel));
			if (const Json::Value* versions = node.FindMember("versions"); versions && versions->IsArray()) {
				for (const Json::Value& entry : versions->GetArray()) {
					RegistryPackageVersion release;
					if (!entry.IsObject() || !ReadRelease(entry, release)) continue;
					const bool duplicate = std::any_of(package.Versions.begin(), package.Versions.end(),
						[&](const RegistryPackageVersion& existing) { return existing.VersionText == release.VersionText; });
					if (!duplicate) package.Versions.push_back(std::move(release));
				}
			}
			if (package.Versions.empty()) continue;

			std::stable_sort(package.Versions.begin(), package.Versions.end(),
				[](const RegistryPackageVersion& a, const RegistryPackageVersion& b) {
					if (a.Version && b.Version) return *a.Version > *b.Version;
					return a.Version.has_value() && !b.Version.has_value();
				});
			outPackages.push_back(std::move(package));
		}
		return true;
	}

	const RegistryPackage* FindRegistryPackage(const std::vector<RegistryPackage>& packages, std::string_view name) {
		for (const RegistryPackage& package : packages) {
			if (package.Name == name) return &package;
		}
		return nullptr;
	}

	PackageInstallPlan PlanRegistryInstall(const std::vector<RegistryPackage>& registry,
		const std::vector<PackageManifest>& installed, std::string_view name, std::string_view range,
		const SemanticVersion& engineVersion, bool updateRoot) {
		PackageInstallPlan plan;
		std::unordered_map<std::string, const PackageManifest*> installedByName;
		for (const PackageManifest& manifest : installed) installedByName.emplace(manifest.Name, &manifest);
		std::unordered_map<std::string, const RegistryPackageVersion*> planned;
		std::unordered_map<std::string, bool> visiting;

		std::function<bool(const std::string&, const std::string&, const std::string&, bool)> ensure =
			[&](const std::string& packageName, const std::string& rangeText, const std::string& requiredBy, bool isRoot) -> bool {
			const auto parsedRange = VersionRange::Parse(rangeText);
			if (!parsedRange) {
				plan.Errors.push_back("'" + requiredBy + "' declares an invalid range '" + rangeText + "' for " + packageName);
				return false;
			}
			const std::string who = requiredBy.empty() ? std::string("You") : "'" + requiredBy + "'";

			if (const auto it = planned.find(packageName); it != planned.end()) {
				if (!parsedRange->IsAny() && !parsedRange->IsSatisfiedBy(*it->second->Version)) {
					plan.Errors.push_back(who + " needs " + packageName + " " + rangeText
						+ ", but the plan already picks " + it->second->VersionText);
					return false;
				}
				return true;
			}
			if (visiting[packageName]) return true; // cycle: the outer visit validates the version

			const auto installedIt = installedByName.find(packageName);
			const PackageManifest* current = installedIt != installedByName.end() ? installedIt->second : nullptr;
			const bool forceUpdate = isRoot && updateRoot;
			if (current && !forceUpdate) {
				if (parsedRange->IsAny() || (current->Version && parsedRange->IsSatisfiedBy(*current->Version))) {
					return true;
				}
			}

			const RegistryPackage* package = FindRegistryPackage(registry, packageName);
			if (!package) {
				plan.Errors.push_back(current
					? who + " needs " + packageName + " " + rangeText + ", installed is " + current->VersionText + " and the registry has no other release"
					: who + " needs " + packageName + ", which is neither installed nor in the registry");
				return false;
			}
			const RegistryPackageVersion* best = package->FindBest(*parsedRange, engineVersion);
			if (!best) {
				std::string available;
				for (const RegistryPackageVersion& release : package->Versions) {
					available += (available.empty() ? "" : ", ") + release.VersionText;
				}
				plan.Errors.push_back(who + " needs " + packageName + " " + rangeText + ", but no release fits that range and Funke "
					+ engineVersion.ToString() + " (registry has " + available + ")");
				return false;
			}
			if (forceUpdate && current && current->Version && best->Version && !(*best->Version > *current->Version)) {
				plan.Errors.push_back(packageName + " " + current->VersionText + " is already the newest compatible release");
				return false;
			}

			if (current) {
				for (const PackageManifest& other : installed) {
					const PackageDependency* dependency = other.FindDependency(packageName);
					if (!dependency) continue;
					const auto otherRange = VersionRange::Parse(dependency->Range);
					if (otherRange && !otherRange->IsAny() && !otherRange->IsSatisfiedBy(*best->Version)) {
						plan.Errors.push_back("updating " + packageName + " to " + best->VersionText + " would break '" + other.Name
							+ "', which needs " + dependency->Range);
						return false;
					}
				}
			}

			for (const std::string& module : best->Modules) {
				const std::optional<EngineModule> engineModule = EngineModules::FromId(module);
				if (!engineModule || !EngineModules::IsCompiled(*engineModule)) {
					plan.Errors.push_back(packageName + " " + best->VersionText + " needs the engine module " + module
						+ ", which this build of Funke does not have");
					return false;
				}
			}

			planned[packageName] = best;
			visiting[packageName] = true;
			bool ok = true;
			for (const PackageDependency& dependency : best->Dependencies) {
				ok = ensure(dependency.Name, dependency.Range, packageName, false) && ok;
			}
			visiting[packageName] = false;

			PackageInstallStep step;
			step.Kind = current ? PackageInstallStep::Action::Update : PackageInstallStep::Action::Install;
			step.Name = packageName;
			step.Version = best->VersionText;
			step.FromVersion = current ? current->VersionText : std::string();
			step.DownloadUrl = best->DownloadUrl;
			step.Sha256 = best->Sha256;
			step.Requested = isRoot;
			plan.Steps.push_back(std::move(step));
			return ok;
		};

		ensure(std::string(name), range.empty() ? std::string("*") : std::string(range), {}, true);
		if (!plan.IsValid()) plan.Steps.clear();
		return plan;
	}

}
