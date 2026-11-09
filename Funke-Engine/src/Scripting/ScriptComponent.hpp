#pragma once
#include "Core/Log.hpp"
#include "Scripting/ScriptInstance.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace Funke {

	struct ScriptComponent {
		std::vector<ScriptInstance> Scripts;
		std::vector<std::string> ManagedComponents;

		// Authored field values, reapplied whenever an instance binds or reloads.
		// Key: "ClassName.FieldName", Value: string representation.
		std::unordered_map<std::string, std::string> PendingFieldValues;
		// Raw dynamic IComponent payloads captured when scene deserialization
		// runs before the user assembly registers its dynamic component types.
		std::unordered_map<std::string, std::string> PendingDynamicComponentValues;

		ScriptComponent() = default;

		bool MigrateManagedIdentities(const std::function<std::string(const std::string&)>& resolve) {
			std::vector<std::string> owners = ManagedComponents;
			for (const auto& script : Scripts) owners.push_back(script.GetClassName());
			for (const auto& [name, _] : PendingDynamicComponentValues) owners.push_back(name);
			std::unordered_map<std::string, std::string> mappings;
			auto propose = [&](const std::string& name) {
				if (name.starts_with("type:")) return;
				std::string canonical = resolve(name);
				if (!canonical.empty() && canonical != name) mappings.emplace(name, std::move(canonical));
			};
			for (const auto& name : ManagedComponents) propose(name);
			for (const auto& [name, _] : PendingDynamicComponentValues) propose(name);
			for (const auto& script : Scripts) if (script.GetType() != ScriptType::Native) propose(script.GetClassName());
			bool changed = false;
			for (const auto& [oldName, newName] : mappings) {
				if (HasScript(newName) || HasManagedComponent(newName)) continue;
				bool collision = false;
				for (const auto& other : mappings) if (other.first != oldName && other.second == newName) collision = true;
				std::vector<std::pair<std::string, std::string>> fields;
				const std::string prefix = oldName + ".";
				for (const auto& [key, value] : PendingFieldValues) {
					if (!key.starts_with(prefix)) continue;
					bool owned = true;
					for (const auto& owner : owners) if (owner.size() > oldName.size() && key.starts_with(owner + ".")) owned = false;
					if (!owned) continue;
					const std::string target = newName + key.substr(oldName.size());
					if (PendingFieldValues.contains(target)) collision = true;
					fields.emplace_back(key, target);
				}
				if (PendingDynamicComponentValues.contains(oldName) && PendingDynamicComponentValues.contains(newName)) collision = true;
				if (collision) continue;
				for (const auto& [oldKey, newKey] : fields) {
					auto node = PendingFieldValues.extract(oldKey);
					node.key() = newKey;
					PendingFieldValues.insert(std::move(node));
				}
				if (auto node = PendingDynamicComponentValues.extract(oldName); !node.empty()) {
					node.key() = newName;
					PendingDynamicComponentValues.insert(std::move(node));
				}
				for (auto& script : Scripts) if (script.GetType() != ScriptType::Native && script.GetClassName() == oldName) { script.SetClassName(newName); script.SetType(ScriptType::Managed); }
				for (auto& name : ManagedComponents) if (name == oldName) name = newName;
				changed = true;
			}
			return changed;
		}

		bool AddScript(const std::string& className, ScriptType type = ScriptType::Managed) {
			if (className.empty() || HasScript(className, type)) {
				return false;
			}
			if (HasScript(className) || HasManagedComponent(className)) {
				FNK_WARN_TAG("ScriptComponent", "Cannot attach '{}': this entity already has a script or component with that class name. "
					"Use a unique class name or namespace so saved fields remain unambiguous.", className);
				return false;
			}

			Scripts.emplace_back(className, type);
			return true;
		}

		bool HasScript(const std::string& className) const {
			for (const auto& s : Scripts) {
				if (s.GetClassName() == className) return true;
			}
			return false;
		}

		bool HasScript(const std::string& className, ScriptType type) const {
			for (const auto& s : Scripts) {
				if (s.GetClassName() == className && s.GetType() == type) return true;
			}
			return false;
		}

		bool AddManagedComponent(const std::string& className) {
			if (className.empty() || HasManagedComponent(className)) {
				return false;
			}
			if (HasScript(className)) {
				FNK_WARN_TAG("ScriptComponent", "Cannot attach '{}': this entity already has a script with that class name. "
					"Use a unique class name or namespace so saved fields remain unambiguous.", className);
				return false;
			}

			ManagedComponents.push_back(className);
			return true;
		}

		bool HasManagedComponent(const std::string& className) const {
			for (const auto& componentClass : ManagedComponents) {
				if (componentClass == className) return true;
			}
			return false;
		}

		bool RemoveManagedComponent(const std::string& className) {
			for (auto it = ManagedComponents.begin(); it != ManagedComponents.end(); ++it) {
				if (*it == className) {
					ManagedComponents.erase(it);
					const std::string prefix = className + ".";
					for (auto fieldIt = PendingFieldValues.begin(); fieldIt != PendingFieldValues.end(); ) {
						if (fieldIt->first.rfind(prefix, 0) == 0) {
							fieldIt = PendingFieldValues.erase(fieldIt);
						}
						else {
							++fieldIt;
						}
					}
					return true;
				}
			}
			return false;
		}
	};

} 
