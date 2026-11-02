#include <pch.hpp>
#include "Bridge/EditorSceneEdit.hpp"

#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Inspector/PropertyJson.hpp"
#include "Inspector/PropertyValidation.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptComponentInspector.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Undo/StructureEditCommand.hpp"
#include "Undo/UndoStack.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace Funke::EditorSceneEdit {
	 namespace {
		std::string Text(const Json::Value& object, std::string_view key) {
			const auto* value = object.FindMember(key);
			return value ? value->AsStringOr() : std::string();
		}
		Json::Value Fail(std::string code, std::string message, size_t index) {
			Json::Value result = Json::Value::MakeObject();
			result.AddMember("ok", false);
			result.AddMember("code", std::move(code));
			result.AddMember("error", std::move(message));
			result.AddMember("operationIndex", static_cast<uint64_t>(index));
			return result;
		}
		std::string Key(const Json::Value* reference) {
			if (!reference || !reference->IsObject()) return {};
			const std::string local = Text(*reference, "localId"), id = Text(*reference, "id");
			if (!local.empty() && !id.empty()) return {};
			return local.empty() ? id : "@" + local;
		}
		struct PlannedEntity {
			Entity Existing = Entity::Null;
			std::string Parent;
			std::vector<const ComponentInfo*> Components;
			bool Deleted = false;
			std::set<std::string> Scripts;
		};
		struct Operation {
			std::string Kind, EntityKey, ParentKey, Name, Component, Property;
			std::string ReferenceKey;
			PropertyValue Value;
			int Sibling = -1;
		};
		std::vector<EntityHandle> Roots(Scene& scene) {
			std::vector<EntityHandle> result;
			for (auto handle : scene.GetRegistry().view<EntityMetaDataComponent>()) if (!scene.GetEntity(handle).HasParent()) result.push_back(handle);
			return result;
		}
		const PropertyDescriptor* FindProperty(const std::vector<PropertyDescriptor>& properties, std::string_view path, const Entity* entity = nullptr) {
			for (const auto& property : properties) {
				if (property.Name == path) return &property;
				const std::string prefix = property.Name + ".";
				if (!entity || !entity->IsValid() || !property.Get || !path.starts_with(prefix)) continue;
				if (property.Metadata.EnabledIfFn && !property.Metadata.EnabledIfFn(*entity)) return nullptr;
				const auto tag = property.Get(*entity).IntValue;
				for (const auto& branch : property.VariantBranches) if (branch.TagValue == tag)
					return FindProperty(branch.Properties, path.substr(prefix.size()), entity);
			}
			return nullptr;
		}
		PropertyDescriptor ResolveProperty(Entity entity, const std::string& component, const std::string& path) {
			if (component.starts_with("script:")) {
				for (const auto& section : GetScriptInspectionProperties(entity)) if (section.Name == component) {
					if (const auto* property = FindProperty(section.Properties, path)) return *property;
				}
			}
			else if (const auto* info = SceneManager::GetComponentRegistryOrBuiltIn().FindBySerializedName(component)) {
				if (const auto* property = FindProperty(info->properties, path, &entity)) return *property;
			}
			throw std::runtime_error("Property descriptor is no longer available");
		}
		void Reorder(Scene& scene, Entity entity, Entity parent, int index) {
			entity.SetParent(parent);
			if (index < 0) return;
			if (parent.IsValid()) {
				auto& children = parent.GetComponent<HierarchyComponent>().Children;
				std::erase(children, entity.GetHandle());
				children.insert(children.begin() + std::min(static_cast<size_t>(index), children.size()), entity.GetHandle());
			}
			else {
				auto roots = Roots(scene);
				std::reverse(roots.begin(), roots.end());
				std::erase(roots, entity.GetHandle());
				roots.insert(roots.begin() + std::min(static_cast<size_t>(index), roots.size()), entity.GetHandle());
				std::map<EntityHandle, size_t> positions;
				for (size_t i = 0; i < roots.size(); ++i) positions[roots[i]] = i;
				for (auto handle : scene.GetRegistry().view<EntityMetaDataComponent>()) positions.try_emplace(handle, positions.size());
				scene.GetRegistry().storage<EntityHandle>().sort([&](auto a, auto b) { return positions.at(a) > positions.at(b); });
			}
			scene.MarkHierarchyStructureDirty();
		}
	}

	Json::Value Apply(Scene& scene, UndoStack& history, const Json::Value& request, const std::function<void()>& beforeApply) {
		const auto* operations = request.FindMember("operations");
		if (!operations || !operations->IsArray() || operations->GetArray().empty() || operations->GetArray().size() > 128) return Fail("invalid_arguments", "Expected 1 to 128 operations", 0);
		const auto* dry = request.FindMember("dryRun");
		if (dry && !dry->IsBool()) return Fail("invalid_arguments", "dryRun must be a boolean", 0);
		const bool dryRun = dry && dry->AsBoolOr(false);
		if (operations->GetArray().size() == 1) {
			const std::string op = Text(operations->GetArray()[0], "op");
			if (op == "history.undo" || op == "history.redo") {
				if (!dryRun && beforeApply) beforeApply();
				const bool redo = op == "history.redo";
				const bool available = redo ? history.CanRedo() : history.CanUndo();
				Json::Value result = Json::Value::MakeObject();
				result.AddMember("ok", true);
				result.AddMember("changed", available && !dryRun);
				result.AddMember("dryRun", dryRun);
				if (available && !dryRun) {
					const std::string label = redo ? history.Redo(scene) : history.Undo(scene);
					if (label.empty()) return Fail("history_failed", "History restoration failed; inspect the scene before continuing", 0);
					result.AddMember("label", label);
				}
				return result;
			}
		}
		auto& registry = SceneManager::GetComponentRegistryOrBuiltIn();
		std::map<std::string, PlannedEntity> entities;
		for (auto handle : scene.GetRegistry().view<EntityMetaDataComponent>()) {
			Entity entity = scene.GetEntity(handle);
			const Entity parent = entity.GetParent();
			PlannedEntity planned{ entity, parent.IsValid() ? std::to_string(scene.GetEntityPersistentID(parent.GetHandle())) : std::string(), {}, false, {} };
			registry.ForEachComponentInfo([&](auto, const ComponentInfo& info) { if (info.has && info.has(entity)) planned.Components.push_back(&info); });
			if (entity.HasComponent<ScriptComponent>()) for (const auto& script : entity.GetComponent<ScriptComponent>().Scripts) planned.Scripts.insert("script:" + script.GetClassName());
			entities.emplace(std::to_string(scene.GetEntityPersistentID(handle)), std::move(planned));
		}
		std::vector<Operation> plan;
		size_t index = 0;
		for (const auto& raw : operations->GetArray()) {
			Operation operation;
			operation.Kind = Text(raw, "op");
			operation.EntityKey = Key(raw.FindMember("entity"));
			operation.Component = Text(raw, "component");
			if (operation.Kind == "entity.create") {
				const std::string local = Text(raw, "localId");
				operation.EntityKey = "@" + local;
				operation.Name = Text(raw, "name");
				if (local.empty() || local.size() > 128 || operation.Name.size() > 4096 || entities.contains(operation.EntityKey)) return Fail("invalid_local_id", "localId must be unique and nonempty", index);
				entities.emplace(operation.EntityKey, PlannedEntity{});
			}
			else {
				auto found = entities.find(operation.EntityKey);
				if (found == entities.end() || found->second.Deleted) return Fail("entity_not_found", "Unknown or deleted entity in batch", index);
				auto& entity = found->second;
				if (operation.Kind == "entity.rename") {
					operation.Name = Text(raw, "name");
					if (operation.Name.size() > 4096 || !raw.FindMember("name") || !raw.FindMember("name")->IsString()) return Fail("invalid_arguments", "Expected a name string of at most 4096 bytes", index);
				}
				else if (operation.Kind == "entity.delete") {
					entity.Deleted = true;
					bool changed;
					do {
						changed = false;
						for (auto& [key, child] : entities) if (!child.Deleted && !child.Parent.empty() && entities.at(child.Parent).Deleted) { child.Deleted = true; changed = true; }
					} while (changed);
				}
				else if (operation.Kind == "entity.parent") {
					operation.ParentKey = Key(raw.FindMember("parent"));
					if (!operation.ParentKey.empty() && (!entities.contains(operation.ParentKey) || entities.at(operation.ParentKey).Deleted)) return Fail("entity_not_found", "Unknown parent", index);
					for (std::string ancestor = operation.ParentKey; !ancestor.empty(); ancestor = entities.at(ancestor).Parent) {
						if (ancestor == operation.EntityKey) return Fail("hierarchy_cycle", "Cannot parent an entity to its descendant", index);
					}
					if (const auto* sibling = raw.FindMember("siblingIndex")) {
						const double value = sibling->AsDoubleOr(-1);
						if (!sibling->IsNumber() || value < 0 || value > 1000000 || std::floor(value) != value) return Fail("invalid_arguments", "Invalid siblingIndex", index);
						operation.Sibling = static_cast<int>(value);
					}
					entity.Parent = operation.ParentKey;
				}
				else if (operation.Kind == "component.add" || operation.Kind == "component.remove") {
					if (operation.Component.starts_with("script:")) {
						if (operation.Kind == "component.add") {
							if (GetScriptClassProperties(operation.Component.substr(7)).empty()) return Fail("unsupported_operation", "Expected an inspectable managed script class", index);
							entity.Scripts.insert(operation.Component);
						}
						else if (entity.Scripts.erase(operation.Component) == 0) return Fail("component_not_found", "Script is not attached", index);
						plan.push_back(std::move(operation));
						++index;
						continue;
					}
					const auto* info = registry.FindBySerializedName(operation.Component);
					if (!info) return Fail("component_not_found", "Unknown registered component", index);
					static const std::set<std::string> legacyNames{ "RectTransform2D", "name", "TextRenderer", "Image", "AudioSource", "Rigidbody2D", "BoxCollider2D", "CircleCollider2D", "PolygonCollider2D" };
					const bool legacySerialization = legacyNames.contains(operation.Component);
					if (info->ownerId || info->isDynamic || (!legacySerialization && !info->serializeArchive && (!info->serialize || !info->deserialize))) return Fail("unsupported_operation", "Component does not have a supported built-in restoration path", index);
					if (operation.Kind == "component.add") {
						std::vector<uint32_t> additions;
						std::string error;
						if (!registry.PlanAddition(*info, entity.Components, additions, error)) return Fail("component_conflict", error, index);
						for (auto id : additions) {
							const auto* dependency = registry.GetByTypeId(id);
							if (std::find(entity.Components.begin(), entity.Components.end(), dependency) == entity.Components.end()) entity.Components.push_back(dependency);
						}
					}
					else {
						if (!info->remove || std::find(entity.Components.begin(), entity.Components.end(), info) == entity.Components.end()) return Fail("component_not_found", "Component is not present", index);
						for (const auto* dependent : entity.Components) if (dependent != info && std::find(dependent->dependsOn.begin(), dependent->dependsOn.end(), info->typeId) != dependent->dependsOn.end()) return Fail("component_required", "Another component requires this component", index);
						std::erase(entity.Components, info);
					}
				}
				else if (operation.Kind == "property.set") {
					operation.Property = Text(raw, "property");
					PropertyDescriptor descriptor;
					if (operation.Component.starts_with("script:")) {
						if (!entity.Scripts.contains(operation.Component)) return Fail("component_not_found", "Script is not present in the planned state", index);
						try {
							if (entity.Existing.IsValid() && entity.Existing.HasComponent<ScriptComponent>() && entity.Existing.GetComponent<ScriptComponent>().HasScript(operation.Component.substr(7))) descriptor = ResolveProperty(entity.Existing, operation.Component, operation.Property);
							else {
								const auto properties = GetScriptClassProperties(operation.Component.substr(7));
								const auto* property = FindProperty(properties, operation.Property);
								if (!property) return Fail("property_not_found", "Unknown script field", index);
								descriptor = *property;
							}
						}
						catch (...) { return Fail("property_not_found", "Unknown script field", index); }
					}
					else {
						const auto* info = registry.FindBySerializedName(operation.Component);
						if (!info || std::find(entity.Components.begin(), entity.Components.end(), info) == entity.Components.end()) return Fail("component_not_found", "Component is not present in the planned state", index);
						if (info->ownerId || info->isDynamic) return Fail("unsupported_operation", "Package callback restoration is not supported", index);
						const bool nested = operation.Property.find('.') != std::string::npos;
						if (nested && std::any_of(plan.begin(), plan.end(), [&](const auto& previous) { return previous.EntityKey == operation.EntityKey; })) return Fail("unsupported_operation", "Variant fields require a separate batch after earlier changes to this entity", index);
						const auto* property = FindProperty(info->properties, operation.Property, &entity.Existing);
						if (!property) return Fail("property_not_found", "Unknown property or inactive variant", index);
						descriptor = *property;
					}
					const auto* value = raw.FindMember("value");
					Json::Value resolved;
					if (value && value->FindMember("localId")) {
						if (descriptor.Type != PropertyType::EntityRef && descriptor.Type != PropertyType::ComponentRef) return Fail("invalid_reference_type", "localId requires an entity or component reference", index);
						operation.ReferenceKey = "@" + Text(*value, "localId");
						if (!entities.contains(operation.ReferenceKey) || entities.at(operation.ReferenceKey).Deleted) return Fail("invalid_reference", "Unknown local reference", index);
						resolved = *value;
						resolved.AddMember("id", "0");
						value = &resolved;
					}
					if (descriptor.Metadata.EnabledIfFn && std::any_of(plan.begin(), plan.end(), [&](const auto& previous) { return previous.EntityKey == operation.EntityKey; })) return Fail("unsupported_operation", "Conditional fields require a separate batch after earlier changes to this entity", index);
					std::string error;
					if (!value || !PropertyJson::Decode(descriptor.Type, *value, operation.Value, error)) return Fail(error.empty() ? "invalid_value" : error, "Invalid property value", index);
					if (!operation.ReferenceKey.empty() && operation.Value.Type == PropertyType::ComponentRef) {
						const auto& target = entities.at(operation.ReferenceKey);
						const auto found = std::find_if(target.Components.begin(), target.Components.end(), [&](const auto* component) { return component->serializedName == operation.Value.StringValue || component->displayName == operation.Value.StringValue; });
						if (found == target.Components.end()) return Fail("invalid_reference", "Local target lacks the requested component", index);
						if (!descriptor.Metadata.ComponentTypeName.empty() && descriptor.Metadata.ComponentTypeName != (*found)->serializedName && descriptor.Metadata.ComponentTypeName != (*found)->displayName) return Fail("invalid_reference_type", "Local target has the wrong component type", index);
					}
					if (!PropertyValidation::Validate(entity.Existing.IsValid() ? &entity.Existing : nullptr, descriptor, operation.Value, error)) return Fail(error, "Property validation failed", index);
				}
				else return Fail("unsupported_operation", "Unknown operation", index);
			}
			plan.push_back(std::move(operation));
			++index;
		}
		Json::Value result = Json::Value::MakeObject();
		for (const auto& operation : plan) {
			if (!operation.ReferenceKey.empty() && entities.at(operation.ReferenceKey).Deleted) return Fail("invalid_reference", "Referenced entity is deleted by this batch", 0);
			if ((operation.Value.Type == PropertyType::EntityRef && operation.Value.StringValue != "prefab") || operation.Value.Type == PropertyType::ComponentRef) {
				const auto target = entities.find(std::to_string(operation.Value.UIntValue));
				if (target != entities.end() && target->second.Deleted) return Fail("invalid_reference", "Referenced entity is deleted by this batch", 0);
			}
		}
		std::set<std::string> affected;
		for (const auto& operation : plan) for (const auto& key : { operation.EntityKey, operation.ParentKey }) {
			const auto found = entities.find(key);
			if (found == entities.end() || !found->second.Existing.IsValid()) continue;
			Entity root = found->second.Existing;
			while (root.HasParent()) root = root.GetParent();
			affected.insert(std::to_string(scene.GetEntityPersistentID(root.GetHandle())));
		}
		std::vector<EntityHandle> roots;
		size_t snapshotBytes = 0;
		for (const auto& key : affected) {
			const auto root = entities.at(key).Existing.GetHandle();
			const auto snapshot = EntitySubtreeSnapshot::Capture(scene, root);
			for (const auto& value : snapshot.Entities) snapshotBytes += Json::Stringify(value).size();
			if (snapshotBytes > 16 * 1024 * 1024) return Fail("batch_limit_exceeded", "Affected subtrees exceed the 16 MiB transaction snapshot limit", 0);
			for (const auto id : snapshot.EntityIds) {
				const auto& entity = entities.at(std::to_string(id));
				for (const auto* info : entity.Components) if (info->ownerId || info->isDynamic) return Fail("unsupported_operation", "An affected subtree contains a package component without guaranteed restoration", 0);
				if (entity.Existing.HasComponent<ScriptComponent>()) for (const auto& script : entity.Existing.GetComponent<ScriptComponent>().Scripts)
					if (script.GetType() == ScriptType::Native) return Fail("unsupported_operation", "Native script callback restoration is not supported in an affected subtree", 0);
			}
			roots.push_back(root);
		}
		result.AddMember("ok", true);
		result.AddMember("dryRun", dryRun);
		result.AddMember("operations", static_cast<uint64_t>(plan.size()));
		if (dryRun) return result;
		if (beforeApply) beforeApply();
		const bool dirty = scene.IsDirty(), uiDirty = scene.IsUIDirty();
		StructureEditRecorder recorder(scene, roots, "Agent scene batch");
		std::map<std::string, Entity> actual;
		for (const auto& [key, entity] : entities) if (entity.Existing.IsValid()) actual.emplace(key, entity.Existing);
		auto afterRoots = [&]() {
			std::vector<EntityHandle> handles;
			for (const auto& [key, entity] : actual) if ((affected.contains(key) || key.starts_with("@")) && entity.IsValid()) handles.push_back(entity.GetHandle());
			return handles;
		};
		Json::Value created = Json::Value::MakeObject();
		index = 0;
		try {
			for (const auto& operation : plan) {
				if (operation.Kind == "entity.create") {
					Entity entity = scene.CreateEmptyEntity();
					entity.AddComponent<NameComponent>(operation.Name);
					actual.insert_or_assign(operation.EntityKey, entity);
					created.AddMember(operation.EntityKey.substr(1), std::to_string(scene.GetEntityPersistentID(entity.GetHandle())));
				}
				else {
					Entity entity = actual.at(operation.EntityKey);
					if (!entity.IsValid()) throw std::runtime_error("Entity disappeared during application");
					if (operation.Kind == "entity.delete") scene.DestroyEntity(entity);
					else if (operation.Kind == "entity.rename") {
						if (!entity.HasComponent<NameComponent>()) entity.AddComponent<NameComponent>(operation.Name);
						else entity.GetComponent<NameComponent>().Name = operation.Name;
					}
					else if (operation.Kind == "entity.parent") Reorder(scene, entity, operation.ParentKey.empty() ? Entity::Null : actual.at(operation.ParentKey), operation.Sibling);
					else if (operation.Kind == "component.add") {
						if (operation.Component.starts_with("script:")) {
							if (!entity.HasComponent<ScriptComponent>()) entity.AddComponent<ScriptComponent>();
							entity.GetComponent<ScriptComponent>().AddScript(operation.Component.substr(7));
						}
						else {
							const auto* info = registry.FindBySerializedName(operation.Component);
							if (!info || !registry.AddWithDependencies(entity, *info)) throw std::runtime_error("Component addition failed");
						}
					}
					else if (operation.Kind == "component.remove") {
						if (operation.Component.starts_with("script:")) {
							auto& scripts = entity.GetComponent<ScriptComponent>().Scripts;
							const auto found = std::find_if(scripts.begin(), scripts.end(), [&](const auto& script) { return script.GetClassName() == operation.Component.substr(7); });
							if (found == scripts.end()) throw std::runtime_error("Script disappeared");
							if (!ScriptSystem::RemoveScript(entity, static_cast<size_t>(found - scripts.begin()))) throw std::runtime_error("Script removal failed");
						}
						else {
						const auto* info = registry.FindBySerializedName(operation.Component);
						if (!info || !info->remove) throw std::runtime_error("Component descriptor disappeared");
						info->remove(entity);
						if (info->has(entity)) throw std::runtime_error("Component removal failed");
						}
					}
					else {
						const auto descriptor = ResolveProperty(entity, operation.Component, operation.Property);
						PropertyValue value = operation.Value;
						if (!operation.ReferenceKey.empty()) value.UIntValue = scene.GetEntityPersistentID(actual.at(operation.ReferenceKey).GetHandle());
						std::string error;
						if (!PropertyValidation::Validate(&entity, descriptor, value, error)) throw std::runtime_error(error);
						descriptor.Set(entity, value);
						if (!descriptor.Get || descriptor.Get(entity) != value) throw std::runtime_error("Setter did not retain the requested value");
					}
				}
				scene.MarkDirty();
				++index;
			}
			roots = afterRoots();
			auto command = recorder.Finish(scene, roots);
			result.AddMember("changed", command != nullptr);
			if (command) history.Push(std::move(command));
			else { if (!dirty) scene.ClearDirty(); if (!uiDirty) scene.ClearUIDirty(); }
			result.AddMember("created", std::move(created));
			return result;
		}
		catch (const std::exception& error) {
			try {
				roots = afterRoots();
				if (auto rollback = recorder.Finish(scene, roots)) rollback->Undo(scene);
				if (!dirty) scene.ClearDirty();
				if (!uiDirty) scene.ClearUIDirty();
				return Fail("apply_failed", error.what(), index);
			}
			catch (const std::exception& rollbackError) {
				return Fail("rollback_failed", std::string(error.what()) + "; recovery failed: " + rollbackError.what(), index);
			}
		}
	}
}
