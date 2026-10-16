#include "pch.hpp"
#include "Packages/PackageResolver.hpp"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace Funke {

	bool PackageResolution::HasErrors() const {
		return std::any_of(Issues.begin(), Issues.end(), [](const PackageIssue& issue) {
			return issue.Level == PackageIssue::Severity::Error;
		});
	}

	bool PackageResolution::IsBlocked(std::string_view name) const {
		return std::find(Blocked.begin(), Blocked.end(), name) != Blocked.end();
	}

	std::vector<std::string> PackageResolution::GetMessages(std::string_view package) const {
		std::vector<std::string> messages;
		for (const PackageIssue& issue : Issues) {
			if (issue.Package == package) messages.push_back(issue.Message);
		}
		return messages;
	}

	PackageResolution ResolvePackages(const std::vector<PackageManifest>& available,
		const std::vector<std::string>& requested, const SemanticVersion& engineVersion,
		const std::vector<std::string>* enabledModules) {
		PackageResolution result;
		auto addIssue = [&](PackageIssue::Severity level, const std::string& package, std::string message) {
			result.Issues.push_back({ level, package, std::move(message) });
		};

		std::unordered_map<std::string, const PackageManifest*> byName;
		for (const PackageManifest& manifest : available) {
			if (!byName.emplace(manifest.Name, &manifest).second) {
				addIssue(PackageIssue::Severity::Warning, manifest.Name,
					"found more than once; using " + byName[manifest.Name]->PackageDir);
			}
		}

		enum class State : uint8_t { Visiting, Done };
		std::unordered_map<std::string, State> states;
		std::unordered_map<std::string, bool> loadable;
		std::vector<std::string> stack;

		std::function<bool(const std::string&, const std::string&)> visit =
			[&](const std::string& name, const std::string& requiredBy) -> bool {
			if (const auto state = states.find(name); state != states.end()) {
				if (state->second == State::Done) return loadable[name];
				// Back edge: report the whole loop once, from its first member.
				const auto loopStart = std::find(stack.begin(), stack.end(), name);
				std::string loop;
				for (auto it = loopStart; it != stack.end(); ++it) loop += *it + " -> ";
				addIssue(PackageIssue::Severity::Error, name, "cyclic dependency: " + loop + name);
				return false;
			}

			const auto found = byName.find(name);
			if (found == byName.end()) {
				addIssue(PackageIssue::Severity::Error, name, requiredBy.empty()
					? "is listed in project.json but no funke-package.lua with that name was found"
					: "is required by '" + requiredBy + "' but is not installed");
				states[name] = State::Done;
				loadable[name] = false;
				result.Blocked.push_back(name);
				return false;
			}
			const PackageManifest& manifest = *found->second;

			states[name] = State::Visiting;
			stack.push_back(name);
			bool ok = true;

			if (!manifest.Version) {
				addIssue(PackageIssue::Severity::Warning, name,
					"version '" + manifest.VersionText + "' is not MAJOR.MINOR.PATCH; dependents cannot check it");
			}
			if (!manifest.IsCompatibleWithEngine(engineVersion)) {
				addIssue(PackageIssue::Severity::Error, name,
					"needs engine " + manifest.EngineRange + ", but this is Funke " + engineVersion.ToString());
				ok = false;
			}
			if (enabledModules) {
				for (const std::string& module : manifest.Modules) {
					if (std::find(enabledModules->begin(), enabledModules->end(), module) == enabledModules->end()) {
						addIssue(PackageIssue::Severity::Error, name, "needs the engine module " + module
							+ ", which the project does not enable (Package Manager > Engine Modules)");
						ok = false;
					}
				}
			}

			for (const PackageDependency& dependency : manifest.Dependencies) {
				const bool dependencyLoads = visit(dependency.Name, name);
				const auto dependencyManifest = byName.find(dependency.Name);
				if (dependencyManifest != byName.end()) {
					const auto range = VersionRange::Parse(dependency.Range);
					const auto& installedVersion = dependencyManifest->second->Version;
					if (range && !range->IsAny()) {
						if (!installedVersion) {
							addIssue(PackageIssue::Severity::Error, name, "requires " + dependency.Name + " " + dependency.Range
								+ ", but its version '" + dependencyManifest->second->VersionText + "' cannot be compared");
							ok = false;
						}
						else if (!range->IsSatisfiedBy(*installedVersion)) {
							addIssue(PackageIssue::Severity::Error, name, "requires " + dependency.Name + " " + dependency.Range
								+ ", but " + installedVersion->ToString() + " is installed");
							ok = false;
						}
					}
				}
				if (!dependencyLoads) {
					if (dependencyManifest != byName.end()) {
						addIssue(PackageIssue::Severity::Error, name, "cannot load because its dependency '" + dependency.Name + "' cannot load");
					}
					ok = false;
				}
			}

			stack.pop_back();
			states[name] = State::Done;
			loadable[name] = ok;
			if (ok) {
				result.LoadOrder.push_back(name);
			}
			else {
				result.Blocked.push_back(name);
			}
			return ok;
		};

		for (const std::string& name : requested) {
			visit(name, {});
		}

		// A dependency whose only dependents are blocked has no reason to load.
		std::unordered_map<std::string, bool> needed;
		std::function<void(const std::string&)> markNeeded = [&](const std::string& name) {
			if (needed[name] || !loadable[name]) return;
			needed[name] = true;
			for (const PackageDependency& dependency : byName.at(name)->Dependencies) markNeeded(dependency.Name);
		};
		for (const std::string& name : requested) {
			if (byName.count(name)) markNeeded(name);
		}
		std::erase_if(result.LoadOrder, [&](const std::string& name) { return !needed[name]; });

		for (const std::string& name : result.LoadOrder) {
			if (std::find(requested.begin(), requested.end(), name) == requested.end()) {
				result.AutoIncluded.push_back(name);
			}
		}
		return result;
	}

	std::vector<std::string> CollectRequiredModules(const std::vector<PackageManifest>& available, std::string_view package) {
		std::unordered_map<std::string_view, const PackageManifest*> byName;
		for (const PackageManifest& manifest : available) byName.emplace(manifest.Name, &manifest);

		std::vector<std::string> modules;
		std::unordered_set<std::string_view> visited;
		std::function<void(std::string_view)> visit = [&](std::string_view name) {
			if (!visited.insert(name).second) return;
			const auto found = byName.find(name);
			if (found == byName.end()) return;
			for (const std::string& module : found->second->Modules) {
				if (std::find(modules.begin(), modules.end(), module) == modules.end()) modules.push_back(module);
			}
			for (const PackageDependency& dependency : found->second->Dependencies) visit(dependency.Name);
		};
		visit(package);
		return modules;
	}

	std::vector<std::string> FindModuleDependents(const std::vector<PackageManifest>& available,
		const std::vector<std::string>& installed, std::string_view moduleId) {
		std::vector<std::string> dependents;
		for (const std::string& package : installed) {
			const std::vector<std::string> modules = CollectRequiredModules(available, package);
			if (std::find(modules.begin(), modules.end(), moduleId) != modules.end()) dependents.push_back(package);
		}
		return dependents;
	}

	std::vector<std::string> FindDependents(const std::vector<PackageManifest>& installed, std::string_view name) {
		std::vector<std::string> dependents;
		for (const PackageManifest& manifest : installed) {
			if (manifest.FindDependency(name)) dependents.push_back(manifest.Name);
		}
		return dependents;
	}

}
