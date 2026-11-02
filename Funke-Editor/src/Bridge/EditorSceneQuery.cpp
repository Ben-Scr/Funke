#include <pch.hpp>
#include "Bridge/EditorSceneQuery.hpp"
#include "Bridge/EditorSceneEdit.hpp"

#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "DevTools/EditorBridge.hpp"
#include "Inspector/PropertyJson.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptComponentInspector.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Systems/ImGuiEditorLayer.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>

namespace Funke {

	namespace {
		std::string String(const Json::Value& object, std::string_view key) {
			const auto* value = object.FindMember(key);
			return value ? value->AsStringOr() : std::string();
		}

		Json::Value Error(std::string_view code, std::string_view message) {
			Json::Value result = Json::Value::MakeObject();
			result.AddMember("formatVersion", 1);
			result.AddMember("ok", false);
			result.AddMember("code", std::string(code));
			result.AddMember("error", std::string(message));
			return result;
		}

		std::vector<const ComponentInfo*> Components() {
			std::vector<const ComponentInfo*> result;
			SceneManager::GetComponentRegistry().ForEachComponentInfo([&](auto, const ComponentInfo& info) {
				if (!info.serializedName.empty()) result.push_back(&info);
			});
			std::sort(result.begin(), result.end(), [](auto* a, auto* b) { return a->serializedName < b->serializedName; });
			return result;
		}

		Json::Value PropertySchema(const PropertyDescriptor& property) {
			Json::Value result = Json::Value::MakeObject();
			result.AddMember("path", property.Name);
			result.AddMember("displayName", property.DisplayName);
			result.AddMember("type", std::string(ToString(property.Type)));
			const bool supported = PropertyJson::IsSupported(property.Type);
			result.AddMember("supported", supported);
			result.AddMember("readable", supported && static_cast<bool>(property.Get));
			result.AddMember("writable", supported && static_cast<bool>(property.Set) && !property.Metadata.ReadOnly);
			result.AddMember("conditional", static_cast<bool>(property.Metadata.EnabledIfFn));
			if (!supported) result.AddMember("reason", "unsupported_field_type");
			else if (property.Metadata.ReadOnly || !property.Set) result.AddMember("reason", "read_only_property");
			if (property.Metadata.HasClamp) {
				result.AddMember("minimum", property.Metadata.ClampMin);
				result.AddMember("maximum", property.Metadata.ClampMax);
			}
			if (property.Metadata.HasDragClamp) {
				result.AddMember("dragMinimum", property.Metadata.DragClampMin);
				result.AddMember("dragMaximum", property.Metadata.DragClampMax);
			}
			if (property.Metadata.Enum) {
				Json::Value options = Json::Value::MakeArray();
				for (const auto& option : property.Metadata.Enum->Options) {
					Json::Value item = Json::Value::MakeObject();
					item.AddMember("name", option.Name);
					item.AddMember("value", std::to_string(option.Value));
					options.Append(std::move(item));
				}
				result.AddMember("enumValues", std::move(options));
			}
			result.AddMember("componentType", property.Metadata.ComponentTypeName);
			result.AddMember("assetKind", static_cast<int>(property.Metadata.AssetKindFilter));
			result.AddMember("textureSlices", !property.Metadata.SuppressTextureSlices);
			Json::Value branches = Json::Value::MakeArray();
			for (const auto& branch : property.VariantBranches) {
				Json::Value item = Json::Value::MakeObject();
				item.AddMember("tag", std::to_string(branch.TagValue));
				Json::Value fields = Json::Value::MakeArray();
				for (const auto& nested : branch.Properties) fields.Append(PropertySchema(nested));
				item.AddMember("fields", std::move(fields));
				branches.Append(std::move(item));
			}
			result.AddMember("variants", std::move(branches));
			return result;
		}

		Json::Value ComponentSchema(const ComponentInfo& info, const std::vector<const ComponentInfo*>& all) {
			Json::Value result = Json::Value::MakeObject();
			result.AddMember("component", info.serializedName);
			result.AddMember("displayName", info.displayName);
			result.AddMember("moduleActive", info.IsModuleActive());
			result.AddMember("customDrawer", info.drawInspector != nullptr);
			Json::Value dependencies = Json::Value::MakeArray(), conflicts = Json::Value::MakeArray();
			for (const auto* other : all) {
				if (std::find(info.dependsOn.begin(), info.dependsOn.end(), other->typeId) != info.dependsOn.end()) dependencies.Append(other->serializedName);
				if (std::find(info.conflictsWith.begin(), info.conflictsWith.end(), other->typeId) != info.conflictsWith.end()
					|| std::find(other->conflictsWith.begin(), other->conflictsWith.end(), info.typeId) != other->conflictsWith.end()) conflicts.Append(other->serializedName);
			}
			result.AddMember("dependencies", std::move(dependencies));
			result.AddMember("conflicts", std::move(conflicts));
			Json::Value fields = Json::Value::MakeArray();
			for (const auto& property : info.properties) fields.Append(PropertySchema(property));
			result.AddMember("fields", std::move(fields));
			const std::string signature = std::to_string(SceneManager::GetComponentRegistry().GetGeneration()) + ":"
				+ std::to_string(ScriptSystem::GetCompletedRebuildCount()) + ":" + Json::Stringify(result);
			result.AddMember("schemaRevision", std::to_string(detail::FnvHash64(signature)));
			return result;
		}

		Json::Value ReadProperties(Entity entity, const std::vector<PropertyDescriptor>& properties, std::string_view selected,
			bool& found, const std::string& prefix = {}) {
			Json::Value fields = Json::Value::MakeArray();
			for (const auto& property : properties) {
				const std::string path = prefix + property.Name;
				const bool include = selected.empty() || selected == path;
				PropertyValue value;
				std::string error;
				Json::Value encoded;
				bool readable = false;
				if (include || !property.VariantBranches.empty()) {
					if (!PropertyJson::IsSupported(property.Type)) error = "unsupported_field_type";
					else if (!property.Get) error = "unreadable_property";
					else {
						value = property.Get(entity);
						readable = PropertyJson::Encode(value, encoded, error);
					}
				}
				if (include) {
					found = true;
					Json::Value field = PropertySchema(property);
					*field.FindMember("path") = Json::Value(path);
					field.AddMember("ok", readable);
					if (readable) field.AddMember("value", std::move(encoded));
					else field.AddMember("code", error);
					if (property.Metadata.EnabledIfFn && !property.Metadata.EnabledIfFn(entity)) {
						*field.FindMember("writable") = Json::Value(false);
						field.AddMember("disabledReason", "condition_not_met");
					}
					fields.Append(std::move(field));
				}
				if (readable) for (const auto& branch : property.VariantBranches) {
					if (value.IntValue != branch.TagValue) continue;
					auto nested = ReadProperties(entity, branch.Properties, selected, found, path + ".");
					for (auto& field : nested.GetArray()) fields.Append(std::move(field));
				}
			}
			return fields;
		}
	}

	EditorSceneQuery::EditorSceneQuery() : m_SessionId(EditorBridge::GenerateToken()) {}

	std::string EditorSceneQuery::SchemaRevision() const {
		const auto all = Components();
		std::string signature;
		for (const auto* info : all) signature += String(ComponentSchema(*info, all), "schemaRevision") + ":";
		return std::to_string(detail::FnvHash64(signature));
	}

	void EditorSceneQuery::Refresh() {
		const bool playing = Application::GetIsPlaying();
		const auto loaded = SceneManager::GetLoadedScenes();
		for (auto it = m_Instances.begin(); it != m_Instances.end();) {
			const bool present = std::any_of(loaded.begin(), loaded.end(), [&](const auto& weak) {
				return !weak.owner_before(it->first) && !it->first.owner_before(weak);
			});
			if (it->first.expired() || !present) it = m_Instances.erase(it);
			else {
				const auto generation = it->first.lock()->GetContentGeneration();
				if (playing != m_Playing || generation != it->second.ContentGeneration) it->second.Generation = std::to_string(m_NextId++);
				it->second.ContentGeneration = generation;
				++it;
			}
		}
		m_Playing = playing;
		for (const auto& weak : loaded) {
			if (!weak.expired() && !m_Instances.contains(weak)) {
				const std::string id = std::to_string(m_NextId++);
				m_Instances.emplace(weak, Instance{ id, id, weak.lock()->GetContentGeneration() });
			}
		}
	}

	Json::Value EditorSceneQuery::Describe(Scene& scene, const Instance& instance) const {
		Json::Value result = Json::Value::MakeObject();
		result.AddMember("formatVersion", 1);
		result.AddMember("ok", true);
		result.AddMember("editorSessionId", m_SessionId);
		result.AddMember("sceneId", instance.Id);
		result.AddMember("sceneInstanceId", instance.Generation);
		result.AddMember("context", m_Playing ? "runtime" : "edit");
		result.AddMember("name", scene.GetName());
		result.AddMember("revision", std::to_string(scene.GetRevision()));
		result.AddMember("schemaRevision", SchemaRevision());
		result.AddMember("dirty", scene.IsDirty());
		result.AddMember("frame", std::to_string(Time::GetFrameCount()));
		result.AddMember("gameTime", static_cast<double>(Time::GetTimeSinceStartup()));
		result.AddMember("consistency", "single_response");
		return result;
	}

	Json::Value EditorSceneQuery::Execute(std::string_view command, const Json::Value& args) {
		Refresh();
		if (!args.IsObject() || Json::Stringify(args).size() > 32768) return Error("invalid_arguments", "Expected an object of at most 32768 bytes");
		if (command == "apply-status") {
			if (String(args, "editorSessionId") != m_SessionId) return Error("stale_editor_session", "Query the current scene state");
			const auto found = m_Requests.find(String(args, "requestId"));
			if (found == m_Requests.end()) return Error("outcome_unknown", "Request outcome is not known in this editor session");
			if (std::chrono::steady_clock::now() - found->second.Completed > std::chrono::minutes(10)) return Error("outcome_expired", "Result retention expired; query the current scene state");
			return found->second.Result;
		}
		if (command == "scenes") {
			Json::Value result = Json::Value::MakeObject(), scenes = Json::Value::MakeArray();
			result.AddMember("formatVersion", 1);
			result.AddMember("ok", true);
			for (const auto& [weak, instance] : m_Instances) if (const auto scene = weak.lock()) scenes.Append(Describe(*scene, instance));
			result.AddMember("scenes", std::move(scenes));
			return result;
		}
		const auto all = Components();
		const std::string component = String(args, "component");
		const ComponentInfo* info = nullptr;
		if (!component.empty()) {
			for (const auto* candidate : all) if (candidate->serializedName == component) info = candidate;
		}
		if (command == "schema" && !component.starts_with("script:") && !component.starts_with("managed:")) {
			if (!info) return Error("component_not_found", "Use the serialized component name from entities");
			Json::Value result = ComponentSchema(*info, all);
			result.AddMember("formatVersion", 1);
			result.AddMember("ok", true);
			return result;
		}
		const std::string sceneId = String(args, "sceneId");
		for (const auto& [weak, instance] : m_Instances) {
			if (instance.Id != sceneId) continue;
			const auto scene = weak.lock();
			if (!scene) break;
			if ((!String(args, "editorSessionId").empty() && String(args, "editorSessionId") != m_SessionId)
				|| (!String(args, "sceneInstanceId").empty() && String(args, "sceneInstanceId") != instance.Generation)) {
				return Error("stale_scene_instance", "Query scenes again after an editor restart, scene reload or Play/Stop");
			}
			if (!String(args, "context").empty() && String(args, "context") != (m_Playing ? "runtime" : "edit")) {
				return Error("context_mismatch", "Requested scene context is not loaded");
			}
			Json::Value result = Describe(*scene, instance);
			std::vector<std::pair<uint64_t, Entity>> entities;
			for (auto handle : scene->GetRegistry().view<EntityMetaDataComponent>()) {
				entities.emplace_back(scene->GetEntityPersistentID(handle), scene->GetEntity(handle));
			}
			std::sort(entities.begin(), entities.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			if (command == "entities") {
				if (!component.empty() && !info) return Error("component_not_found", "Unknown component filter");
				int limit = 100;
				if (const auto* requested = args.FindMember("limit")) {
					const double n = requested->AsDoubleOr(-1);
					if (!requested->IsNumber() || n < 1 || n > 500 || std::floor(n) != n) return Error("invalid_arguments", "limit must be an integer from 1 to 500");
					limit = static_cast<int>(n);
				}
				const std::string cursor = String(args, "cursor");
				const std::string query = String(args, "name") + "|" + component + "|" + String(args, "parentId");
				const std::string prefix = m_SessionId + ":" + instance.Generation + ":" + std::to_string(scene->GetRevision()) + ":"
					+ std::to_string(detail::FnvHash64(query)) + ":";
				uint64_t after = 0;
				if (!cursor.empty()) {
					if (!cursor.starts_with(prefix)) return Error("stale_cursor", "Scene or query changed; restart pagination");
					const auto suffix = std::string_view(cursor).substr(prefix.size());
					const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), after);
					if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size()) return Error("invalid_cursor", "Invalid cursor");
				}
				Json::Value rows = Json::Value::MakeArray();
				uint64_t last = 0;
				bool more = false;
				for (auto& [id, entity] : entities) {
					if (id <= after) continue;
					if (!String(args, "name").empty() && entity.GetName().find(String(args, "name")) == std::string::npos) continue;
					if (info && (!info->has || !info->has(entity))) continue;
					const Entity parent = entity.GetParent();
					const std::string parentId = parent.IsValid() ? std::to_string(scene->GetEntityPersistentID(parent.GetHandle())) : "0";
					if (!String(args, "parentId").empty() && parentId != String(args, "parentId")) continue;
					if (rows.GetArray().size() >= static_cast<size_t>(limit)) { more = true; break; }
					Json::Value row = Json::Value::MakeObject(), components = Json::Value::MakeArray();
					row.AddMember("id", std::to_string(id));
					row.AddMember("name", entity.GetName());
					row.AddMember("parentId", parentId);
					for (const auto* type : all) if (type->has && type->has(entity)) components.Append(type->serializedName);
					if (entity.HasComponent<ScriptComponent>()) for (const auto& script : entity.GetComponent<ScriptComponent>().Scripts) components.Append("script:" + script.GetClassName());
					if (entity.HasComponent<ScriptComponent>()) for (const auto& name : entity.GetComponent<ScriptComponent>().ManagedComponents) components.Append("managed:" + name);
					row.AddMember("components", std::move(components));
					rows.Append(std::move(row));
					last = id;
				}
				result.AddMember("entities", std::move(rows));
				result.AddMember("nextCursor", more ? prefix + std::to_string(last) : std::string());
				return result;
			}
			if (command == "inspect" || command == "schema") {
				for (auto& [id, entity] : entities) {
					if (std::to_string(id) != String(args, "entityId")) continue;
					result.AddMember("entityId", std::to_string(id));
					Json::Value components = Json::Value::MakeArray();
					bool found = false, propertyFound = String(args, "property").empty();
					for (const auto* type : all) {
						if (!component.empty() && type->serializedName != component) continue;
						if (!type->has || !type->has(entity)) continue;
						found = true;
						Json::Value data = ComponentSchema(*type, all);
						data.AddMember("values", ReadProperties(entity, type->properties, String(args, "property"), propertyFound));
						components.Append(std::move(data));
					}
					for (auto& script : GetScriptInspectionProperties(entity)) {
						if (!component.empty() && script.Name != component) continue;
						found = true;
						Json::Value data = Json::Value::MakeObject();
						data.AddMember("component", script.Name);
						Json::Value fields = Json::Value::MakeArray();
						for (const auto& property : script.Properties) fields.Append(PropertySchema(property));
						data.AddMember("fields", std::move(fields));
						data.AddMember("schemaRevision", std::to_string(detail::FnvHash64(Json::Stringify(data)
							+ ":" + std::to_string(ScriptSystem::GetCompletedRebuildCount()))));
						data.AddMember("values", ReadProperties(entity, script.Properties, String(args, "property"), propertyFound));
						components.Append(std::move(data));
					}
					if (!found) return Error("component_not_found", "Component is not present on this entity");
					if (!propertyFound) return Error("property_not_found", "Property path is unknown or its variant is inactive");
					for (auto& data : components.GetArray()) for (auto& field : data.FindMember("values")->GetArray()) {
						if (m_Playing) {
							*field.FindMember("writable") = Json::Value(false);
							field.AddMember("contextReadOnly", true);
						}
						if (!String(args, "property").empty() && !field.FindMember("ok")->AsBoolOr(false)) {
							return Error(String(field, "code"), "Requested field cannot be read");
						}
					}
					result.AddMember("components", std::move(components));
					if (Json::Stringify(result).size() > 1024 * 1024) return Error("query_limit_exceeded", "Select one component or property");
					return result;
				}
				return Error("entity_not_found", "Entity ID is not present in this scene");
			}
		}
		return Error("scene_not_found", "Pass an explicit sceneId from scenes");
	}

	Json::Value EditorSceneQuery::Apply(const Json::Value& args, ImGuiEditorLayer& editor) {
		Refresh();
		const auto* target = args.FindMember("target");
		if (!target || !target->IsObject() || String(*target, "editorSessionId") != m_SessionId) return Error("stale_editor_session", "Pass the current editorSessionId");
		const auto* version = args.FindMember("schemaVersion");
		if (!version || !version->IsNumber() || version->AsDoubleOr(0) != 1) return Error("invalid_format_version", "schemaVersion must be 1");
		const std::string id = String(args, "requestId");
		const std::string payload = Json::Stringify(args);
		if (id.empty() || id.size() > 128 || payload.size() > 1024 * 1024) return Error("invalid_arguments", "Expected a requestId of 1..128 bytes and a batch below 1 MiB");
		if (auto prior = m_Requests.find(id); prior != m_Requests.end()) {
			if (prior->second.Payload != payload) return Error("request_id_conflict", "This requestId already belongs to different content");
			if (std::chrono::steady_clock::now() - prior->second.Completed > std::chrono::minutes(10)) return Error("outcome_expired", "Result retention expired; do not automatically repeat the mutation");
			return prior->second.Result;
		}
		if (m_Requests.size() >= 4096) return Error("request_limit_exceeded", "This editor session has reached its 4096 mutation request limit");
		if (m_RequestBytes + payload.size() + 65536 > 32 * 1024 * 1024) return Error("request_limit_exceeded", "Mutation result retention has reached 32 MiB; start a new editor session");
		if (m_Playing || editor.IsPlayModeEntryPending() || String(*target, "context") != "edit") return Error("read_only_context", "Authoring requires Edit mode");
		for (const auto& [weak, instance] : m_Instances) {
			if (instance.Id != String(*target, "sceneId")) continue;
			const auto scene = weak.lock();
			if (!scene || String(*target, "sceneInstanceId") != instance.Generation) return Error("stale_scene_instance", "Query scenes again");
			if (String(args, "expectedRevision") != std::to_string(scene->GetRevision())) return Error("revision_conflict", "Scene changed since the query");
			if (String(args, "expectedSchemaRevision") != SchemaRevision()) return Error("schema_changed", "Query the schema again");
			const std::string before = std::to_string(scene->GetRevision());
			Json::Value result = EditorSceneEdit::Apply(*scene, editor.GetBridgeUndoStack(*scene), args, [&]() { editor.PrepareBridgeMutation(*scene); });
			result.AddMember("formatVersion", 1);
			result.AddMember("requestId", id);
			result.AddMember("revisionBefore", before);
			result.AddMember("revisionAfter", std::to_string(scene->GetRevision()));
			m_Requests.emplace(id, RequestResult{ payload, result, std::chrono::steady_clock::now() });
			m_RequestBytes += payload.size() + Json::Stringify(result).size();
			return result;
		}
		return Error("scene_not_found", "Query scenes again");
	}
}
