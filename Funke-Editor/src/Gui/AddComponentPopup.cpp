#include <pch.hpp>
#include "Gui/AddComponentPopup.hpp"

#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiUtils.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptDiscovery.hpp"

#include <imgui.h>

#include <algorithm>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Funke {

	namespace {

		std::string BuildScriptMenuLabel(const EditorScriptDiscovery::ScriptEntry& scriptEntry)
		{
			return EditorScriptDiscovery::MenuLabel(scriptEntry);
		}

		bool AttachScriptToEntity(Entity entity, Scene& scene, const EditorScriptDiscovery::ScriptEntry& scriptEntry)
		{
			if (scriptEntry.ClassName.empty() || scriptEntry.Type == ScriptType::Unknown) {
				return false;
			}

			// Guard against hot-reload / scene-swap invalidating the captured Entity/Scene between frames.
			if (!entity.IsValid() || !scene.IsValid(entity.GetHandle())) {
				return false;
			}

			if (!entity.HasComponent<ScriptComponent>()) {
				entity.AddComponent<ScriptComponent>();
			}

			auto& scriptComponent = entity.GetComponent<ScriptComponent>();
			const bool alreadyHadScript = scriptComponent.HasScript(scriptEntry.ClassName, scriptEntry.Type);

			if (!alreadyHadScript) {
				scriptComponent.AddScript(scriptEntry.ClassName, scriptEntry.Type);
			}

			// MUST also seed DynamicComponentStorage; without it inspector renders fine but
			// Entity.HasNativeComponent<T>()/GetRef<T>() sees nothing in the ECS pool.
			bool storageAdded = false;
			if (scriptEntry.IsNativeComponent) {
				auto& componentRegistry = SceneManager::GetComponentRegistry();
				if (const ComponentInfo* info = componentRegistry.FindBySerializedName(scriptEntry.ClassName)) {
					if (info->isDynamic && info->add && (!info->has || !info->has(entity))) {
						info->add(entity);
						storageAdded = true;
					}
				}
			}

			if (alreadyHadScript && !storageAdded) {
				return false;
			}

			scene.MarkDirty();
			return true;
		}

		bool AttachManagedComponentToEntity(Entity entity, Scene& scene, const EditorScriptDiscovery::ScriptEntry& scriptEntry)
		{
			if (scriptEntry.ClassName.empty() || !scriptEntry.IsManagedComponent) {
				return false;
			}

			// Same teardown-race guard as AttachScriptToEntity above.
			if (!entity.IsValid() || !scene.IsValid(entity.GetHandle())) {
				return false;
			}

			if (!entity.HasComponent<ScriptComponent>()) {
				entity.AddComponent<ScriptComponent>();
			}

			auto& scriptComponent = entity.GetComponent<ScriptComponent>();
			if (scriptComponent.HasManagedComponent(scriptEntry.ClassName)) {
				return false;
			}

			scriptComponent.AddManagedComponent(scriptEntry.ClassName);
			scene.MarkDirty();
			return true;
		}

	} // namespace

	void RenderAddComponentPopup(
		const char* popupId,
		Scene& scene,
		std::span<const Entity> entities,
		char* searchBuffer,
		std::size_t searchBufferSize,
		bool* outChanged,
		const std::function<void()>& onBeforeChange)
	{
		// Local helper: set the caller's "did we change anything" flag.
		// Cheaper than threading the pointer through every add site.
		auto markChanged = [&]() {
			if (outChanged) *outChanged = true;
		};
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float availableH = viewport ? viewport->WorkSize.y : 800.0f;
		const float maxHeight = availableH * 0.7f;
		const float minHeight = std::min(maxHeight, std::max(360.0f, availableH * 0.45f));
		ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, minHeight),
			ImVec2(460.0f, maxHeight));

		if (!ImGui::BeginPopup(popupId)) {
			return;
		}

		// Defensive: an empty selection would make every "missing from any"
		// check vacuously true and offer the entire registry. The caller
		// shouldn't open the popup in that case, but be lenient.
		if (entities.empty()) {
			EditorUI::EmptyState("No entity selected.", "Select an entity to add components to it.");
			ImGui::EndPopup();
			return;
		}

		const ComponentRegistry& registry = SceneManager::GetComponentRegistry();

		EditorUI::SearchField("##CompSearch", searchBuffer, searchBufferSize, "Search components...");
		ImGui::Separator();

		ImGui::BeginChild("##AddComponentScroll", ImVec2(0, 0), false,
			ImGuiWindowFlags_HorizontalScrollbar);

		std::string filter(searchBuffer);
		std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);

		const bool hasFilter = !filter.empty();

		// The ScriptComponent list is the authored source of truth for managed components.
		auto entityHasComponent = [&](const ComponentInfo& info, const Entity& e) -> bool {
			if (info.isDynamic) {
				if (!e.HasComponent<ScriptComponent>()) return false;
				const ScriptComponent& sc = e.GetComponent<ScriptComponent>();
				return sc.HasScript(info.serializedName) || sc.HasManagedComponent(info.serializedName);
			}
			return info.has && info.has(e);
		};
		auto componentPresentOnAll = [&](const ComponentInfo& info) -> bool {
			if (entities.empty()) return false;
			for (const Entity& e : entities) {
				if (!entityHasComponent(info, e)) return false;
			}
			return true;
		};

		// nativeScriptByClassName MUST be defined before addComponentToAll lambda captures it.
		std::vector<EditorScriptDiscovery::ScriptEntry> scriptEntries;
		EditorScriptDiscovery::CollectProjectScriptEntries(scriptEntries);

		// Maps C++ display name → hand-authored mirror .cs entry so we can suppress duplicates in regular
		// subcategory buckets and render the "Native Components (C#)" tree under the C++ display name.
		// isDynamic components are NOT mirrors — their .cs IS the source of truth and routes by Subcategory.
		std::unordered_map<std::string, const EditorScriptDiscovery::ScriptEntry*> nativeBackingByDisplayName;
		for (const auto& scriptEntry : scriptEntries) {
			if (!scriptEntry.IsNativeComponent || scriptEntry.NativeName.empty()) continue;
			nativeBackingByDisplayName.emplace(scriptEntry.NativeName, &scriptEntry);
		}
		const auto isHandMirror = [&](const ComponentInfo& info) {
			if (info.isDynamic) return false; // dynamic components route by Subcategory
			return nativeBackingByDisplayName.find(info.displayName) != nativeBackingByDisplayName.end();
		};

		// Maps IComponent class name → script entry so addComponentToAll can route dynamic components
		// through the script-attach path (DynamicComponentRegistrar has no drawInspector/properties).
		std::unordered_map<std::string, const EditorScriptDiscovery::ScriptEntry*> nativeScriptByClassName;
		for (const auto& scriptEntry : scriptEntries) {
			if (!scriptEntry.IsNativeComponent) continue;
			if (scriptEntry.ClassName.empty()) continue;
			nativeScriptByClassName.emplace(scriptEntry.ClassName, &scriptEntry);
		}

		auto addComponentToAll = [&](const ComponentInfo& info) {
			if (onBeforeChange) onBeforeChange();
			bool added = false;
			if (info.isDynamic) {
				auto scriptIt = nativeScriptByClassName.find(info.displayName);
				if (scriptIt != nativeScriptByClassName.end() && scriptIt->second) {
					const auto& scriptEntry = *scriptIt->second;
					for (const Entity& e : entities) {
						if (AttachScriptToEntity(const_cast<Entity&>(e), scene, scriptEntry)) {
							added = true;
						}
					}
					if (added) markChanged();
					return;
				}
				for (const Entity& e : entities) {
					if (!info.has(e)) {
						info.add(e);
						added = true;
					}
				}
				if (added) {
					scene.MarkDirty();
					markChanged();
				}
				return;
			}
			for (const Entity& e : entities) {
				if (!info.has(e)) {
					registry.AddWithDependencies(e, info.typeId);
					added = true;
				}
			}
			if (added) {
				scene.MarkDirty();
				markChanged();
			}
		};

		auto componentConflictsWithSelection = [&](const std::type_index& proposed,
			std::string* outConflictName) -> bool
		{
			for (const Entity& e : entities) {
				if (registry.HasConflict(e, proposed)) {
					if (outConflictName) {
						registry.ForEachComponentInfo([&](const std::type_index& id, const ComponentInfo& info) {
							if (!outConflictName->empty()) return;
							if (id == proposed || !info.has || !info.has(e)) return;
							if (registry.TypesConflict(id, proposed)) *outConflictName = info.displayName;
						});
					}
					return true;
				}
			}
			return false;
		};

		// Render one component as a menu item. Already-present components are shown
		// DISABLED (greyed) rather than hidden, so the user can see what the entity
		// already has; conflicting components are likewise disabled with a reason.
		auto renderComponentItem = [&](const std::type_index& typeId, const ComponentInfo& info) {
			const bool alreadyAdded = componentPresentOnAll(info);
			std::string conflictName;
			const bool conflicts = !alreadyAdded && componentConflictsWithSelection(typeId, &conflictName);
			const bool enabled = !alreadyAdded && !conflicts;
			if (ImGuiUtils::IconMenuItemEllipsis(info.displayName, info.displayName.c_str(), info.icon, enabled, 260.0f)) {
				if (enabled) {
					addComponentToAll(info);
					ImGui::CloseCurrentPopup();
				}
			}
			if (!enabled) {
				if (alreadyAdded) {
					EditorUI::Tooltip("Already added");
				}
				else {
					const std::string conflictTooltip = "Conflicts with " + conflictName;
					EditorUI::Tooltip(conflictTooltip.c_str());
				}
			}
		};

		if (hasFilter) {
			// Flat filtered list when searching across both built-in components
			// and project scripts.
			registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
				if (info.category != ComponentCategory::Component || !info.IsModuleActive()) return;
				if (info.displayName == "Scripts") return;
				if (isHandMirror(info)) return;

				std::string lowerName = info.displayName;
				std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
				if (lowerName.find(filter) == std::string::npos) return;

				renderComponentItem(typeId, info);
			});

			registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
				if (info.category != ComponentCategory::Component || !info.IsModuleActive()) return;
				if (info.displayName == "Scripts") return;
				if (!isHandMirror(info)) return;

				std::string lowerName = info.displayName;
				std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
				if (lowerName.find(filter) == std::string::npos) return;

				renderComponentItem(typeId, info);
			});

			for (const auto& scriptEntry : scriptEntries) {
				if (scriptEntry.IsSceneSystem || scriptEntry.IsGlobalSystem) {
					continue;
				}
				if (scriptEntry.IsNativeComponent) {
					continue;
				}
				std::string lowerClassName = EditorScriptDiscovery::ToLowerCopy(scriptEntry.ClassName);
				std::string lowerPath = EditorScriptDiscovery::ToLowerCopy(scriptEntry.Path.string());
				if (lowerClassName.find(filter) == std::string::npos
					&& lowerPath.find(filter) == std::string::npos) {
					continue;
				}

				const std::string label = BuildScriptMenuLabel(scriptEntry);
				const std::string id = EditorScriptDiscovery::EntryId(scriptEntry);
				if (ImGuiUtils::IconMenuItemEllipsis(label, id.c_str(), "script", true, 260.0f)) {
					bool added = false;
					for (const Entity& e : entities) {
						if (scriptEntry.IsManagedComponent) {
							added |= AttachManagedComponentToEntity(const_cast<Entity&>(e), scene, scriptEntry);
						}
						else {
							added |= AttachScriptToEntity(const_cast<Entity&>(e), scene, scriptEntry);
						}
					}
					if (added) markChanged();
					ImGui::CloseCurrentPopup();
				}
			}
		}
		else {
			// Categorized tree view. Subcategory order is fixed so users learn
			// muscle memory; unknown subcategories append after the known ones.
			struct CategoryEntry { std::type_index TypeId; const ComponentInfo* Info; };
			std::vector<std::pair<std::string, std::vector<CategoryEntry>>> categories;
			std::unordered_map<std::string, size_t> categoryIndex;

			const std::vector<std::string> subcategoryOrder = {
				"General", "Rendering", "Physics", "Audio"
			};
			for (const auto& sub : subcategoryOrder) {
				categoryIndex[sub] = categories.size();
				categories.emplace_back(sub, std::vector<CategoryEntry>{});
			}

			registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
				if (info.category != ComponentCategory::Component || !info.IsModuleActive()) return;
				if (info.displayName == "Scripts") return;
				if (isHandMirror(info)) return;

				std::string sub = info.subcategory.empty() ? "General" : info.subcategory;
				auto it = categoryIndex.find(sub);
				if (it == categoryIndex.end()) {
					categoryIndex[sub] = categories.size();
					categories.emplace_back(sub, std::vector<CategoryEntry>{});
					it = categoryIndex.find(sub);
				}
				categories[it->second].second.push_back({ typeId, &info });
			});

			for (const auto& [subcategory, components] : categories) {
				if (components.empty()) continue;

				if (ImGui::TreeNode(subcategory.c_str())) {
					for (const auto& entry : components) {
						renderComponentItem(entry.TypeId, *entry.Info);
					}
					ImGui::TreePop();
				}
			}

			// Native Components (C#) — C# IComponent structs mirroring a C++ pool; clicks route through
			// AddWithDependencies (NOT AddManagedComponent) since data lives in the C++ ECS pool.
			struct NativeEntry { std::type_index TypeId; const ComponentInfo* Info; };
			std::vector<NativeEntry> nativeBackedComponents;
			registry.ForEachComponentInfo([&](const std::type_index& typeId, const ComponentInfo& info) {
				if (info.category != ComponentCategory::Component || !info.IsModuleActive()) return;
				if (info.displayName == "Scripts") return;
				if (!isHandMirror(info)) return;
				nativeBackedComponents.push_back({ typeId, &info });
			});
			if (!nativeBackedComponents.empty()) {
				if (ImGui::TreeNode("Native Components (C#)")) {
					for (const auto& entry : nativeBackedComponents) {
						renderComponentItem(entry.TypeId, *entry.Info);
					}
					ImGui::TreePop();
				}
			}

			bool hasManagedComponents = false;
			bool hasScripts = false;
			for (const auto& scriptEntry : scriptEntries) {
				if (scriptEntry.IsPackage() || scriptEntry.IsSceneSystem || scriptEntry.IsGlobalSystem) {
					continue;
				}
				hasManagedComponents |= scriptEntry.IsManagedComponent;
				hasScripts |= !scriptEntry.IsManagedComponent && !scriptEntry.IsNativeComponent;
			}
			if (hasManagedComponents) {
				if (ImGui::TreeNode("Components (C#)")) {
					for (const auto& scriptEntry : scriptEntries) {
						if (!scriptEntry.IsManagedComponent || scriptEntry.IsSceneSystem || scriptEntry.IsGlobalSystem
							|| scriptEntry.IsPackage()) {
							continue;
						}
						bool presentOnAll = !entities.empty();
						for (const Entity& e : entities) {
							if (!e.HasComponent<ScriptComponent>()
								|| !e.GetComponent<ScriptComponent>().HasManagedComponent(scriptEntry.ClassName)) {
								presentOnAll = false;
								break;
							}
						}
						const std::string label = BuildScriptMenuLabel(scriptEntry);
						const std::string path = scriptEntry.Path.string();
						if (ImGuiUtils::IconMenuItemEllipsis(label, path.c_str(), "script", !presentOnAll, 260.0f)) {
							if (!presentOnAll) {
								bool added = false;
								for (const Entity& e : entities) {
									added |= AttachManagedComponentToEntity(const_cast<Entity&>(e), scene, scriptEntry);
								}
								if (added) markChanged();
								ImGui::CloseCurrentPopup();
							}
						}
						if (presentOnAll) {
							EditorUI::Tooltip("Already added");
						}
					}
					ImGui::TreePop();
				}
			}

			if (hasScripts) {
				if (ImGui::TreeNode("Scripts")) {
					for (const auto& scriptEntry : scriptEntries) {
						if (scriptEntry.IsManagedComponent || scriptEntry.IsNativeComponent
							|| scriptEntry.IsSceneSystem || scriptEntry.IsGlobalSystem || scriptEntry.IsPackage()) {
							continue;
						}
						const std::string label = BuildScriptMenuLabel(scriptEntry);
						const std::string path = scriptEntry.Path.string();
						if (ImGuiUtils::IconMenuItemEllipsis(label, path.c_str(), "script", true, 260.0f)) {
							bool added = false;
							for (const Entity& e : entities) {
								added |= AttachScriptToEntity(const_cast<Entity&>(e), scene, scriptEntry);
							}
							if (added) markChanged();
							ImGui::CloseCurrentPopup();
						}
					}
					ImGui::TreePop();
				}
			}

			// One sub-tree per package; items show the class name, the scene stores ClassName.
			std::map<std::string, std::vector<const EditorScriptDiscovery::ScriptEntry*>> packages;
			for (const auto& scriptEntry : scriptEntries) {
				if (scriptEntry.IsPackage() && !scriptEntry.IsSceneSystem && !scriptEntry.IsGlobalSystem) {
					packages[scriptEntry.Package].push_back(&scriptEntry);
				}
			}
			if (!packages.empty() && ImGui::TreeNode("Packages")) {
				for (const auto& [package, packageEntries] : packages) {
					if (!ImGui::TreeNode(package.c_str(), "%s", EditorScriptDiscovery::PackageDisplayName(package).c_str())) {
						continue;
					}
					for (const EditorScriptDiscovery::ScriptEntry* scriptEntry : packageEntries) {
						bool presentOnAll = true;
						for (const Entity& e : entities) {
							const ScriptComponent* sc = e.HasComponent<ScriptComponent>() ? &e.GetComponent<ScriptComponent>() : nullptr;
							const bool present = sc && (scriptEntry->IsManagedComponent
								? sc->HasManagedComponent(scriptEntry->ClassName)
								: sc->HasScript(scriptEntry->ClassName, scriptEntry->Type));
							if (!present) {
								presentOnAll = false;
								break;
							}
						}
						const std::string id = EditorScriptDiscovery::EntryId(*scriptEntry);
						if (ImGuiUtils::IconMenuItemEllipsis(EditorScriptDiscovery::ShortClassName(scriptEntry->ClassName),
							id.c_str(), "script", !presentOnAll, 260.0f) && !presentOnAll) {
							bool added = false;
							for (const Entity& e : entities) {
								added |= scriptEntry->IsManagedComponent
									? AttachManagedComponentToEntity(const_cast<Entity&>(e), scene, *scriptEntry)
									: AttachScriptToEntity(const_cast<Entity&>(e), scene, *scriptEntry);
							}
							if (added) markChanged();
							ImGui::CloseCurrentPopup();
						}
						EditorUI::Tooltip(presentOnAll ? "Already added" : scriptEntry->ClassName.c_str());
					}
					ImGui::TreePop();
				}
				ImGui::TreePop();
			}
		}

		ImGui::EndChild();
		ImGui::EndPopup();
	}

}
