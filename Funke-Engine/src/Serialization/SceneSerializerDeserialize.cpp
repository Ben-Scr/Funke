#include "pch.hpp"
#include <atomic>
#include <mutex>
#include "Assets/AssetRegistry.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/SceneSerializerShared.hpp"
#include "Serialization/File.hpp"
#include "Serialization/FileStamp.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Scene/Scene.hpp"
#include "Scene/EntityHelper.hpp"
#include "Components/General/NameComponent.hpp"
#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#endif
#include "Components/Graphics/TextRendererComponent.hpp"
#if FUNKE_WITH_2D
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#endif
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/PrefabInstanceComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Entity.hpp"
#include "Components/Tags.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#if FUNKE_WITH_2D
#include "Components/Physics/FastBody2DComponent.hpp"
#include "Components/Physics/FastBoxCollider2DComponent.hpp"
#include "Components/Physics/FastCircleCollider2DComponent.hpp"
#endif
#include "Graphics/TextureManager.hpp"
#include "Audio/AudioManager.hpp"
#include "Physics/PhysicsTypes.hpp"
#include "Core/Application.hpp"
#include "Core/Log.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/ComponentInfo.hpp"
#include "Serialization/PrefabTemplateCache.hpp"

#include <cmath>
#include <cctype>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Funke {

	using Json::Value;
	using namespace SceneSerializerShared;

	namespace {
		static constexpr int SCENE_FORMAT_VERSION = 1;

		// Absent keys mean a Simple image with the standard shader (files from before 9-slice and materials).
		void ReadImageSlicingAndMaterial(const Value& imageValue, ImageComponent& image) {
			image.Material = GetUInt64Member(imageValue, "materialAsset", 0);
			const int type = GetIntMember(imageValue, "imageType", 0);
			image.Type = type == static_cast<int>(ImageType::Sliced) || type == static_cast<int>(ImageType::Tiled)
				? static_cast<ImageType>(type) : ImageType::Simple;
			float border[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if (const Value* values = GetArrayMember(imageValue, "border")) {
				const auto& items = values->GetArray();
				for (size_t i = 0; i < items.size() && i < 4; ++i) {
					border[i] = std::max(static_cast<float>(items[i].AsDoubleOr(0.0)), 0.0f);
				}
			}
			image.BorderLeft = border[0];
			image.BorderBottom = border[1];
			image.BorderRight = border[2];
			image.BorderTop = border[3];
			image.BorderScale = std::max(GetFloatMember(imageValue, "borderScale", 1.0f), 0.0f);
		}
#if FUNKE_WITH_2D
		static constexpr float k_MinScaleAxis = 0.0001f;

		// BoxCollider2D size: prefer the exact local size written since localSizeX/Y were
		// added. Legacy files only carry world-scaled scaleX/Y, which must be divided by the
		// entity's world scale — but during a full scene load parenting isn't wired yet, so
		// transform.Scale is still the *local* scale and a child of a scaled parent used to
		// grow by the parent's scale on every save/load cycle.
		Vec2 ReadBoxColliderLocalSize(const Value& colliderValue, const Transform2DComponent& transform) {
			if (colliderValue.FindMember("localSizeX") && colliderValue.FindMember("localSizeY")) {
				return {
					GetFloatMember(colliderValue, "localSizeX", 1.0f),
					GetFloatMember(colliderValue, "localSizeY", 1.0f)
				};
			}

			const Vec2 savedScale{
				GetFloatMember(colliderValue, "scaleX", transform.Scale.x),
				GetFloatMember(colliderValue, "scaleY", transform.Scale.y)
			};
			Vec2 localScale{ 1.0f, 1.0f };
			if (std::fabs(transform.Scale.x) > k_MinScaleAxis) {
				localScale.x = savedScale.x / transform.Scale.x;
			}
			if (std::fabs(transform.Scale.y) > k_MinScaleAxis) {
				localScale.y = savedScale.y / transform.Scale.y;
			}
			return localScale;
		}
#endif

		bool IsUnsignedIntegerValue(const Value& value) {
			if (value.IsString()) {
				const std::string text = value.AsStringOr();
				if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c) {
					return std::isdigit(c) != 0;
				})) return false;
				try {
					std::size_t end = 0;
					(void)std::stoull(text, &end);
					return end == text.size();
				}
				catch (...) {
					return false;
				}
			}
			if (!value.IsNumber()) return false;
			if (value.GetNumberKind() == Value::NumberKind::UInt64) return true;
			if (value.GetNumberKind() == Value::NumberKind::Int64) return value.AsInt64Or(-1) >= 0;
			const double numeric = value.AsDoubleOr(-1.0);
			return std::isfinite(numeric) && numeric >= 0.0 && std::floor(numeric) == numeric;
		}

		bool ValidateSceneRoot(const Value& root, std::string_view source, bool requireEntities) {
			const std::string sourceLabel = source.empty() ? std::string("scene data") : std::string(source);
			if (!root.IsObject()) {
				FNK_CORE_ERROR_TAG("SceneSerializer", "{} root must be an object", sourceLabel);
				return false;
			}
			if (const Value* version = root.FindMember("version")) {
				if (!version->IsNumber()) {
					FNK_CORE_ERROR_TAG("SceneSerializer", "{} has a non-numeric scene version", sourceLabel);
					return false;
				}
				const int value = version->AsIntOr(-1);
				if (value < 0 || version->AsDoubleOr(-1.0) != static_cast<double>(value)) {
					FNK_CORE_ERROR_TAG("SceneSerializer", "{} has an invalid scene version", sourceLabel);
					return false;
				}
				if (value > SCENE_FORMAT_VERSION) {
					FNK_CORE_ERROR_TAG("SceneSerializer", "{} uses newer scene version {} (supported: {})",
						sourceLabel, value, SCENE_FORMAT_VERSION);
					return false;
				}
			}
			if (const Value* name = root.FindMember("name"); name && !name->IsString()) {
				FNK_CORE_ERROR_TAG("SceneSerializer", "{} has a non-string name", sourceLabel);
				return false;
			}
			if (const Value* sceneId = root.FindMember("sceneId"); sceneId && !IsUnsignedIntegerValue(*sceneId)) {
				FNK_CORE_ERROR_TAG("SceneSerializer", "{} has an invalid scene id", sourceLabel);
				return false;
			}
			if (const Value* systems = root.FindMember("systems")) {
				if (!systems->IsArray()) {
					FNK_CORE_ERROR_TAG("SceneSerializer", "{} has a non-array systems member", sourceLabel);
					return false;
				}
				for (const Value& system : systems->GetArray()) {
					if (system.IsString()) {
						if (!system.AsStringOr().empty()) continue;
						FNK_CORE_ERROR_TAG("SceneSerializer", "{} has an empty system name", sourceLabel);
						return false;
					}
					if (!system.IsObject()) {
						FNK_CORE_ERROR_TAG("SceneSerializer", "{} has an invalid system entry", sourceLabel);
						return false;
					}
					const Value* className = system.FindMember("className");
					const Value* enabled = system.FindMember("enabled");
					const Value* fields = system.FindMember("fields");
					if (!className || !className->IsString() || className->AsStringOr().empty()
						|| (enabled && !enabled->IsBool()) || (fields && !fields->IsObject())) {
						FNK_CORE_ERROR_TAG("SceneSerializer", "{} has an invalid system object", sourceLabel);
						return false;
					}
					if (fields) {
						for (const auto& [fieldName, fieldValue] : fields->GetObject()) {
							(void)fieldName;
							if (!fieldValue.IsString()) {
								FNK_CORE_ERROR_TAG("SceneSerializer", "{} has a non-string system field value", sourceLabel);
								return false;
							}
						}
					}
				}
			}
			const Value* entities = root.FindMember("entities");
			if ((entities && !entities->IsArray()) || (requireEntities && !entities)) {
				FNK_CORE_ERROR_TAG("SceneSerializer", "{} requires an entities array", sourceLabel);
				return false;
			}
			return true;
		}

		class SceneLoadStage {
		public:
			explicit SceneLoadStage(Scene& target)
				: m_Scene(Scene::CreateDetachedScene(target.GetName())) {
				m_Scene->SetSceneId(target.GetSceneId());
			}

			~SceneLoadStage() {
				for (EntityHandle entity : m_Scene->GetRegistry().view<UUIDComponent>()) {
					m_Scene->GetRegistry().get<UUIDComponent>(entity).Id = UUID(0);
				}
			}

			Scene& Get() { return *m_Scene; }

			SceneLoadStage(const SceneLoadStage&) = delete;
			SceneLoadStage& operator=(const SceneLoadStage&) = delete;

		private:
			std::unique_ptr<Scene> m_Scene;
		};

		bool ValidateStagedEntityIdentities(Scene& scene, std::string_view source) {
			struct IdentityOwner {
				EntityHandle Entity = entt::null;
				const char* Domain = nullptr;
			};
			std::unordered_map<uint64_t, IdentityOwner> owners;
			owners.reserve(scene.GetEntityCount() * 2);
			const std::string sourceLabel = source.empty() ? std::string("scene data") : std::string(source);
			const auto registerIdentity = [&](uint64_t id, EntityHandle entity, const char* domain) {
				if (id == 0) {
					FNK_CORE_ERROR_TAG("SceneSerializer", "{} contains a zero {}", sourceLabel, domain);
					return false;
				}
				const auto [it, inserted] = owners.emplace(id, IdentityOwner{ entity, domain });
				if (inserted) return true;
				FNK_CORE_ERROR_TAG("SceneSerializer", "{} reuses entity identity {} across {} and {}",
					sourceLabel, id, it->second.Domain, domain);
				return false;
			};

			for (EntityHandle entity : scene.GetRegistry().view<EntityMetaDataComponent>()) {
				const EntityMetaData& metadata = scene.GetRegistry().get<EntityMetaDataComponent>(entity).MetaData;
				if (!registerIdentity(metadata.RuntimeID, entity, "RuntimeID")) return false;
				uint64_t persistentId = metadata.RuntimeID;
				if (metadata.Origin == EntityOrigin::Scene) {
					persistentId = static_cast<uint64_t>(metadata.SceneGUID);
					if (!registerIdentity(persistentId, entity, "SceneGUID")) return false;
				}
				if (!scene.HasComponent<UUIDComponent>(entity)
					|| static_cast<uint64_t>(scene.GetComponent<UUIDComponent>(entity).Id) != persistentId) {
					FNK_CORE_ERROR_TAG("SceneSerializer", "{} contains inconsistent entity identity metadata", sourceLabel);
					return false;
				}
			}
			return true;
		}

		// Destroys the in-progress entity if construction throws before Commit(); prevents half-baked orphans.
		class EntityRollbackGuard {
		public:
			EntityRollbackGuard(Scene& scene, EntityHandle entity, bool wasDirty, bool wasUiDirty)
				: m_Scene(&scene), m_Entity(entity),
				m_FixupStart(scene.GetPendingEntityRefFixupCount()),
				m_WasDirty(wasDirty), m_WasUiDirty(wasUiDirty) {}
			~EntityRollbackGuard() noexcept {
				if (m_Committed) return;
				if (m_Entity == entt::null) return;
				try {
					m_Scene->DiscardPendingEntityRefFixupsFrom(m_FixupStart);
					if (m_Scene && m_Scene->GetRegistry().valid(m_Entity)) {
						if (m_Scene->GetRegistry().all_of<UUIDComponent>(m_Entity)) {
							m_Scene->GetRegistry().get<UUIDComponent>(m_Entity).Id = UUID(0);
						}
						m_Scene->DestroyEntity(m_Entity);
					}
				}
				catch (...) {}
				if (m_WasDirty) m_Scene->MarkDirty(); else m_Scene->ClearDirty();
				if (m_WasUiDirty) m_Scene->MarkUIDirty(); else m_Scene->ClearUIDirty();
			}
			EntityRollbackGuard(const EntityRollbackGuard&) = delete;
			EntityRollbackGuard& operator=(const EntityRollbackGuard&) = delete;
			void Commit() { m_Committed = true; }
		private:
			Scene*       m_Scene = nullptr;
			EntityHandle m_Entity = entt::null;
			std::size_t  m_FixupStart = 0;
			bool         m_WasDirty = false;
			bool         m_WasUiDirty = false;
			bool         m_Committed = false;
		};

		class EntityTreeRollbackGuard {
		public:
			EntityTreeRollbackGuard(Scene& scene, bool enabled, std::size_t fixupStart)
				: m_Scene(&scene), m_FixupStart(fixupStart), m_Enabled(enabled),
				m_WasDirty(scene.IsDirty()), m_WasUiDirty(scene.IsUIDirty()) {}
			~EntityTreeRollbackGuard() noexcept {
				if (!m_Enabled || m_Committed || !m_Scene) return;
				try { m_Scene->DiscardPendingEntityRefFixupsFrom(m_FixupStart); }
				catch (...) {}
				for (auto it = m_Entities.rbegin(); it != m_Entities.rend(); ++it) {
					if (m_Scene->IsValid(*it)) {
						try { m_Scene->DestroyEntity(*it); }
						catch (...) {}
					}
				}
				if (m_WasDirty) m_Scene->MarkDirty(); else m_Scene->ClearDirty();
				if (m_WasUiDirty) m_Scene->MarkUIDirty(); else m_Scene->ClearUIDirty();
			}
			EntityTreeRollbackGuard(const EntityTreeRollbackGuard&) = delete;
			EntityTreeRollbackGuard& operator=(const EntityTreeRollbackGuard&) = delete;
			void Reserve(std::size_t count) { m_Entities.reserve(count); }
			void Track(EntityHandle entity) { m_Entities.push_back(entity); }
			void Commit() { m_Committed = true; }
		private:
			Scene* m_Scene = nullptr;
			std::vector<EntityHandle> m_Entities;
			std::size_t m_FixupStart = 0;
			bool m_Enabled = false;
			bool m_WasDirty = false;
			bool m_WasUiDirty = false;
			bool m_Committed = false;
		};

		class PrefabRefreshRollbackGuard {
		public:
			PrefabRefreshRollbackGuard(Scene& scene, EntityHandle existing, EntityHandle replacement,
				uint64_t preservedId, std::size_t fixupStart, bool wasDirty, bool wasUiDirty)
				: m_Scene(&scene), m_Existing(existing), m_Replacement(replacement),
				m_PreservedId(preservedId), m_FixupStart(fixupStart),
				m_WasDirty(wasDirty), m_WasUiDirty(wasUiDirty) {}
			~PrefabRefreshRollbackGuard() noexcept {
				if (m_Committed || !m_Scene) return;
				try { m_Scene->DiscardPendingEntityRefFixupsFrom(m_FixupStart); }
				catch (...) {}
				if (m_Scene->IsValid(m_Replacement)) {
					try {
						if (m_IdentityTransferred && m_Scene->HasComponent<UUIDComponent>(m_Replacement)) {
							m_Scene->GetComponent<UUIDComponent>(m_Replacement).Id = UUID(0);
						}
						m_Scene->DestroyEntity(m_Replacement);
					}
					catch (...) {}
				}
				if (m_IdentityTransferred && m_PreservedId != 0 && m_Scene->IsValid(m_Existing)) {
					try { m_Scene->SetEntityPersistentID(m_Existing, m_PreservedId); }
					catch (...) {}
				}
				if (m_WasDirty) m_Scene->MarkDirty(); else m_Scene->ClearDirty();
				if (m_WasUiDirty) m_Scene->MarkUIDirty(); else m_Scene->ClearUIDirty();
			}
			PrefabRefreshRollbackGuard(const PrefabRefreshRollbackGuard&) = delete;
			PrefabRefreshRollbackGuard& operator=(const PrefabRefreshRollbackGuard&) = delete;
			void MarkIdentityTransferred() { m_IdentityTransferred = true; }
			void Commit() { m_Committed = true; }
		private:
			Scene* m_Scene = nullptr;
			EntityHandle m_Existing = entt::null;
			EntityHandle m_Replacement = entt::null;
			uint64_t m_PreservedId = 0;
			std::size_t m_FixupStart = 0;
			bool m_WasDirty = false;
			bool m_WasUiDirty = false;
			bool m_IdentityTransferred = false;
			bool m_Committed = false;
		};

		Value SerializeScriptFields(const ScriptComponent& scriptComponent) {
			Value fieldsByClass = Value::MakeObject();
			auto& callbacks = ScriptEngine::GetCallbacks();

			for (const ScriptInstance& instance : scriptComponent.Scripts) {
				if (instance.GetType() == ScriptType::Native) {
					continue;
				}

				const char* rawJson = nullptr;
				const bool hasLiveInstance = instance.HasManagedInstance();

				if (hasLiveInstance && callbacks.GetScriptFields) {
					rawJson = callbacks.GetScriptFields(static_cast<int32_t>(instance.GetGCHandle()));
				}
				else if (callbacks.GetClassFieldDefs) {
					rawJson = callbacks.GetClassFieldDefs(instance.GetClassName().c_str());
				}

				if (!rawJson || !*rawJson) {
					continue;
				}

				Value fieldArray;
				std::string parseError;
				if (!Json::TryParse(rawJson, fieldArray, &parseError) || !fieldArray.IsArray()) {
					FNK_CORE_WARN_TAG(
						"SceneSerializer",
						"Failed to parse script field metadata for {}: {}",
						instance.GetClassName(),
						parseError);
					continue;
				}

				if (!hasLiveInstance && !scriptComponent.PendingFieldValues.empty()) {
					const std::string prefix = instance.GetClassName() + ".";
					for (Value& fieldValue : fieldArray.GetArray()) {
						if (!fieldValue.IsObject()) {
							continue;
						}

						const std::string fieldName = GetStringMember(fieldValue, "name");
						if (fieldName.empty()) {
							continue;
						}

						const auto pendingValueIt = scriptComponent.PendingFieldValues.find(prefix + fieldName);
						if (pendingValueIt == scriptComponent.PendingFieldValues.end()) {
							continue;
						}

						fieldValue.AddMember("value", Value(pendingValueIt->second));
					}
				}

				for (Value& fieldValue : fieldArray.GetArray()) {
					if (!fieldValue.IsObject()) {
						continue;
					}

					const std::string fieldType = GetStringMember(fieldValue, "type");
					const std::string currentValue = GetStringMember(fieldValue, "value");
					const std::string normalizedValue = NormalizeScriptAssetValue(fieldType, currentValue);
					if (normalizedValue != currentValue) {
						fieldValue.AddMember("value", Value(normalizedValue));
					}
				}

				if (!fieldArray.GetArray().empty()) {
					fieldsByClass.AddMember(instance.GetClassName(), std::move(fieldArray));
				}
			}

			for (const std::string& className : scriptComponent.ManagedComponents) {
				if (className.empty() || !callbacks.GetClassFieldDefs) {
					continue;
				}

				const char* rawJson = callbacks.GetClassFieldDefs(className.c_str());
				if (!rawJson || !*rawJson) {
					continue;
				}

				Value fieldArray;
				std::string parseError;
				if (!Json::TryParse(rawJson, fieldArray, &parseError) || !fieldArray.IsArray()) {
					FNK_CORE_WARN_TAG(
						"SceneSerializer",
						"Failed to parse managed component field metadata for {}: {}",
						className,
						parseError);
					continue;
				}

				const std::string prefix = className + ".";
				for (Value& fieldValue : fieldArray.GetArray()) {
					if (!fieldValue.IsObject()) {
						continue;
					}

					const std::string fieldName = GetStringMember(fieldValue, "name");
					if (fieldName.empty()) {
						continue;
					}

					const auto pendingValueIt = scriptComponent.PendingFieldValues.find(prefix + fieldName);
					if (pendingValueIt != scriptComponent.PendingFieldValues.end()) {
						fieldValue.AddMember("value", Value(pendingValueIt->second));
					}

					const std::string fieldType = GetStringMember(fieldValue, "type");
					const std::string currentValue = GetStringMember(fieldValue, "value");
					const std::string normalizedValue = NormalizeScriptAssetValue(fieldType, currentValue);
					if (normalizedValue != currentValue) {
						fieldValue.AddMember("value", Value(normalizedValue));
					}
				}

				if (!fieldArray.GetArray().empty()) {
					fieldsByClass.AddMember(className, std::move(fieldArray));
				}
			}

			return fieldsByClass;
		}

		// All callers now use Detail::SerializeEntity; a local divergent copy here caused spurious prefab-diff deletions.

		EntityOrigin EntityOriginFromString(const std::string& value) {
			if (value == "Scene" || value == "scene") {
				return EntityOrigin::Scene;
			}
			if (value == "Prefab" || value == "prefab") {
				return EntityOrigin::Prefab;
			}
			return EntityOrigin::Runtime;
		}

		void RemoveObjectMember(Value& value, std::string_view key) {
			if (!value.IsObject()) {
				return;
			}

			auto& members = value.GetObject();
			members.erase(
				std::remove_if(members.begin(), members.end(), [key](const auto& member) {
					return member.first == key;
				}),
				members.end());
		}

		void RemoveEntityIdentityMembers(Value& value) {
			RemoveObjectMember(value, "Origin");
			RemoveObjectMember(value, "RuntimeID");
			RemoveObjectMember(value, "EntityID");
			RemoveObjectMember(value, "SceneGUID");
			RemoveObjectMember(value, "PrefabGUID");
			RemoveObjectMember(value, "uuid");
		}

		void RemovePrefabRuntimeIdentityMembers(Value& value) {
			RemoveObjectMember(value, "Origin");
			RemoveObjectMember(value, "RuntimeID");
			RemoveObjectMember(value, "EntityID");
			RemoveObjectMember(value, "SceneGUID");
			RemoveObjectMember(value, "PrefabGUID");
		}

		void RemoveEntityComparisonIdentityMembers(Value& value) {
			RemoveEntityIdentityMembers(value);
			RemoveObjectMember(value, "parentUuid");
		}

		bool JsonEquivalent(const Value& left, const Value& right) {
			return left == right;
		}

		void BuildOverridePatch(const Value& prefabValue, const Value& instanceValue,
			const std::string& prefix, Value& overrides) {
			if (prefabValue.IsObject() && instanceValue.IsObject()) {
				for (const auto& [key, instanceMember] : instanceValue.GetObject()) {
					const Value* prefabMember = prefabValue.FindMember(key);
					const std::string path = prefix.empty() ? key : prefix + "." + key;
					if (prefabMember && prefabMember->IsObject() && instanceMember.IsObject()) {
						BuildOverridePatch(*prefabMember, instanceMember, path, overrides);
						continue;
					}

					if (!prefabMember || !JsonEquivalent(*prefabMember, instanceMember)) {
						overrides.AddMember(path, instanceMember);
					}
				}
				return;
			}

			if (!JsonEquivalent(prefabValue, instanceValue)) {
				overrides.AddMember(prefix, instanceValue);
			}
		}

		std::vector<std::string> SplitOverridePath(const std::string& path) {
			std::vector<std::string> parts;
			size_t start = 0;
			while (start <= path.size()) {
				const size_t dot = path.find('.', start);
				parts.push_back(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
				if (dot == std::string::npos) {
					break;
				}
				start = dot + 1;
			}
			return parts;
		}

		bool ApplyScriptFieldOverride(Value& entityValue, const std::vector<std::string>& parts, const Value& overrideValue) {
			if (parts.size() != 2) {
				return false;
			}

			Value* scriptFields = entityValue.FindMember("ScriptFields");
			if (!scriptFields || !scriptFields->IsObject()) {
				return false;
			}

			Value* classFields = scriptFields->FindMember(parts[0]);
			if (!classFields || !classFields->IsArray()) {
				return false;
			}

			for (Value& fieldValue : classFields->GetArray()) {
				if (!fieldValue.IsObject() || GetStringMember(fieldValue, "name") != parts[1]) {
					continue;
				}

				fieldValue.AddMember("value", overrideValue);
				return true;
			}

			Value fieldValue = Value::MakeObject();
			fieldValue.AddMember("name", Value(parts[1]));
			fieldValue.AddMember("value", overrideValue);
			classFields->Append(std::move(fieldValue));
			return true;
		}

		bool ApplyOverridePath(Value& entityValue, const std::string& path, const Value& overrideValue) {
			if (path.empty()) {
				return false;
			}

			const std::vector<std::string> parts = SplitOverridePath(path);
			if (parts.empty()) {
				return false;
			}

			if (ApplyScriptFieldOverride(entityValue, parts, overrideValue)) {
				return true;
			}

			Value* current = &entityValue;
			for (size_t i = 0; i + 1 < parts.size(); ++i) {
				if (!current->IsObject()) {
					return false;
				}

				Value* child = current->FindMember(parts[i]);
				if (!child) {
					child = &current->AddMember(parts[i], Value::MakeObject());
				}
				current = child;
			}

			current->AddMember(parts.back(), overrideValue);
			return true;
		}

		const Value* GetOverrideValueAtPath(const Value& entityValue, const std::string& path) {
			const std::vector<std::string> parts = SplitOverridePath(path);
			if (parts.empty()) {
				return nullptr;
			}

			if (parts.size() == 2) {
				if (const Value* scriptFields = GetObjectMember(entityValue, "ScriptFields")) {
					if (const Value* classFields = scriptFields->FindMember(parts[0]); classFields && classFields->IsArray()) {
						for (const Value& fieldValue : classFields->GetArray()) {
							if (!fieldValue.IsObject() || GetStringMember(fieldValue, "name") != parts[1]) {
								continue;
							}

							return fieldValue.FindMember("value");
						}
					}
				}
			}

			const Value* current = &entityValue;
			for (const std::string& part : parts) {
				if (!current->IsObject()) {
					return nullptr;
				}

				current = current->FindMember(part);
				if (!current) {
					return nullptr;
				}
			}
			return current;
		}

		void ApplyOverrides(Value& prefabEntityValue, const Value& instanceValue);

		struct PrefabDefinition {
			std::vector<Value> Entities;
		};

		uint64_t GetPrefabSourceId(const Value& entityValue) {
			return GetUInt64Member(entityValue, "uuid", 0);
		}

		EntityOrigin GetEntityTreeOrigin(const Value& entityValue, EntityOrigin defaultOrigin) {
			if (defaultOrigin != EntityOrigin::Scene) return defaultOrigin;
			const std::string origin = GetStringMember(entityValue, "Origin", "");
			if (!origin.empty() && EntityOriginFromString(origin) == EntityOrigin::Prefab
				&& GetUInt64Member(entityValue, "PrefabGUID", 0) != 0) {
				return EntityOrigin::Prefab;
			}
			return defaultOrigin;
		}

		bool ValidateEntityTreeSourceIds(
			const std::vector<Value>& entityValues,
			EntityOrigin origin,
			bool validateRefRemap) {
			std::unordered_set<uint64_t> hierarchyIds;
			std::unordered_set<uint64_t> refIds;
			hierarchyIds.reserve(entityValues.size());
			refIds.reserve(entityValues.size());

			for (const Value& entityValue : entityValues) {
				if (!entityValue.IsObject()) return false;
				const uint64_t sourceId = GetPrefabSourceId(entityValue);
				if (sourceId != 0 && !hierarchyIds.insert(sourceId).second) {
					FNK_CORE_ERROR_TAG("SceneSerializer", "Entity tree reuses source identity {}", sourceId);
					return false;
				}

				if (!validateRefRemap) continue;
				uint64_t refId = GetUInt64Member(entityValue, "sourceUuid", 0);
				if (refId == 0 && GetEntityTreeOrigin(entityValue, origin) == EntityOrigin::Prefab) {
					refId = sourceId;
				}
				if (refId != 0 && !refIds.insert(refId).second) {
					FNK_CORE_ERROR_TAG("SceneSerializer", "Entity tree reuses reference-remap identity {}", refId);
					return false;
				}
			}

			if (entityValues.size() > 1 && hierarchyIds.size() != entityValues.size()) {
				FNK_CORE_ERROR_TAG("SceneSerializer", "Multi-entity tree contains a missing source identity");
				return false;
			}
			for (const Value& entityValue : entityValues) {
				const uint64_t parentId = GetUInt64Member(entityValue, "parentUuid", 0);
				if (parentId != 0 && !hierarchyIds.contains(parentId)) {
					FNK_CORE_ERROR_TAG("SceneSerializer", "Entity tree references missing parent identity {}", parentId);
					return false;
				}
			}
			return true;
		}

		bool ReadPrefabDefinitionFromRoot(const Value& root, PrefabDefinition& outDefinition) {
			outDefinition.Entities.clear();
			if (!root.IsObject()) {
				return false;
			}

			if (const Value* entities = GetArrayMember(root, "Entities")) {
				for (const Value& entityValue : entities->GetArray()) {
					if (entityValue.IsObject()) {
						outDefinition.Entities.push_back(entityValue);
					}
				}
				if (!outDefinition.Entities.empty()) {
					return true;
				}
			}

			if (const Value* entityValue = GetObjectMember(root, "Entity")) {
				outDefinition.Entities.push_back(*entityValue);
				return true;
			}
			if (const Value* entityValue = GetObjectMember(root, "prefab")) {
				outDefinition.Entities.push_back(*entityValue);
				return true;
			}

			if (root.FindMember("Transform2D") || root.FindMember("Scripts") || root.FindMember("Name")) {
				outDefinition.Entities.push_back(root);
				return true;
			}

			return false;
		}

		bool LoadPrefabDefinition(uint64_t prefabGuid, PrefabDefinition& outDefinition);

		bool ApplyEntityOverrides(PrefabDefinition& definition, const Value& instanceValue) {
			if (definition.Entities.empty()) {
				return true;
			}
			if (!ValidateEntityTreeSourceIds(definition.Entities, EntityOrigin::Prefab, false)) return false;

			// Backwards-compatible root override payload.
			ApplyOverrides(definition.Entities.front(), instanceValue);
			if (!ValidateEntityTreeSourceIds(definition.Entities, EntityOrigin::Prefab, false)) return false;

			const Value* entityOverrides = GetObjectMember(instanceValue, "EntityOverrides");
			if (!entityOverrides) {
				return true;
			}

			std::unordered_map<uint64_t, Value*> entitiesBySourceId;
			entitiesBySourceId.reserve(definition.Entities.size());
			for (Value& entityValue : definition.Entities) {
				const uint64_t sourceId = GetPrefabSourceId(entityValue);
				if (sourceId != 0) {
					entitiesBySourceId[sourceId] = &entityValue;
				}
			}

			for (const auto& [sourceIdText, overrides] : entityOverrides->GetObject()) {
				uint64_t sourceId = 0;
				try {
					sourceId = static_cast<uint64_t>(std::stoull(sourceIdText));
				}
				catch (...) {
					// Swallowed silently before — a corrupt prefab would lose its
					// override for that entity with no log line. Surface it so the
					// user knows something didn't apply.
					FNK_CORE_WARN_TAG("SceneSerializer", "Skipping entity override with invalid source-id key: '{}'", sourceIdText);
					continue;
				}

				auto targetIt = entitiesBySourceId.find(sourceId);
				if (targetIt == entitiesBySourceId.end() || !overrides.IsObject()) {
					continue;
				}

				Value wrapper = Value::MakeObject();
				wrapper.AddMember("Overrides", overrides);
				ApplyOverrides(*targetIt->second, wrapper);
			}
			return ValidateEntityTreeSourceIds(definition.Entities, EntityOrigin::Prefab, false);
		}

		std::unordered_map<uint64_t, const Value*> BuildSourceEntityMap(const PrefabDefinition& definition) {
			std::unordered_map<uint64_t, const Value*> bySourceId;
			bySourceId.reserve(definition.Entities.size());
			for (const Value& entityValue : definition.Entities) {
				const uint64_t sourceId = GetPrefabSourceId(entityValue);
				if (sourceId != 0) {
					bySourceId[sourceId] = &entityValue;
				}
			}
			return bySourceId;
		}

		uint64_t GetPrefabInstanceSourceId(Scene& scene, EntityHandle entity) {
			auto& registry = scene.GetRegistry();
			if (registry.all_of<PrefabInstanceComponent>(entity)) {
				const auto& instance = registry.get<PrefabInstanceComponent>(entity);
				if (instance.SourceEntityId != 0) {
					return instance.SourceEntityId;
				}
			}
			if (registry.all_of<UUIDComponent>(entity)) {
				return static_cast<uint64_t>(registry.get<UUIDComponent>(entity).Id);
			}
			return 0;
		}

		EntityHandle GetSamePrefabParent(Scene& scene, EntityHandle entity, uint64_t prefabGuid) {
			auto& registry = scene.GetRegistry();
			const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
			if (!hierarchy) return entt::null;
			const EntityHandle parent = hierarchy->Parent;
			if (parent == entt::null || !registry.valid(parent)) return entt::null;
			if (scene.GetEntityOrigin(parent) != EntityOrigin::Prefab
				|| static_cast<uint64_t>(scene.GetPrefabGUID(parent)) != prefabGuid) {
				return entt::null;
			}
			return parent;
		}

		uint64_t GetPrefabSourceIdOrZero(Scene& scene, EntityHandle entity) {
			const auto* instance = scene.GetRegistry().try_get<PrefabInstanceComponent>(entity);
			return instance ? instance->SourceEntityId : 0;
		}

		std::vector<EntityHandle> CollectPrefabInstanceSubtree(Scene& scene, EntityHandle root, uint64_t prefabGuid) {
			std::vector<EntityHandle> entities;
			auto& registry = scene.GetRegistry();
			std::unordered_set<uint32_t> visited;

			std::function<void(EntityHandle)> visit = [&](EntityHandle entity) {
				if (entity == entt::null || !registry.valid(entity)) {
					return;
				}

				const uint32_t key = static_cast<uint32_t>(entity);
				if (!visited.insert(key).second) {
					return;
				}

				if (entity != root
					&& (scene.GetEntityOrigin(entity) != EntityOrigin::Prefab
						|| static_cast<uint64_t>(scene.GetPrefabGUID(entity)) != prefabGuid
						|| SceneSerializer::IsPrefabInstanceRoot(scene, entity))) {
					return;
				}

				entities.push_back(entity);
				if (!registry.all_of<HierarchyComponent>(entity)) {
					return;
				}

				const auto children = registry.get<HierarchyComponent>(entity).Children;
				for (EntityHandle child : children) {
					visit(child);
				}
			};

			visit(root);
			return entities;
		}

		// Prefab-instance children aren't written to the scene file, so their ids must be reproducible from
		// (instance root id, source entity id); random ids broke parentUuid links and entity refs on reload.
		uint64_t DerivePrefabChildPersistentId(uint64_t instanceId, uint64_t sourceId) {
			uint64_t z = instanceId ^ (sourceId * 0x9E3779B97F4A7C15ull);
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
			z ^= z >> 31;
			return z != 0 ? z : 1;
		}

		void PinPrefabInstanceChildIds(Scene& scene, EntityHandle root) {
			if (root == entt::null || !scene.IsValid(root)) return;
			const uint64_t instanceId = scene.GetEntityPersistentID(root);
			const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(root));
			if (instanceId == 0 || prefabGuid == 0) return;

			for (EntityHandle child : CollectPrefabInstanceSubtree(scene, root, prefabGuid)) {
				if (child == root) continue;
				const uint64_t sourceId = GetPrefabSourceIdOrZero(scene, child);
				if (sourceId == 0) continue;
				const uint64_t previousId = scene.GetEntityPersistentID(child);
				const uint64_t derivedId = DerivePrefabChildPersistentId(instanceId, sourceId);
				if (previousId == derivedId) continue;
				if (scene.SetEntityPersistentID(child, derivedId)) {
					// Template-internal refs were remapped to the old id; follow the re-key.
					scene.RetargetEntityRefRemap(previousId, derivedId);
				}
			}
		}

		Value SerializePrefabComparableEntity(
			Scene& scene,
			EntityHandle entity,
			const std::unordered_map<uint32_t, uint64_t>& sourceIds) {
			Value entityValue = Detail::SerializeEntity(scene, entity);
			RemovePrefabRuntimeIdentityMembers(entityValue);
			// SerializeEntity already stamped live uuid/parentUuid; drop them so the
			// source-id re-stamp below inserts cleanly instead of tripping AddMember's
			// duplicate-key overwrite trace (this runs every frame in the inspector diff).
			RemoveObjectMember(entityValue, "uuid");
			RemoveObjectMember(entityValue, "parentUuid");

			const auto sourceIt = sourceIds.find(static_cast<uint32_t>(entity));
			const uint64_t sourceId = sourceIt != sourceIds.end() ? sourceIt->second : 0;
			if (sourceId != 0) {
				entityValue.AddMember("uuid", Value(std::to_string(sourceId)));
			}

			auto& registry = scene.GetRegistry();
			EntityHandle parent = entt::null;
			if (registry.all_of<HierarchyComponent>(entity)) {
				parent = registry.get<HierarchyComponent>(entity).Parent;
			}

			const auto parentIt = sourceIds.find(static_cast<uint32_t>(parent));
			if (parentIt != sourceIds.end() && parentIt->second != 0) {
				entityValue.AddMember("parentUuid", Value(std::to_string(parentIt->second)));
			}
			else {
				RemoveObjectMember(entityValue, "parentUuid");
			}

			return entityValue;
		}

		void BuildPrefabInstanceOverrideSet(
			Scene& scene,
			EntityHandle root,
			const PrefabDefinition& sourceDefinition,
			Value& outRootOverrides,
			Value& outEntityOverrides) {
			outRootOverrides = Value::MakeObject();
			outEntityOverrides = Value::MakeObject();
			if (sourceDefinition.Entities.empty()) {
				return;
			}

			const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(root));
			const std::vector<EntityHandle> instanceEntities = CollectPrefabInstanceSubtree(scene, root, prefabGuid);
			if (instanceEntities.empty()) {
				return;
			}

			std::unordered_map<uint32_t, uint64_t> sourceIds;
			sourceIds.reserve(instanceEntities.size());
			for (EntityHandle entity : instanceEntities) {
				sourceIds[static_cast<uint32_t>(entity)] = GetPrefabInstanceSourceId(scene, entity);
			}

			Value currentRoot = SerializePrefabComparableEntity(scene, root, sourceIds);
			Value sourceRoot = sourceDefinition.Entities.front();
			RemoveEntityComparisonIdentityMembers(currentRoot);
			RemoveEntityComparisonIdentityMembers(sourceRoot);
			NormalizeLegacyColliderKeysForDiff(sourceRoot, currentRoot);
			BuildOverridePatch(sourceRoot, currentRoot, {}, outRootOverrides);

			const auto sourceById = BuildSourceEntityMap(sourceDefinition);
			for (EntityHandle entity : instanceEntities) {
				const uint64_t sourceId = sourceIds[static_cast<uint32_t>(entity)];
				if (sourceId == 0) {
					continue;
				}

				const auto sourceIt = sourceById.find(sourceId);
				if (sourceIt == sourceById.end() || !sourceIt->second) {
					continue;
				}

				Value currentValue = SerializePrefabComparableEntity(scene, entity, sourceIds);
				Value sourceValue = *sourceIt->second;
				RemoveEntityComparisonIdentityMembers(currentValue);
				RemoveEntityComparisonIdentityMembers(sourceValue);

				Value entityOverrides = Value::MakeObject();
				NormalizeLegacyColliderKeysForDiff(sourceValue, currentValue);
				BuildOverridePatch(sourceValue, currentValue, {}, entityOverrides);
				if (!entityOverrides.GetObject().empty()) {
					outEntityOverrides.AddMember(std::to_string(sourceId), std::move(entityOverrides));
				}
			}
		}

		bool LoadPrefabEntityValue(uint64_t prefabGuid, Value& outEntityValue) {
			PrefabDefinition definition;
			if (!LoadPrefabDefinition(prefabGuid, definition) || definition.Entities.empty()) {
				return false;
			}

			outEntityValue = definition.Entities.front();
			return true;
		}

		struct CachedPrefabRoot {
			std::string Path;
			FileStamp Stamp;
			std::shared_ptr<const Value> Root;
		};
		std::mutex g_PrefabRootCacheMutex;
		std::unordered_map<std::string, CachedPrefabRoot> g_PrefabRootCache;
		// Parses replaced because the file changed outside SaveEntityToFile (which drops its entry
		// instead); the editor drains these to refresh loaded instances against the old parse.
		std::unordered_map<std::string, SceneSerializer::ExternalPrefabChange> g_SupersededPrefabRoots;

		std::string PrefabRootCacheKey(const std::string& prefabPath) {
			std::string key = Path::ToGenericUtf8(Path::FromUtf8(prefabPath).lexically_normal());
#ifdef FNK_PLATFORM_WINDOWS
			std::transform(key.begin(), key.end(), key.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
			return key;
		}

		// Caller holds g_PrefabRootCacheMutex. Keeps the oldest parse when several edits land before a drain.
		void SupersedePrefabRootLocked(const std::string& key, const CachedPrefabRoot& entry) {
			if (entry.Root && !g_SupersededPrefabRoots.contains(key)) {
				g_SupersededPrefabRoots.emplace(key, SceneSerializer::ExternalPrefabChange{ entry.Path, entry.Root });
			}
		}

		// Every prefab instance (and every runtime spawn of a non-bakeable prefab) used to re-read and
		// re-parse the file. Keyed on (mtime, size) so editor saves and external edits still land.
		bool ReadPrefabRootCached(const std::string& prefabPath, std::shared_ptr<const Value>& outRoot) {
			const FileStamp stamp = FileStamp::Read(prefabPath);
			if (!stamp.Exists) return false;

			const std::string key = PrefabRootCacheKey(prefabPath);
			{
				std::lock_guard<std::mutex> lock(g_PrefabRootCacheMutex);
				const auto it = g_PrefabRootCache.find(key);
				if (it != g_PrefabRootCache.end() && it->second.Stamp == stamp) {
					outRoot = it->second.Root;
					return true;
				}
			}

			auto root = std::make_shared<Value>();
			std::string readError;
			if (!SceneSerializerStorage::ReadRootFromFile(prefabPath, *root, &readError) || !root->IsObject()) {
				FNK_CORE_WARN_TAG("SceneSerializer", "Failed to parse prefab {}: {}", prefabPath, readError);
				return false;
			}

			std::lock_guard<std::mutex> lock(g_PrefabRootCacheMutex);
			auto& entry = g_PrefabRootCache[key];
			if (entry.Root && entry.Stamp != stamp) {
				SupersedePrefabRootLocked(key, entry);
			}
			entry.Path = prefabPath;
			entry.Stamp = stamp;
			entry.Root = std::move(root);
			outRoot = entry.Root;
			return true;
		}

		bool LoadPrefabDefinitionRecursive(uint64_t prefabGuid, PrefabDefinition& outDefinition,
			std::unordered_set<uint64_t>& inProgress) {
			if (!inProgress.insert(prefabGuid).second) {
				FNK_CORE_WARN_TAG("SceneSerializer", "Circular prefab reference detected at GUID {}", prefabGuid);
				return false;
			}
			constexpr std::size_t k_MaxPrefabChainDepth = 32;
			if (inProgress.size() > k_MaxPrefabChainDepth) {
				FNK_CORE_WARN_TAG("SceneSerializer", "Prefab inheritance chain exceeds max depth {}", k_MaxPrefabChainDepth);
				inProgress.erase(prefabGuid);
				return false;
			}

			const std::string prefabPath = AssetRegistry::ResolvePath(prefabGuid);
			std::shared_ptr<const Value> cachedRoot;
			if (prefabPath.empty() || !ReadPrefabRootCached(prefabPath, cachedRoot)) {
				inProgress.erase(prefabGuid);
				return false;
			}
			const Value& root = *cachedRoot;

			if (ReadPrefabDefinitionFromRoot(root, outDefinition)) {
				const bool valid = ValidateEntityTreeSourceIds(outDefinition.Entities, EntityOrigin::Prefab, true);
				inProgress.erase(prefabGuid);
				return valid;
			}

			if (const Value* prefabRefValue = root.FindMember("PrefabRef")) {
				uint64_t basePrefabGuid = 0;
				if (prefabRefValue->IsString()) {
					const std::string prefabRef = prefabRefValue->AsStringOr();
					try {
						basePrefabGuid = static_cast<uint64_t>(std::stoull(prefabRef));
					}
					catch (...) {
						basePrefabGuid = AssetRegistry::GetOrCreateAssetUUID(prefabRef);
					}
				}
				else {
					basePrefabGuid = prefabRefValue->AsUInt64Or(0);
				}

				if (basePrefabGuid != 0 && LoadPrefabDefinitionRecursive(basePrefabGuid, outDefinition, inProgress)) {
					if (!ApplyEntityOverrides(outDefinition, root)) {
						inProgress.erase(prefabGuid);
						return false;
					}
					inProgress.erase(prefabGuid);
					return true;
				}
			}

			inProgress.erase(prefabGuid);
			return false;
		}

		bool LoadPrefabDefinition(uint64_t prefabGuid, PrefabDefinition& outDefinition) {
			std::unordered_set<uint64_t> inProgress;
			return LoadPrefabDefinitionRecursive(prefabGuid, outDefinition, inProgress);
		}

		uint64_t ResolvePrefabGuid(const Value& entityValue) {
			uint64_t prefabGuid = GetUInt64Member(entityValue, "PrefabGUID", 0);
			if (prefabGuid != 0) {
				return prefabGuid;
			}

			const std::string prefabRef = GetStringMember(entityValue, "PrefabRef");
			if (!prefabRef.empty()) {
				return AssetRegistry::GetOrCreateAssetUUID(prefabRef);
			}

			return 0;
		}

		void ApplyOverrides(Value& prefabEntityValue, const Value& instanceValue) {
			if (const Value* overrides = GetObjectMember(instanceValue, "Overrides")) {
				for (const auto& [path, overrideValue] : overrides->GetObject()) {
					ApplyOverridePath(prefabEntityValue, path, overrideValue);
				}
				return;
			}

			if (const Value* overrideArray = GetArrayMember(instanceValue, "Overrides")) {
				for (const Value& overrideEntry : overrideArray->GetArray()) {
					if (!overrideEntry.IsObject()) {
						continue;
					}

					const std::string componentName = GetStringMember(overrideEntry, "Component");
					const std::string fieldName = GetStringMember(overrideEntry, "Field");
					const Value* value = overrideEntry.FindMember("Value");
					if (componentName.empty() || fieldName.empty() || !value) {
						continue;
					}

					ApplyOverridePath(prefabEntityValue, componentName + "." + fieldName, *value);
				}
			}
		}
	} // namespace

	bool SceneSerializer::LoadFromFile(Scene& scene, const std::string& path) {
		FUNKE_PROFILE_SCOPE("SceneSerializer::LoadFromFile");
		try {
			if (!File::Exists(path)) {
				FNK_CORE_WARN_TAG("SceneSerializer", "Scene file not found: {}", path);
				return false;
			}

			// Before the read: an edit landing mid-read must still show up as a change afterwards.
			KnownFileStamps::Record(path);
			std::vector<std::uint8_t> bytes;
			{
				FUNKE_PROFILE_SCOPE("SceneSerializer::ReadFileBytes");
				bytes = File::ReadAllBytes(path);
			}
			if (bytes.empty()) {
				FNK_CORE_ERROR_TAG("SceneSerializer", "Failed to read scene (empty file): {}", path);
				return false;
			}

			// Binary scenes stream entities one at a time (no whole-file DOM); JSON keeps the
			// plain parse-then-walk path.
			if (SceneSerializerStorage::IsBinaryData(bytes)) {
				return DeserializeSceneStreamingBinary(scene, bytes, path);
			}

			Value root;
			{
				FUNKE_PROFILE_SCOPE("SceneSerializer::ParseJson");
				std::string parseError;
				const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
				if (!Json::TryParse(text, root, &parseError) || !root.IsObject()) {
					FNK_CORE_ERROR_TAG("SceneSerializer", "Failed to parse scene {}: {}", path, parseError);
					return false;
				}
			}

			return DeserializeScene(scene, root, path);
		}
		catch (const std::exception& exception) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Load failed: {}", exception.what());
			return false;
		}
		catch (...) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Load failed with an unknown exception");
			return false;
		}
	}

	bool SceneSerializer::ProcessSceneHeader(
		Scene& scene,
		const Json::Value& header,
		std::string_view source,
		bool& restartLifecycle) {
		const std::string sourcePath(source);
		const std::string serializedName = GetStringMember(header, "name");
		if (!scene.BeginContentReplacement(restartLifecycle)) {
			return false;
		}
		// Prefer the serialized name when present â€” file-stem fallback is only for
		// legacy scenes that never wrote a "name" field. Previously the file stem
		// always won, making the serialized "name" a write-only field.
		if (!serializedName.empty()) {
			scene.SetName(serializedName);
		}
		else if (!sourcePath.empty()) {
			scene.SetName(Path::ToUtf8(Path::FromUtf8(sourcePath).stem()));
		}

		const uint64_t sceneId = GetUInt64Member(header, "sceneId", 0);
		if (sceneId != 0) {
			scene.SetSceneId(UUID(sceneId));
		}

		if (const Value* systemsValue = GetArrayMember(header, "systems")) {
			for (const Value& systemValue : systemsValue->GetArray()) {
				// Two accepted shapes for forward / backward compatibility:
				//   1. "MySceneSystem"                                        (legacy / no overrides)
				//   2. {"className": "MySceneSystem", "enabled": false,
				//      "fields": { ... }}                                    (with authored state)
				std::string className;
				bool enabled = true;
				const Value* fieldsValue = nullptr;

				if (systemValue.IsString()) {
					className = systemValue.AsStringOr();
				}
				else if (systemValue.IsObject()) {
					if (const Value* nameNode = systemValue.FindMember("className")) {
						className = nameNode->AsStringOr();
					}
					if (const Value* enabledNode = systemValue.FindMember("enabled")) {
						enabled = enabledNode->AsBoolOr(true);
					}
					fieldsValue = systemValue.FindMember("fields");
				}

				if (className.empty()) continue;
				scene.AddSceneSystem(className);
				scene.SetSceneSystemEnabled(className, enabled);

				if (fieldsValue && fieldsValue->IsObject()) {
					for (const auto& [fieldName, fieldValueNode] : fieldsValue->GetObject()) {
						scene.SetSceneSystemFieldValue(className, fieldName, fieldValueNode.AsStringOr());
					}
				}
			}
		}

		return true;
	}

	bool SceneSerializer::ProcessLoadedEntity(Scene& scene, const Json::Value& entityValue, SceneLoadState& state) {
		if (!entityValue.IsObject()) {
			return false;
		}
		EntityHandle handle = DeserializeEntity(scene, entityValue);
		if (handle == entt::null) return false;

		// Accepts string or numeric ids like "uuid" does; malformed ids read as 0 and the entity becomes a root.
		const uint64_t parentUuid = GetUInt64Member(entityValue, "parentUuid", 0);
		if (parentUuid != 0) state.pendingParents.emplace_back(handle, parentUuid);

		const int childIdx = GetIntMember(entityValue, "childIndex", -1);
		if (childIdx >= 0) {
			state.childIndexByEntity[static_cast<uint32_t>(handle)] = childIdx;
		}
		return true;
	}

	void SceneSerializer::FinalizeSceneLoad(Scene& scene, const SceneLoadState& state, bool restartLifecycle) {
		// Pass 2 wires parentUuid links; pass 3 re-sorts children by authored childIndex.
		const auto& pendingParents = state.pendingParents;
		const auto& childIndexByEntity = state.childIndexByEntity;
		if (!pendingParents.empty()) {
			std::unordered_map<uint64_t, EntityHandle> byUuid;
			byUuid.reserve(pendingParents.size() * 2);
			for (auto e : scene.GetRegistry().view<UUIDComponent>()) {
				const uint64_t uuid = static_cast<uint64_t>(scene.GetRegistry().get<UUIDComponent>(e).Id);
				byUuid[uuid] = e;
			}
			std::unordered_set<uint32_t> parentsTouched;
			parentsTouched.reserve(pendingParents.size());
			for (const auto& [childHandle, parentUuid] : pendingParents) {
				auto it = byUuid.find(parentUuid);
				if (it == byUuid.end() || !scene.IsValid(it->second) || !scene.IsValid(childHandle)) continue;
				Entity child = scene.GetEntity(childHandle);
				Entity parent = scene.GetEntity(it->second);
				child.SetParent(parent);
				parentsTouched.insert(static_cast<uint32_t>(it->second));
			}

			auto& registry = scene.GetRegistry();
			for (uint32_t parentRaw : parentsTouched) {
				EntityHandle parentEntity = static_cast<EntityHandle>(parentRaw);
				if (!registry.valid(parentEntity)) continue;
				if (!registry.all_of<HierarchyComponent>(parentEntity)) continue;
				auto& hc = registry.get<HierarchyComponent>(parentEntity);
				if (hc.Children.size() < 2) continue;

				bool anyAuthored = false;
				for (EntityHandle child : hc.Children) {
					if (childIndexByEntity.find(static_cast<uint32_t>(child)) != childIndexByEntity.end()) {
						anyAuthored = true;
						break;
					}
				}
				if (!anyAuthored) continue;

				std::stable_sort(hc.Children.begin(), hc.Children.end(),
					[&](EntityHandle a, EntityHandle b) {
						auto ia = childIndexByEntity.find(static_cast<uint32_t>(a));
						auto ib = childIndexByEntity.find(static_cast<uint32_t>(b));
						const int va = (ia != childIndexByEntity.end()) ? ia->second : std::numeric_limits<int>::max();
						const int vb = (ib != childIndexByEntity.end()) ? ib->second : std::numeric_limits<int>::max();
						return va < vb;
					});
			}
		}

		scene.RunPendingEntityRefFixups();

		scene.ClearDirty();
		scene.EndContentReplacement(restartLifecycle);
	}

	bool SceneSerializer::CommitStagedScene(Scene& scene, Scene& stagedScene, std::string_view source) {
		if (scene.IsLoaded() && !scene.IsLifecycleSuspended()) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Loaded scene replacement requires an owner-level staged swap");
			return false;
		}
		Json::Value previousState;
		Json::Value canonicalPreviousState;
		Json::Value nextState;
		try {
			previousState = SerializeScene(scene);
			nextState = SerializeScene(stagedScene);
			SceneLoadStage previousStage(scene);
			if (!DeserializeSceneInPlace(previousStage.Get(), previousState, {}, false)
				|| !ValidateStagedEntityIdentities(previousStage.Get(), {})) {
				FNK_CORE_ERROR_TAG("SceneSerializer", "Current scene cannot be staged for a safe transaction");
				return false;
			}
			canonicalPreviousState = SerializeScene(previousStage.Get());
		}
		catch (const std::exception& exception) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Could not snapshot scene transaction: {}", exception.what());
			return false;
		}
		catch (...) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Could not snapshot scene transaction due to an unknown exception");
			return false;
		}

		const bool wasLoaded = scene.IsLoaded();
		const bool wasLifecycleSuspended = scene.IsLifecycleSuspended();
		const bool wasDirty = scene.IsDirty();
		const bool wasUiDirty = scene.IsUIDirty();
		const std::string previousName = scene.GetName();
		const UUID previousSceneId = scene.GetSceneId();
		auto restorePreviousState = [&]() {
			bool restored = false;
			try {
				restored = DeserializeSceneInPlace(scene, canonicalPreviousState, {}, false)
					&& ValidateStagedEntityIdentities(scene, {});
			}
			catch (const std::exception& exception) {
				FNK_CORE_ERROR_TAG("SceneSerializer", "Scene transaction rollback failed: {}", exception.what());
			}
			catch (...) {
				FNK_CORE_ERROR_TAG("SceneSerializer", "Scene transaction rollback failed with an unknown exception");
			}
			scene.m_IsLoaded = wasLoaded;
			scene.m_LifecycleSuspended = wasLifecycleSuspended;
			try {
				scene.SetName(previousName);
			}
			catch (...) {
				restored = false;
			}
			scene.SetSceneId(previousSceneId);
			scene.m_Dirty = wasDirty;
			scene.m_UIDirty = wasUiDirty;
			return restored;
		};

		try {
			if (DeserializeSceneInPlace(scene, nextState, source, true)
				&& ValidateStagedEntityIdentities(scene, source)) {
				return true;
			}
		}
		catch (const std::exception& exception) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Scene transaction commit failed: {}", exception.what());
		}
		catch (...) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Scene transaction commit failed with an unknown exception");
		}

		if (!restorePreviousState()) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Scene transaction could not restore its previous state");
		}
		return false;
	}

	bool SceneSerializer::DeserializeScene(Scene& scene, const Json::Value& root, std::string_view source) {
		if (scene.IsLoaded() && !scene.IsLifecycleSuspended()) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Loaded scene replacement requires an owner-level staged swap");
			return false;
		}
		if (!ValidateSceneRoot(root, source, true)) return false;
		try {
			SceneLoadStage stagedScene(scene);
			if (!DeserializeSceneInPlace(stagedScene.Get(), root, source, false)) return false;
			if (!ValidateStagedEntityIdentities(stagedScene.Get(), source)) return false;
			return CommitStagedScene(scene, stagedScene.Get(), source);
		}
		catch (const std::exception& exception) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Scene validation failed: {}", exception.what());
			return false;
		}
		catch (...) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Scene validation failed with an unknown exception");
			return false;
		}
	}

	bool SceneSerializer::DeserializeSceneInPlace(
		Scene& scene,
		const Json::Value& root,
		std::string_view source,
		bool logSuccess) {
		FUNKE_PROFILE_SCOPE("SceneSerializer::DeserializeScene");
		if (!ValidateSceneRoot(root, source, true)) return false;

		bool restartLifecycle = false;
		if (!ProcessSceneHeader(scene, root, source, restartLifecycle)) return false;

		// Two-pass load: pass 1 creates all entities, passes 2-3 (in FinalizeSceneLoad) wire parents / child order.
		SceneLoadState state;
		bool entitiesComplete = true;
		if (const Value* entitiesValue = GetArrayMember(root, "entities")) {
			FUNKE_PROFILE_SCOPE("SceneSerializer::DeserializeEntities");
			// Pre-size storages and identity maps to the known entity count so the create loop
			// doesn't repeatedly rehash the GUID/runtime maps or grow the hot component pools.
			const std::size_t entityCount = entitiesValue->GetArray().size();
			scene.ReserveForLoad<
				EntityMetaDataComponent,
				UUIDComponent,
#if FUNKE_WITH_2D
				Transform2DComponent,
#endif
				NameComponent>(entityCount);
			state.pendingParents.reserve(entityCount);
			state.childIndexByEntity.reserve(entityCount);
			for (const Value& entityValue : entitiesValue->GetArray()) {
				entitiesComplete = ProcessLoadedEntity(scene, entityValue, state) && entitiesComplete;
			}
		}
		else {
			if (!source.empty()) {
				FNK_CORE_WARN_TAG("SceneSerializer", "No entities array in scene file: {}", std::string(source));
			}
			else {
				FNK_CORE_WARN_TAG("SceneSerializer", "No entities array in scene data");
			}
		}

		FinalizeSceneLoad(scene, state, restartLifecycle);
		if (entitiesComplete && logSuccess) {
			FNK_CORE_INFO_TAG("SceneSerializer", "Loaded scene: {}", scene.GetName());
		}
		return entitiesComplete;
	}

	bool SceneSerializer::DeserializeSceneStreamingBinary(
		Scene& scene,
		const std::vector<std::uint8_t>& bytes,
		std::string_view source) {
		if (scene.IsLoaded() && !scene.IsLifecycleSuspended()) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Loaded scene replacement requires an owner-level staged swap");
			return false;
		}
		try {
			SceneLoadStage stagedScene(scene);
			if (!DeserializeSceneStreamingBinaryInPlace(stagedScene.Get(), bytes, source)) return false;
			if (!ValidateStagedEntityIdentities(stagedScene.Get(), source)) return false;
			return CommitStagedScene(scene, stagedScene.Get(), source);
		}
		catch (const std::exception& exception) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Binary scene validation failed: {}", exception.what());
			return false;
		}
		catch (...) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Binary scene validation failed with an unknown exception");
			return false;
		}
	}

	bool SceneSerializer::DeserializeSceneStreamingBinaryInPlace(
		Scene& scene,
		const std::vector<std::uint8_t>& bytes,
		std::string_view source) {
		FUNKE_PROFILE_SCOPE("SceneSerializer::DeserializeSceneStreamingBinary");

		Value header;
		SceneSerializerStorage::BinarySceneEntityCursor cursor;
		std::string decodeError;
		{
			FUNKE_PROFILE_SCOPE("SceneSerializer::BeginBinaryScene");
			if (!SceneSerializerStorage::BeginBinaryScene(bytes, header, cursor, &decodeError) || !header.IsObject()) {
				FNK_CORE_ERROR_TAG("SceneSerializer", "Failed to decode binary scene {}: {}",
					std::string(source), decodeError);
				return false;
			}
		}
		if (!ValidateSceneRoot(header, source, false)) return false;
		if (!cursor.bytes) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Binary scene {} is missing its entities array", std::string(source));
			return false;
		}

		bool restartLifecycle = false;
		if (!ProcessSceneHeader(scene, header, source, restartLifecycle)) return false;

		SceneLoadState state;
		bool streamComplete = true;
		if (cursor.remaining > 0) {
			FUNKE_PROFILE_SCOPE("SceneSerializer::DeserializeEntities");
			const std::size_t entityCount = cursor.remaining;
			scene.ReserveForLoad<
				EntityMetaDataComponent,
				UUIDComponent,
#if FUNKE_WITH_2D
				Transform2DComponent,
#endif
				NameComponent>(entityCount);
			state.pendingParents.reserve(entityCount);
			state.childIndexByEntity.reserve(entityCount);

			// Only one entity subtree is materialized at a time — that is the win over the
			// whole-file DOM: not 300k fat Json::Value subtrees alive (and cache-cold) at once.
			Value entityValue;
			std::string entityError;
			while (SceneSerializerStorage::ReadNextBinaryEntity(cursor, entityValue, &entityError)) {
				streamComplete = ProcessLoadedEntity(scene, entityValue, state) && streamComplete;
			}
			if (!entityError.empty()) {
				FNK_CORE_ERROR_TAG("SceneSerializer", "Binary scene {} entity stream aborted: {}",
					std::string(source), entityError);
				streamComplete = false;
			}
		}
		else {
			FNK_CORE_WARN_TAG("SceneSerializer", "No entities in scene file: {}", std::string(source));
		}

		FinalizeSceneLoad(scene, state, restartLifecycle);
		return streamComplete;
	}

	EntityHandle SceneSerializer::DeserializeEntity(Scene& scene, const Json::Value& entityValue) {
		if (!entityValue.IsObject()) {
			return entt::null;
		}

		const std::string originText = GetStringMember(entityValue, "Origin", "Scene");
		EntityOrigin origin = EntityOriginFromString(originText);
		if (origin == EntityOrigin::Runtime) {
			// Returning null here fails the whole scene load; keep the entity as scene content instead.
			FNK_CORE_WARN_TAG("SceneSerializer", "Entity has unsupported Origin '{}'; loading it as a scene entity", originText);
			origin = EntityOrigin::Scene;
		}

		try {
			if (origin == EntityOrigin::Prefab) {
				return DeserializePrefabInstance(scene, entityValue);
			}
			return DeserializeFullEntity(scene, entityValue, EntityOrigin::Scene);
		}
		catch (const std::exception& e) {
			FNK_CORE_ERROR_TAG("SceneSerializer",
				"DeserializeEntity threw, partial entity rolled back by inner guard: {}", e.what());
			return entt::null;
		}
		catch (...) {
			FNK_CORE_ERROR_TAG("SceneSerializer",
				"DeserializeEntity threw unknown exception, partial entity rolled back by inner guard");
			return entt::null;
		}
	}

	namespace {
		// Data is kept so nothing is lost; the warning tells the user why those entities look inert.
		void WarnInactiveModuleComponent(const ComponentInfo& info) {
			static std::atomic<bool> s_Warned[std::size(k_AllEngineModules)];
			if (s_Warned[static_cast<size_t>(*info.engineModule)].exchange(true)) return;
			const std::string_view id = EngineModules::GetId(*info.engineModule);
			FNK_CORE_WARN_TAG("SceneSerializer", "The scene uses {} components (e.g. {}), but the project does not enable "
				"the {} module. They are kept but not rendered or simulated; enable the module in Project Settings > "
				"Build > Engine Modules.", id, info.serializedName, id);
		}

#if FUNKE_WITH_EDITOR
		// Scenes from before a conflict existed (e.g. a 3D component on a 2D entity) still load; say which part is inert.
		void WarnConflictingComponents(Scene& scene, EntityHandle entity) {
			const auto [first, second] = SceneManager::GetComponentRegistryOrBuiltIn().FindConflictingPair(scene.GetEntity(entity));
			if (!first) return;
			NameComponent* name = nullptr;
			scene.TryGetComponent<NameComponent>(entity, name);
			const std::string entityName = name ? name->Name : std::to_string(static_cast<uint32_t>(entity));
			// Opening a scene, entering and leaving play mode all reload it; report each mix once per session.
			static std::mutex s_ReportedMutex;
			static std::unordered_set<std::string> s_Reported;
			{
				std::scoped_lock lock(s_ReportedMutex);
				if (!s_Reported.insert(scene.GetName() + '/' + entityName + '/' + first->serializedName + '/' + second->serializedName).second) return;
			}
			FNK_CORE_WARN_TAG("SceneSerializer", "Entity '{}' in scene '{}' combines '{}' and '{}', which can't work together; remove one of them.",
				entityName, scene.GetName(), first->displayName, second->displayName);
		}
#endif

		// Iterates the entity's members (O(1) lookup each) rather than every registered type.
		void ReadRegistryComponents(Scene& scene, EntityHandle entity, const Json::Value& entityValue, bool first) {
			Entity entityWrapper = scene.GetEntity(entity);
			const ComponentRegistry& componentRegistry = SceneManager::GetComponentRegistryOrBuiltIn();
			for (const auto& [memberName, memberValue] : entityValue.GetObject()) {
				const ComponentInfo* info = componentRegistry.FindBySerializedName(memberName);
				if (!info || info->serializeFirst != first) continue;
				if (!info->deserialize || !info->has || !info->add) continue;
				if (!info->IsModuleActive()) WarnInactiveModuleComponent(*info);
				try {
					if (!info->has(entityWrapper)) {
						info->add(entityWrapper);
					}
					info->deserialize(entityWrapper, memberValue);
				} catch (const std::exception& e) {
					// The entity handle lets the editor's log clicker jump to the offending entity.
					FNK_CORE_ERROR_TAG("SceneSerializer",
						"Component '{}' deserialize threw on entity {}: {}",
						info->serializedName, static_cast<uint32_t>(entity), e.what());
				} catch (...) {
					FNK_CORE_ERROR_TAG("SceneSerializer",
						"Component '{}' deserialize threw an unknown exception on entity {}",
						info->serializedName, static_cast<uint32_t>(entity));
				}
			}
		}
	}

	EntityHandle SceneSerializer::DeserializeFullEntity(
		Scene& scene,
		const Json::Value& entityValue,
		EntityOrigin origin,
		uint64_t prefabGuid) {
		if (!entityValue.IsObject()) {
			return entt::null;
		}

		const std::string name = GetStringMember(entityValue, "name", "");
		const bool wasDirty = scene.IsDirty();
		const bool wasUiDirty = scene.IsUIDirty();
		// No default transform: the snapshot's own transform (if any) is added by ReadRegistryComponents,
		// so UI entities don't sprout one on reload.
		const EntityHandle entity = scene.CreateEmptyEntity().GetHandle();
		if (!name.empty()) {
			scene.AddComponent<NameComponent>(entity, name);
		}
		EntityRollbackGuard rollback(scene, entity, wasDirty, wasUiDirty);

		uint64_t savedSceneGuid = GetUInt64Member(entityValue, "SceneGUID", 0);
		if (savedSceneGuid == 0) {
			savedSceneGuid = GetUInt64Member(entityValue, "uuid", 0);
		}
		const uint64_t savedRuntimeId = origin == EntityOrigin::Prefab
			? 0
			: GetUInt64Member(entityValue, "RuntimeID", 0);
		scene.SetEntityMetaData(
			entity,
			origin,
			AssetGUID(prefabGuid),
			AssetGUID(savedSceneGuid),
			savedRuntimeId);

		if (origin == EntityOrigin::Prefab && scene.HasComponent<PrefabInstanceComponent>(entity)) {
			scene.GetComponent<PrefabInstanceComponent>(entity).SourceEntityId = savedSceneGuid;
		}

		ReadRegistryComponents(scene, entity, entityValue, true);
		// The rect is read with the UI components below; present it now so 2D construct hooks see a UI entity.
		if (GetObjectMember(entityValue, "RectTransform2D")) {
			scene.AddComponent<RectTransform2DComponent>(entity);
		}

		if (GetBoolMember(entityValue, "static", false)) {
			scene.AddComponent<StaticTag>(entity);
		}
		if (GetBoolMember(entityValue, "disabled", false)) {
			scene.AddComponent<DisabledTag>(entity);
		}
		if (GetBoolMember(entityValue, "deadly", false)) {
			scene.AddComponent<DeadlyTag>(entity);
		}

		if (const Value* textValue = GetObjectMember(entityValue, "TextRenderer")) {
			auto& text = scene.AddComponent<TextRendererComponent>(entity);
			text.Text = GetStringMember(*textValue, "text", text.Text);
			const std::string fontAssetStr = GetStringMember(*textValue, "fontAsset", std::string{});
			if (!fontAssetStr.empty()) {
				try {
					text.FontAssetId = UUID(std::stoull(fontAssetStr));
				} catch (...) {
					text.FontAssetId = UUID(0);
				}
			}
			text.FontSize = GetFloatMember(*textValue, "fontSize", text.FontSize);
			text.Color.r = GetFloatMember(*textValue, "r", text.Color.r);
			text.Color.g = GetFloatMember(*textValue, "g", text.Color.g);
			text.Color.b = GetFloatMember(*textValue, "b", text.Color.b);
			text.Color.a = GetFloatMember(*textValue, "a", text.Color.a);
			text.LetterSpacing = GetFloatMember(*textValue, "letterSpacing", text.LetterSpacing);
			text.LineSpacing = GetFloatMember(*textValue, "lineSpacing", text.LineSpacing);
			text.HAlign = static_cast<TextAlignment>(GetIntMember(*textValue, "alignment", static_cast<int>(text.HAlign)));
			text.VAlign = static_cast<TextVerticalAlignment>(GetIntMember(*textValue, "valign", static_cast<int>(text.VAlign)));
			text.WrapMode = static_cast<TextWrapMode>(GetIntMember(*textValue, "wrapMode", static_cast<int>(text.WrapMode)));
			text.AutoSize = GetBoolMember(*textValue, "autoSize", false);
			// WrapWidth field removed; old scenes that wrote it just
			// pass through unread.
			text.Margin.x = GetFloatMember(*textValue, "marginL", text.Margin.x);
			text.Margin.y = GetFloatMember(*textValue, "marginT", text.Margin.y);
			text.Margin.z = GetFloatMember(*textValue, "marginR", text.Margin.z);
			text.Margin.w = GetFloatMember(*textValue, "marginB", text.Margin.w);
			text.SortingOrder = static_cast<int16_t>(GetIntMember(*textValue, "sortOrder", text.SortingOrder));
			text.SortingLayer = static_cast<uint8_t>(GetIntMember(*textValue, "sortLayer", text.SortingLayer));
			text.ResolvedFont = FontHandle{};
		}

#if FUNKE_WITH_2D
		if (const Value* rigidbodyValue = GetObjectMember(entityValue, "Rigidbody2D")) {
			auto& rigidbody = scene.AddComponent<Rigidbody2DComponent>(entity);
			rigidbody.SetBodyType(static_cast<BodyType>(GetIntMember(*rigidbodyValue, "bodyType", static_cast<int>(BodyType::Dynamic))));
			rigidbody.SetGravityScale(GetFloatMember(*rigidbodyValue, "gravityScale", 1.0f));
			// Only pin the mass when it was explicitly overridden. Legacy files lack the
			// key (-> false), so they keep Box2D's density*area auto mass as before.
			if (GetBoolMember(*rigidbodyValue, "massOverridden", false)) {
				rigidbody.SetMass(GetFloatMember(*rigidbodyValue, "mass", 1.0f));
			}
			// Constraints. Missing keys default to false so older scenes
			// load with no surprises (`SetFreeze*` no-ops the body when the
			// flag is already false, which it is on a freshly-created body).
			rigidbody.SetFreezePositionX(GetBoolMember(*rigidbodyValue, "freezePositionX", false));
			rigidbody.SetFreezePositionY(GetBoolMember(*rigidbodyValue, "freezePositionY", false));
			rigidbody.SetFreezeRotation(GetBoolMember(*rigidbodyValue, "freezeRotation", false));
		}

		if (const Value* colliderValue = GetObjectMember(entityValue, "BoxCollider2D")) {
			auto& boxCollider = scene.AddComponent<BoxCollider2DComponent>(entity);
			const Vec2 savedCenter{
				GetFloatMember(*colliderValue, "centerX", 0.0f),
				GetFloatMember(*colliderValue, "centerY", 0.0f)
			};
			boxCollider.SetCenter(savedCenter, scene);

			// UI entities (RectTransform) can carry a collider without a Transform2D; GetComponent would throw and fail the load.
			static const Transform2DComponent k_DefaultTransform{};
			const Transform2DComponent* transformPtr = scene.GetRegistry().try_get<Transform2DComponent>(entity);
			const Transform2DComponent& transform = transformPtr ? *transformPtr : k_DefaultTransform;
			boxCollider.SetScale(ReadBoxColliderLocalSize(*colliderValue, transform), scene);
			boxCollider.SetSensor(GetBoolMember(*colliderValue, "sensor", false), scene);
			boxCollider.SetFriction(GetFloatMember(*colliderValue, "friction", boxCollider.GetFriction()));
			boxCollider.SetBounciness(GetFloatMember(*colliderValue, "bounciness", boxCollider.GetBounciness()));
			boxCollider.SetCollisionMask(GetUInt64Member(*colliderValue, "collisionMask", boxCollider.GetCollisionMask()));
			boxCollider.SetLayer(GetUInt64Member(*colliderValue, "layer", boxCollider.GetLayer()));
			boxCollider.SetRegisterContacts(GetBoolMember(*colliderValue, "registerContacts", boxCollider.CanRegisterContacts()));
		}

		if (const Value* colliderValue = GetObjectMember(entityValue, "CircleCollider2D")) {
			auto& circleCollider = scene.AddComponent<CircleCollider2DComponent>(entity);
			const Vec2 savedCenter{
				GetFloatMember(*colliderValue, "centerX", 0.0f),
				GetFloatMember(*colliderValue, "centerY", 0.0f)
			};
			circleCollider.SetCenter(savedCenter, scene);
			circleCollider.SetRadius(GetFloatMember(*colliderValue, "radius", 0.5f), scene);
			circleCollider.SetSensor(GetBoolMember(*colliderValue, "sensor", false), scene);
			circleCollider.SetFriction(GetFloatMember(*colliderValue, "friction", circleCollider.GetFriction()));
			circleCollider.SetBounciness(GetFloatMember(*colliderValue, "bounciness", circleCollider.GetBounciness()));
			circleCollider.SetCollisionMask(GetUInt64Member(*colliderValue, "collisionMask", circleCollider.GetCollisionMask()));
			circleCollider.SetLayer(GetUInt64Member(*colliderValue, "layer", circleCollider.GetLayer()));
			circleCollider.SetRegisterContacts(GetBoolMember(*colliderValue, "registerContacts", circleCollider.CanRegisterContacts()));
		}

		if (const Value* colliderValue = GetObjectMember(entityValue, "PolygonCollider2D")) {
			auto& polygonCollider = scene.AddComponent<PolygonCollider2DComponent>(entity);

			// Restore custom point list if present, otherwise the default
			// regular pentagon installed by the constructor stays in place.
			if (const Value* pointsArr = colliderValue->FindMember("points"); pointsArr && pointsArr->IsArray()) {
				const auto& arr = pointsArr->GetArray();
				// Box2D rejects polygons over B2_MAX_POLYGON_VERTICES (8). Cap the
				// reserve too — without this, a hostile/corrupt prefab with a
				// 100M-element points array allocates ~1.6 GB before the first
				// element is read.
				constexpr std::size_t k_MaxPolygonVertices = 8;
				if (arr.size() > k_MaxPolygonVertices) {
					FNK_CORE_WARN_TAG("Serialization", "Polygon collider point count {} exceeds max {}; truncating", arr.size(), k_MaxPolygonVertices);
				}
				const std::size_t take = arr.size() < k_MaxPolygonVertices ? arr.size() : k_MaxPolygonVertices;
				std::vector<Vec2> points;
				points.reserve(take);
				for (std::size_t i = 0; i < take; ++i) {
					const Value& pt = arr[i];
					points.push_back(Vec2{
						GetFloatMember(pt, "x", 0.0f),
						GetFloatMember(pt, "y", 0.0f)
					});
				}
				if (!points.empty()) {
					polygonCollider.SetPoints(points, scene);
				}
			}

			polygonCollider.SetSize(Vec2{
				GetFloatMember(*colliderValue, "sizeX", 1.0f),
				GetFloatMember(*colliderValue, "sizeY", 1.0f)
			}, scene);
			polygonCollider.SetCenter(Vec2{
				GetFloatMember(*colliderValue, "centerX", 0.0f),
				GetFloatMember(*colliderValue, "centerY", 0.0f)
			}, scene);
			polygonCollider.SetSensor(GetBoolMember(*colliderValue, "sensor", false), scene);
			polygonCollider.SetFriction(GetFloatMember(*colliderValue, "friction", polygonCollider.GetFriction()));
			polygonCollider.SetBounciness(GetFloatMember(*colliderValue, "bounciness", polygonCollider.GetBounciness()));
			polygonCollider.SetCollisionMask(GetUInt64Member(*colliderValue, "collisionMask", polygonCollider.GetCollisionMask()));
			polygonCollider.SetLayer(GetUInt64Member(*colliderValue, "layer", polygonCollider.GetLayer()));
			polygonCollider.SetRegisterContacts(GetBoolMember(*colliderValue, "registerContacts", polygonCollider.CanRegisterContacts()));
		}
#endif

		if (const Value* audioValue = GetObjectMember(entityValue, "AudioSource")) {
			auto& audioSource = scene.AddComponent<AudioSourceComponent>(entity);
			audioSource.SetVolume(GetFloatMember(*audioValue, "volume", 1.0f));
			audioSource.SetPitch(GetFloatMember(*audioValue, "pitch", 1.0f));
			audioSource.SetLoop(GetBoolMember(*audioValue, "loop", false));
			audioSource.SetPlayOnAwake(GetBoolMember(*audioValue, "playOnAwake", false));

			UUID audioAssetId = UUID(0);
			const AudioHandle handle = LoadAudioFromValue(*audioValue, "clipAsset", "clip", &audioAssetId);
			audioSource.SetAudioAssetId(audioAssetId);
			if (handle.IsValid()) {
				audioSource.SetAudioHandle(handle, audioAssetId);
			}
		}

#if FUNKE_WITH_2D
		if (const Value* bodyValue = GetObjectMember(entityValue, "FastBody2D")) {
			auto& body = scene.AddComponent<FastBody2DComponent>(entity);
			body.Type = static_cast<IndexPhys::BodyType>(GetIntMember(*bodyValue, "type", 1));
			body.Mass = GetFloatMember(*bodyValue, "mass", 1.0f);
			body.UseGravity = GetBoolMember(*bodyValue, "useGravity", true);
			body.BoundaryCheck = GetBoolMember(*bodyValue, "boundaryCheck", false);

			if (body.m_Body) {
				body.m_Body->SetBodyType(body.Type);
				body.m_Body->SetMass(body.Mass);
				body.m_Body->SetGravityEnabled(body.UseGravity);
				body.m_Body->SetBoundaryCheckEnabled(body.BoundaryCheck);
			}
		}

		if (const Value* colliderValue = GetObjectMember(entityValue, "FastBoxCollider2D")) {
			auto& collider = scene.AddComponent<FastBoxCollider2DComponent>(entity);
			// SetHalfExtents (not a raw m_Collider->SetHalfExtents) so the
			// scale composed by OnConstruct stays applied — writing the JSON
			// extents straight onto the IndexPhys collider strips the
			// Transform2D.Scale and SyncWithTransform's short-circuit then
			// leaves it stripped because m_LastAppliedScale already equals
			// the current scale.
			collider.SetHalfExtents({
				GetFloatMember(*colliderValue, "halfX", 0.5f),
				GetFloatMember(*colliderValue, "halfY", 0.5f)
			});
		}

		if (const Value* colliderValue = GetObjectMember(entityValue, "FastCircleCollider2D")) {
			auto& collider = scene.AddComponent<FastCircleCollider2DComponent>(entity);
			// SetRadius (not raw m_Collider->SetRadius) so OnConstruct's scaled value isn't blown away.
			collider.SetRadius(GetFloatMember(*colliderValue, "radius", 0.5f));
		}
#endif

		if (const Value* rectValue = GetObjectMember(entityValue, "RectTransform2D")) {
			auto& rectTransform = scene.GetComponent<RectTransform2DComponent>(entity);
			rectTransform.AnchorMin.x        = GetFloatMember(*rectValue, "anchorMinX", 0.5f);
			rectTransform.AnchorMin.y        = GetFloatMember(*rectValue, "anchorMinY", 0.5f);
			rectTransform.AnchorMax.x        = GetFloatMember(*rectValue, "anchorMaxX", 0.5f);
			rectTransform.AnchorMax.y        = GetFloatMember(*rectValue, "anchorMaxY", 0.5f);
			rectTransform.Pivot.x            = GetFloatMember(*rectValue, "pivotX", 0.5f);
			rectTransform.Pivot.y            = GetFloatMember(*rectValue, "pivotY", 0.5f);
			rectTransform.AnchoredPosition.x = GetFloatMember(*rectValue, "posX", 0.0f);
			rectTransform.AnchoredPosition.y = GetFloatMember(*rectValue, "posY", 0.0f);
			// Back-compat: pre-anchor saves used "width"/"height" instead of sizeX/sizeY.
			rectTransform.SizeDelta.x        = GetFloatMember(*rectValue, "sizeX",
				GetFloatMember(*rectValue, "width", 100.0f));
			rectTransform.SizeDelta.y        = GetFloatMember(*rectValue, "sizeY",
				GetFloatMember(*rectValue, "height", 100.0f));
			rectTransform.LocalRotation      = GetFloatMember(*rectValue, "rotation", 0.0f);
			rectTransform.LocalScale.x       = GetFloatMember(*rectValue, "scaleX", 1.0f);
			rectTransform.LocalScale.y       = GetFloatMember(*rectValue, "scaleY", 1.0f);
			rectTransform.IgnoreSafeArea     = GetBoolMember(*rectValue, "ignoreSafeArea", false);
			{
				const bool fillParent = GetBoolMember(*rectValue, "fillParent", false);
				rectTransform.FillWidth  = fillParent || GetBoolMember(*rectValue, "fillWidth", false);
				rectTransform.FillHeight = fillParent || GetBoolMember(*rectValue, "fillHeight", false);
			}
			// Seed the world cache from the local values so a one-frame
			// pre-layout read returns sensible defaults — UILayoutSystem
			// overwrites these on the first tick (matches Transform2D).
			rectTransform.Rotation           = rectTransform.LocalRotation;
			rectTransform.Scale              = rectTransform.LocalScale;
		}

		if (const Value* imageValue = GetObjectMember(entityValue, "Image")) {
			auto& image = scene.AddComponent<ImageComponent>(entity);
			image.Color.r = GetFloatMember(*imageValue, "r", 1.0f);
			image.Color.g = GetFloatMember(*imageValue, "g", 1.0f);
			image.Color.b = GetFloatMember(*imageValue, "b", 1.0f);
			image.Color.a = GetFloatMember(*imageValue, "a", 1.0f);
			image.SortingOrder = static_cast<int16_t>(GetIntMember(*imageValue, "sortOrder", 0));
			image.SortingLayer = static_cast<uint8_t>(GetIntMember(*imageValue, "sortLayer", 0));
			image.FilterMode = static_cast<Filter>(GetIntMember(*imageValue, "filterMode",
				static_cast<int>(image.FilterMode)));

			image.TextureHandle = LoadTextureFromValue(
				*imageValue,
				"textureAsset",
				"texture",
				image.FilterMode,
				Wrap::Clamp,
				Wrap::Clamp,
				&image.TextureAssetId);
			if (image.FilterMode != Filter::Default) {
				if (auto* tex = TextureManager::GetTexture(image.TextureHandle); tex) {
					tex->SetFilter(image.FilterMode);
				}
			}
			image.SpriteName = GetStringMember(*imageValue, "spriteName", std::string{});
			ReadImageSlicingAndMaterial(*imageValue, image);
		}

		ScriptComponent* scriptComponent = nullptr;
		auto getOrCreateScriptComponent = [&]() -> ScriptComponent& {
			if (!scriptComponent) {
				if (scene.HasComponent<ScriptComponent>(entity)) {
					scriptComponent = &scene.GetComponent<ScriptComponent>(entity);
				}
				else {
					scriptComponent = &scene.AddComponent<ScriptComponent>(entity);
				}
			}
			return *scriptComponent;
		};

		if (const Value* scriptsValue = GetArrayMember(entityValue, "Scripts")) {
			auto& scripts = getOrCreateScriptComponent();
			for (const Value& scriptValue : scriptsValue->GetArray()) {
				if (scriptValue.IsString()) {
					scripts.AddScript(scriptValue.AsStringOr(), ScriptType::Unknown);
					continue;
				}

				if (!scriptValue.IsObject()) {
					continue;
				}

				std::string className = GetStringMember(scriptValue, "className");
				if (className.empty()) {
					className = GetStringMember(scriptValue, "class");
				}
				if (className.empty()) {
					continue;
				}

				scripts.AddScript(
					className,
					ScriptTypeFromString(GetStringMember(scriptValue, "type")));
			}
		}

		if (const Value* componentsValue = GetArrayMember(entityValue, "ManagedComponents")) {
			auto& components = getOrCreateScriptComponent();
			for (const Value& componentValue : componentsValue->GetArray()) {
				if (componentValue.IsString()) {
					components.AddManagedComponent(componentValue.AsStringOr());
					continue;
				}

				if (!componentValue.IsObject()) {
					continue;
				}

				std::string className = GetStringMember(componentValue, "className");
				if (className.empty()) {
					className = GetStringMember(componentValue, "class");
				}
				if (!className.empty()) {
					components.AddManagedComponent(className);
				}
			}
		}

		if (const Value* fieldsByClass = GetObjectMember(entityValue, "ScriptFields")) {
			if (scriptComponent || scene.HasComponent<ScriptComponent>(entity)) {
				auto& fieldsComponent = getOrCreateScriptComponent();
				int populated = 0;
				int skippedNoScript = 0;
				// Clone/paste: script entity-ref fields are stored as id strings and resolved in C# at bind
				// time (remap gone), so collect the entity-typed keys for a deferred remap translation.
				std::vector<std::string> entityRefFieldKeys;
				for (const auto& [className, fieldsValue] : fieldsByClass->GetObject()) {
					if ((!fieldsComponent.HasScript(className) && !fieldsComponent.HasManagedComponent(className))
						|| !fieldsValue.IsArray()) {
						++skippedNoScript;
						continue;
					}

					for (const Value& fieldValue : fieldsValue.GetArray()) {
						if (!fieldValue.IsObject()) {
							continue;
						}

						const std::string fieldName = GetStringMember(fieldValue, "name");
						if (fieldName.empty()) {
							continue;
						}

						const Value* valueValue = fieldValue.FindMember("value");
						if (!valueValue) {
							continue;
						}

						const std::string key = className + "." + fieldName;
						const std::string stored = ValueToFieldString(*valueValue);
						fieldsComponent.PendingFieldValues[key] = stored;
						++populated;

						// Entity refs are a bare decimal id ([type]=="entity") or "<id>:<ComponentName>"
						// (component ref); "prefab:<guid>" is an asset ref and must never be remapped.
						if (scene.IsEntityRefRemapActive() && !stored.empty() && stored != "0"
							&& stored.rfind("prefab:", 0) != 0) {
							const std::size_t colon = stored.find(':');
							bool isEntityRef = false;
							if (colon != std::string::npos) {
								const std::string idPart = stored.substr(0, colon);
								isEntityRef = !idPart.empty()
									&& idPart.find_first_not_of("0123456789") == std::string::npos;
							}
							else {
								isEntityRef = GetStringMember(fieldValue, "type") == "entity";
							}
							if (isEntityRef) {
								entityRefFieldKeys.push_back(key);
							}
						}
					}
				}
				// Only the skip case is worth a line; logging every scripted entity stalls large scene loads.
				if (skippedNoScript > 0) {
					FNK_CORE_INFO_TAG("SceneSerializer",
						"Entity '{}' ScriptFields: {} populated, {} class(es) skipped (script not registered yet)",
						name, populated, skippedNoScript);
				}

				if (scene.IsEntityRefRemapActive() && !entityRefFieldKeys.empty()) {
					Scene* scenePtr = &scene;
					ScriptComponent* scPtr = &fieldsComponent;
					scene.DeferEntityRefFixup([scenePtr, scPtr, keys = std::move(entityRefFieldKeys)]() {
						for (const std::string& k : keys) {
							auto it = scPtr->PendingFieldValues.find(k);
							if (it == scPtr->PendingFieldValues.end()) {
								continue;
							}
							std::string& val = it->second;
							if (val.empty() || val == "0" || val.rfind("prefab:", 0) == 0) {
								continue;
							}
							const std::size_t colon = val.find(':');
							try {
								if (colon != std::string::npos) {
									const uint64_t id = std::stoull(val.substr(0, colon));
									if (id != 0) val = std::to_string(scenePtr->TranslateEntityRef(id)) + val.substr(colon);
								}
								else {
									const uint64_t id = std::stoull(val);
									if (id != 0) val = std::to_string(scenePtr->TranslateEntityRef(id));
								}
							}
							catch (...) {}
						}
					});
				}
			}
		}

		if (scriptComponent || scene.HasComponent<ScriptComponent>(entity)) {
			const ComponentRegistry* componentRegistry = nullptr;
			if (Application* app = Application::GetInstance(); app && app->GetSceneManager()) {
				componentRegistry = &SceneManager::GetComponentRegistry();
			}

			auto& dynamicValuesComponent = getOrCreateScriptComponent();
			for (const ScriptInstance& instance : dynamicValuesComponent.Scripts) {
				const std::string& className = instance.GetClassName();
				if (className.empty()) continue;

				const Value* componentValue = entityValue.FindMember(className);
				if (!componentValue || !componentValue->IsObject()) continue;

				const ComponentInfo* info = componentRegistry
					? componentRegistry->FindBySerializedName(className)
					: nullptr;
				if (info && info->isDynamic && info->deserialize) {
					continue;
				}

				dynamicValuesComponent.PendingDynamicComponentValues[className] =
					Json::Stringify(*componentValue, false);
			}
		}

		ReadRegistryComponents(scene, entity, entityValue, false);

		// Materialize native/dynamic (IComponent) rows listed only in the entity's
		// managed script/component list. The registry-driven loop above adds any
		// dynamic component that carries a top-level data block; a prefab saved
		// before its type was registered (or with no live row at save time) has
		// only the "Scripts"/"ManagedComponents" name entry and no data block.
		// ScriptEngine::RestoreDynamicComponentsForScene fixes that, but it only runs
		// at scene-start/assembly-reload — never on a runtime Instantiate — so without
		// this the component is silently absent on spawned prefabs and Scene.Query<T>()
		// / GetComponent<T> miss it. Adds a default-initialized row when missing.
		if (Application* app = Application::GetInstance();
			app && app->GetSceneManager() && scene.HasComponent<ScriptComponent>(entity)) {
			Entity entityWrapper = scene.GetEntity(entity);
			const ComponentRegistry& registry = SceneManager::GetComponentRegistry();
			const ScriptComponent& sc = scene.GetComponent<ScriptComponent>(entity);
			auto materializeDynamic = [&](const std::string& className) {
				if (className.empty()) return;
				const ComponentInfo* info = registry.FindBySerializedName(className);
				if (!info || !info->isDynamic || !info->add || !info->has) return;
				if (!info->has(entityWrapper)) {
					info->add(entityWrapper);
				}
			};
			for (const ScriptInstance& instance : sc.Scripts) materializeDynamic(instance.GetClassName());
			for (const std::string& className : sc.ManagedComponents) materializeDynamic(className);
		}

#if FUNKE_WITH_EDITOR
		WarnConflictingComponents(scene, entity);
#endif
		rollback.Commit();
		return entity;
	}

	EntityHandle SceneSerializer::DeserializeEntityTree(
		Scene& scene,
		const std::vector<Json::Value>& entityValues,
		EntityOrigin origin,
		uint64_t prefabGuid,
		bool preserveSerializedIdentity,
		bool* outComplete) {
		if (entityValues.empty()) {
			if (outComplete) *outComplete = false;
			return entt::null;
		}
		if (outComplete) *outComplete = false;
		if (!ValidateEntityTreeSourceIds(entityValues, origin, scene.IsEntityRefRemapActive())) {
			return entt::null;
		}
		EntityTreeRollbackGuard rollback(scene, true, scene.GetPendingEntityRefFixupCount());
		rollback.Reserve(entityValues.size());
		bool complete = true;

		std::vector<std::pair<EntityHandle, uint64_t>> pendingParents;
		std::vector<std::pair<uint64_t, uint64_t>> pendingRemaps;
		std::unordered_map<uint64_t, EntityHandle> entitiesBySourceId;
		std::unordered_set<uint64_t> remappedSourceIds;
		pendingParents.reserve(entityValues.size());
		pendingRemaps.reserve(entityValues.size());
		entitiesBySourceId.reserve(entityValues.size());
		remappedSourceIds.reserve(entityValues.size());

		EntityHandle root = entt::null;
		for (const Value& sourceEntityValue : entityValues) {
			if (!sourceEntityValue.IsObject()) {
				complete = false;
				continue;
			}

			const uint64_t sourceId = GetPrefabSourceId(sourceEntityValue);
			const uint64_t parentId = GetUInt64Member(sourceEntityValue, "parentUuid", 0);

			Value entityValue = sourceEntityValue;
			if (preserveSerializedIdentity) {
				RemovePrefabRuntimeIdentityMembers(entityValue);
			}
			else {
				RemoveEntityIdentityMembers(entityValue);
			}

			EntityOrigin entityOrigin = origin;
			uint64_t entityPrefabGuid = prefabGuid;
			if (origin == EntityOrigin::Scene && prefabGuid == 0) {
				const std::string originStr = GetStringMember(sourceEntityValue, "Origin", "");
				if (!originStr.empty() && EntityOriginFromString(originStr) == EntityOrigin::Prefab) {
					const uint64_t detectedPrefabGuid = GetUInt64Member(sourceEntityValue, "PrefabGUID", 0);
					if (detectedPrefabGuid != 0) {
						entityOrigin = EntityOrigin::Prefab;
						entityPrefabGuid = detectedPrefabGuid;
					}
				}
			}

			EntityHandle handle = DeserializeFullEntity(scene, entityValue, entityOrigin, entityPrefabGuid);
			if (handle == entt::null) {
				complete = false;
				continue;
			}
			rollback.Track(handle);

			// Map a serialized source id to the freshly-built copy's persistent id so in-component refs into
			// the subtree retarget to THIS copy's children instead of the originals/template. Clone/paste
			// carries the original persistent id as "sourceUuid"; prefab instantiation instead keys on the
			// template entity's own "uuid" (its source id), preserved by RemovePrefabRuntimeIdentityMembers —
			// NOT GetPrefabSourceId/SourceEntityId, which can diverge from the id the template's refs stored.
			if (scene.IsEntityRefRemapActive()) {
				uint64_t sourceRefId = GetUInt64Member(sourceEntityValue, "sourceUuid", 0);
				if (sourceRefId == 0 && entityOrigin == EntityOrigin::Prefab) {
					sourceRefId = GetUInt64Member(sourceEntityValue, "uuid", 0);
				}
				if (sourceRefId != 0) {
					if (!remappedSourceIds.insert(sourceRefId).second) {
						complete = false;
					}
					else pendingRemaps.emplace_back(sourceRefId, scene.GetEntityPersistentID(handle));
				}
			}

			if (entityOrigin == EntityOrigin::Prefab
				&& scene.HasComponent<PrefabInstanceComponent>(handle)) {
				const uint64_t sourceEntityId = GetUInt64Member(sourceEntityValue, "SourceEntityId", 0);
				if (sourceEntityId != 0) {
					scene.GetComponent<PrefabInstanceComponent>(handle).SourceEntityId = sourceEntityId;
				}
			}

			if (root == entt::null) {
				root = handle;
			}

			if (sourceId != 0) {
				if (!entitiesBySourceId.emplace(sourceId, handle).second) {
					complete = false;
				}
			}

			if (parentId != 0) {
				pendingParents.emplace_back(handle, parentId);
			}
		}

		for (const auto& [childHandle, parentId] : pendingParents) {
			auto parentIt = entitiesBySourceId.find(parentId);
			if (parentIt == entitiesBySourceId.end()) {
				complete = false;
				continue;
			}
			if (!scene.IsValid(childHandle) || !scene.IsValid(parentIt->second)) {
				complete = false;
				continue;
			}

			scene.GetEntity(childHandle).SetParent(scene.GetEntity(parentIt->second));
		}

		// Fixup queue is NOT drained here — outermost callers drain after the full batch to avoid resolving refs too early.
		const bool treeComplete = complete && root != entt::null;
		if (outComplete) *outComplete = treeComplete;
		if (!treeComplete) return entt::null;
		for (const auto& [sourceId, persistentId] : pendingRemaps) {
			scene.RegisterEntityRefRemap(sourceId, persistentId);
		}
		rollback.Commit();
		return root;
	}

	EntityHandle SceneSerializer::RestoreEntityTree(Scene& scene, const std::vector<Json::Value>& entityValues) {
		return DeserializeEntityTree(scene, entityValues, EntityOrigin::Scene, 0, /*preserveSerializedIdentity=*/true);
	}

	EntityHandle SceneSerializer::DeserializeEntityFromValue(
		Scene& scene,
		const Json::Value& entityValue,
		EntityOrigin origin) {
		if (!entityValue.IsObject()) {
			return entt::null;
		}

		// Active across the whole rebuild + fixup drain: DeserializeEntityTree records original->clone id
		// pairs, and the deferred ref/event/script-field fixups translate through them before this returns.
		Scene::EntityRefRemapScope remapScope(scene);

		EntityHandle root = entt::null;
		PrefabDefinition definition;
		if (HasPrefabEntityPayload(entityValue) && ReadPrefabDefinitionFromRoot(entityValue, definition)) {
			const bool isClipboardEntity = GetBoolMember(entityValue, "ClipboardEntity", false)
				|| GetBoolMember(entityValue, "Clipboard", false);
			root = DeserializeEntityTree(scene, definition.Entities, origin, 0, !isClipboardEntity);
		}
		else {
			uint64_t sourceRefId = GetUInt64Member(entityValue, "sourceUuid", 0);
			if (sourceRefId == 0) sourceRefId = GetUInt64Member(entityValue, "uuid", 0);
			Value entityCopy = entityValue;
			RemoveEntityIdentityMembers(entityCopy);
			root = DeserializeFullEntity(scene, entityCopy, origin);
			if (root != entt::null && sourceRefId != 0) {
				scene.RegisterEntityRefRemap(sourceRefId, scene.GetEntityPersistentID(root));
			}
		}

		scene.RunPendingEntityRefFixups();
		return root;
	}

	bool SceneSerializer::HasPrefabEntityPayload(const Json::Value& root) {
		if (!root.IsObject()) return false;
		if (const Value* entities = GetArrayMember(root, "Entities")) {
			for (const Value& entityValue : entities->GetArray()) {
				if (entityValue.IsObject()) return true;
			}
		}
		return GetObjectMember(root, "Entity") || GetObjectMember(root, "prefab");
	}

	bool SceneSerializer::DeserializeComponent(Scene& scene, EntityHandle entity, std::string_view componentName, const Json::Value& componentValue) {
		if (entity == entt::null || !scene.IsValid(entity) || componentName.empty() || !componentValue.IsObject()) {
			return false;
		}

		const std::string component(componentName);

		// Paste/Reset run outside a load batch, so drain the ref fixups queued here before returning:
		// they hold raw pointers into component storage that a later delete would invalidate.
		struct FixupDrain {
			Scene& TargetScene;
			std::size_t Start;
			~FixupDrain() {
				try { TargetScene.RunPendingEntityRefFixupsFrom(Start); }
				catch (...) { TargetScene.DiscardPendingEntityRefFixupsFrom(Start); }
			}
		} fixupDrain{ scene, scene.GetPendingEntityRefFixupCount() };

		if (component == "TextRenderer") {
			auto& text = scene.HasComponent<TextRendererComponent>(entity)
				? scene.GetComponent<TextRendererComponent>(entity)
				: scene.AddComponent<TextRendererComponent>(entity);
			text.Text = GetStringMember(componentValue, "text", text.Text);
			const std::string fontAssetStr = GetStringMember(componentValue, "fontAsset", std::string{});
			if (!fontAssetStr.empty()) {
				try {
					text.FontAssetId = UUID(std::stoull(fontAssetStr));
				} catch (...) {
					text.FontAssetId = UUID(0);
				}
			}
			text.FontSize = GetFloatMember(componentValue, "fontSize", text.FontSize);
			text.Color.r = GetFloatMember(componentValue, "r", text.Color.r);
			text.Color.g = GetFloatMember(componentValue, "g", text.Color.g);
			text.Color.b = GetFloatMember(componentValue, "b", text.Color.b);
			text.Color.a = GetFloatMember(componentValue, "a", text.Color.a);
			text.LetterSpacing = GetFloatMember(componentValue, "letterSpacing", text.LetterSpacing);
			text.LineSpacing = GetFloatMember(componentValue, "lineSpacing", text.LineSpacing);
			text.HAlign = static_cast<TextAlignment>(GetIntMember(componentValue, "alignment", static_cast<int>(text.HAlign)));
			text.VAlign = static_cast<TextVerticalAlignment>(GetIntMember(componentValue, "valign", static_cast<int>(text.VAlign)));
			text.WrapMode = static_cast<TextWrapMode>(GetIntMember(componentValue, "wrapMode", static_cast<int>(text.WrapMode)));
			text.AutoSize = GetBoolMember(componentValue, "autoSize", false);
			// WrapWidth field removed; old scenes that wrote it just
			// pass through unread.
			text.Margin.x = GetFloatMember(componentValue, "marginL", text.Margin.x);
			text.Margin.y = GetFloatMember(componentValue, "marginT", text.Margin.y);
			text.Margin.z = GetFloatMember(componentValue, "marginR", text.Margin.z);
			text.Margin.w = GetFloatMember(componentValue, "marginB", text.Margin.w);
			text.SortingOrder = static_cast<int16_t>(GetIntMember(componentValue, "sortOrder", text.SortingOrder));
			text.SortingLayer = static_cast<uint8_t>(GetIntMember(componentValue, "sortLayer", text.SortingLayer));
			text.ResolvedFont = FontHandle{};
			return true;
		}

#if FUNKE_WITH_2D
		if (component == "Rigidbody2D") {
			auto& rigidbody = scene.HasComponent<Rigidbody2DComponent>(entity)
				? scene.GetComponent<Rigidbody2DComponent>(entity)
				: scene.AddComponent<Rigidbody2DComponent>(entity);
			rigidbody.SetBodyType(static_cast<BodyType>(GetIntMember(componentValue, "bodyType", static_cast<int>(BodyType::Dynamic))));
			rigidbody.SetGravityScale(GetFloatMember(componentValue, "gravityScale", 1.0f));
			if (GetBoolMember(componentValue, "massOverridden", false)) {
				rigidbody.SetMass(GetFloatMember(componentValue, "mass", 1.0f));
			}
			rigidbody.SetFreezePositionX(GetBoolMember(componentValue, "freezePositionX", false));
			rigidbody.SetFreezePositionY(GetBoolMember(componentValue, "freezePositionY", false));
			rigidbody.SetFreezeRotation(GetBoolMember(componentValue, "freezeRotation", false));
			return true;
		}

		if (component == "BoxCollider2D") {
			auto& boxCollider = scene.HasComponent<BoxCollider2DComponent>(entity)
				? scene.GetComponent<BoxCollider2DComponent>(entity)
				: scene.AddComponent<BoxCollider2DComponent>(entity);
			const Vec2 savedCenter{
				GetFloatMember(componentValue, "centerX", 0.0f),
				GetFloatMember(componentValue, "centerY", 0.0f)
			};
			boxCollider.SetCenter(savedCenter, scene);

			// UI entities (RectTransform) can carry a collider without a Transform2D; GetComponent would throw and fail the load.
			static const Transform2DComponent k_DefaultTransform{};
			const Transform2DComponent* transformPtr = scene.GetRegistry().try_get<Transform2DComponent>(entity);
			const Transform2DComponent& transform = transformPtr ? *transformPtr : k_DefaultTransform;
			boxCollider.SetScale(ReadBoxColliderLocalSize(componentValue, transform), scene);
			boxCollider.SetSensor(GetBoolMember(componentValue, "sensor", false), scene);
			boxCollider.SetFriction(GetFloatMember(componentValue, "friction", boxCollider.GetFriction()));
			boxCollider.SetBounciness(GetFloatMember(componentValue, "bounciness", boxCollider.GetBounciness()));
			boxCollider.SetCollisionMask(GetUInt64Member(componentValue, "collisionMask", boxCollider.GetCollisionMask()));
			boxCollider.SetLayer(GetUInt64Member(componentValue, "layer", boxCollider.GetLayer()));
			boxCollider.SetRegisterContacts(GetBoolMember(componentValue, "registerContacts", boxCollider.CanRegisterContacts()));
			return true;
		}

		if (component == "CircleCollider2D") {
			auto& circleCollider = scene.HasComponent<CircleCollider2DComponent>(entity)
				? scene.GetComponent<CircleCollider2DComponent>(entity)
				: scene.AddComponent<CircleCollider2DComponent>(entity);
			circleCollider.SetCenter(Vec2{
				GetFloatMember(componentValue, "centerX", 0.0f),
				GetFloatMember(componentValue, "centerY", 0.0f)
			}, scene);
			circleCollider.SetRadius(GetFloatMember(componentValue, "radius", 0.5f), scene);
			circleCollider.SetSensor(GetBoolMember(componentValue, "sensor", false), scene);
			circleCollider.SetFriction(GetFloatMember(componentValue, "friction", circleCollider.GetFriction()));
			circleCollider.SetBounciness(GetFloatMember(componentValue, "bounciness", circleCollider.GetBounciness()));
			circleCollider.SetCollisionMask(GetUInt64Member(componentValue, "collisionMask", circleCollider.GetCollisionMask()));
			circleCollider.SetLayer(GetUInt64Member(componentValue, "layer", circleCollider.GetLayer()));
			circleCollider.SetRegisterContacts(GetBoolMember(componentValue, "registerContacts", circleCollider.CanRegisterContacts()));
			return true;
		}

		if (component == "PolygonCollider2D") {
			auto& polygonCollider = scene.HasComponent<PolygonCollider2DComponent>(entity)
				? scene.GetComponent<PolygonCollider2DComponent>(entity)
				: scene.AddComponent<PolygonCollider2DComponent>(entity);

			if (const Value* pointsArr = componentValue.FindMember("points"); pointsArr && pointsArr->IsArray()) {
				constexpr std::size_t k_MaxPolygonVertices = 8;
				const auto& arr = pointsArr->GetArray();
				if (arr.size() > k_MaxPolygonVertices) {
					FNK_CORE_WARN_TAG("Serialization", "Polygon collider point count {} exceeds max {}; truncating", arr.size(), k_MaxPolygonVertices);
				}
				const std::size_t take = arr.size() < k_MaxPolygonVertices ? arr.size() : k_MaxPolygonVertices;
				std::vector<Vec2> points;
				points.reserve(take);
				for (std::size_t i = 0; i < take; ++i) {
					const Value& pt = arr[i];
					points.push_back(Vec2{
						GetFloatMember(pt, "x", 0.0f),
						GetFloatMember(pt, "y", 0.0f)
					});
				}
				if (!points.empty()) {
					polygonCollider.SetPoints(points, scene);
				}
			}

			polygonCollider.SetSize(Vec2{
				GetFloatMember(componentValue, "sizeX", 1.0f),
				GetFloatMember(componentValue, "sizeY", 1.0f)
			}, scene);
			polygonCollider.SetCenter(Vec2{
				GetFloatMember(componentValue, "centerX", 0.0f),
				GetFloatMember(componentValue, "centerY", 0.0f)
			}, scene);
			polygonCollider.SetSensor(GetBoolMember(componentValue, "sensor", false), scene);
			polygonCollider.SetFriction(GetFloatMember(componentValue, "friction", polygonCollider.GetFriction()));
			polygonCollider.SetBounciness(GetFloatMember(componentValue, "bounciness", polygonCollider.GetBounciness()));
			polygonCollider.SetCollisionMask(GetUInt64Member(componentValue, "collisionMask", polygonCollider.GetCollisionMask()));
			polygonCollider.SetLayer(GetUInt64Member(componentValue, "layer", polygonCollider.GetLayer()));
			polygonCollider.SetRegisterContacts(GetBoolMember(componentValue, "registerContacts", polygonCollider.CanRegisterContacts()));
			return true;
		}
#endif

		if (component == "AudioSource") {
			auto& audioSource = scene.HasComponent<AudioSourceComponent>(entity)
				? scene.GetComponent<AudioSourceComponent>(entity)
				: scene.AddComponent<AudioSourceComponent>(entity);
			audioSource.SetVolume(GetFloatMember(componentValue, "volume", 1.0f));
			audioSource.SetPitch(GetFloatMember(componentValue, "pitch", 1.0f));
			audioSource.SetLoop(GetBoolMember(componentValue, "loop", false));
			audioSource.SetPlayOnAwake(GetBoolMember(componentValue, "playOnAwake", false));

			UUID audioAssetId = UUID(0);
			const AudioHandle handle = LoadAudioFromValue(componentValue, "clipAsset", "clip", &audioAssetId);
			audioSource.SetAudioHandle(handle, audioAssetId);
			return true;
		}

#if FUNKE_WITH_2D
		if (component == "FastBody2D") {
			auto& body = scene.HasComponent<FastBody2DComponent>(entity)
				? scene.GetComponent<FastBody2DComponent>(entity)
				: scene.AddComponent<FastBody2DComponent>(entity);
			body.Type = static_cast<IndexPhys::BodyType>(GetIntMember(componentValue, "type", 1));
			body.Mass = GetFloatMember(componentValue, "mass", 1.0f);
			body.UseGravity = GetBoolMember(componentValue, "useGravity", true);
			body.BoundaryCheck = GetBoolMember(componentValue, "boundaryCheck", false);

			if (body.m_Body) {
				body.m_Body->SetBodyType(body.Type);
				body.m_Body->SetMass(body.Mass);
				body.m_Body->SetGravityEnabled(body.UseGravity);
				body.m_Body->SetBoundaryCheckEnabled(body.BoundaryCheck);
			}
			return true;
		}

		if (component == "FastBoxCollider2D") {
			auto& collider = scene.HasComponent<FastBoxCollider2DComponent>(entity)
				? scene.GetComponent<FastBoxCollider2DComponent>(entity)
				: scene.AddComponent<FastBoxCollider2DComponent>(entity);
			collider.SetHalfExtents({
				GetFloatMember(componentValue, "halfX", 0.5f),
				GetFloatMember(componentValue, "halfY", 0.5f)
			});
			return true;
		}

		if (component == "FastCircleCollider2D") {
			auto& collider = scene.HasComponent<FastCircleCollider2DComponent>(entity)
				? scene.GetComponent<FastCircleCollider2DComponent>(entity)
				: scene.AddComponent<FastCircleCollider2DComponent>(entity);
			collider.SetRadius(GetFloatMember(componentValue, "radius", 0.5f));
			return true;
		}
#endif

		if (component == "RectTransform2D") {
			auto& rectTransform = scene.HasComponent<RectTransform2DComponent>(entity)
				? scene.GetComponent<RectTransform2DComponent>(entity)
				: scene.AddComponent<RectTransform2DComponent>(entity);
			rectTransform.AnchorMin.x        = GetFloatMember(componentValue, "anchorMinX", 0.5f);
			rectTransform.AnchorMin.y        = GetFloatMember(componentValue, "anchorMinY", 0.5f);
			rectTransform.AnchorMax.x        = GetFloatMember(componentValue, "anchorMaxX", 0.5f);
			rectTransform.AnchorMax.y        = GetFloatMember(componentValue, "anchorMaxY", 0.5f);
			rectTransform.Pivot.x            = GetFloatMember(componentValue, "pivotX", 0.5f);
			rectTransform.Pivot.y            = GetFloatMember(componentValue, "pivotY", 0.5f);
			rectTransform.AnchoredPosition.x = GetFloatMember(componentValue, "posX", 0.0f);
			rectTransform.AnchoredPosition.y = GetFloatMember(componentValue, "posY", 0.0f);
			rectTransform.SizeDelta.x        = GetFloatMember(componentValue, "sizeX",
				GetFloatMember(componentValue, "width", 100.0f));
			rectTransform.SizeDelta.y        = GetFloatMember(componentValue, "sizeY",
				GetFloatMember(componentValue, "height", 100.0f));
			rectTransform.LocalRotation      = GetFloatMember(componentValue, "rotation", 0.0f);
			rectTransform.LocalScale.x       = GetFloatMember(componentValue, "scaleX", 1.0f);
			rectTransform.LocalScale.y       = GetFloatMember(componentValue, "scaleY", 1.0f);
			rectTransform.IgnoreSafeArea     = GetBoolMember(componentValue, "ignoreSafeArea", false);
			{
				const bool fillParent = GetBoolMember(componentValue, "fillParent", false);
				rectTransform.FillWidth  = fillParent || GetBoolMember(componentValue, "fillWidth", false);
				rectTransform.FillHeight = fillParent || GetBoolMember(componentValue, "fillHeight", false);
			}
			// Seed the world cache from the local values so a one-frame
			// pre-layout read returns sensible defaults — UILayoutSystem
			// overwrites these on the first tick (matches Transform2D).
			rectTransform.Rotation           = rectTransform.LocalRotation;
			rectTransform.Scale              = rectTransform.LocalScale;
			return true;
		}

		if (component == "Image") {
			auto& image = scene.HasComponent<ImageComponent>(entity)
				? scene.GetComponent<ImageComponent>(entity)
				: scene.AddComponent<ImageComponent>(entity);
			image.Color.r = GetFloatMember(componentValue, "r", 1.0f);
			image.Color.g = GetFloatMember(componentValue, "g", 1.0f);
			image.Color.b = GetFloatMember(componentValue, "b", 1.0f);
			image.Color.a = GetFloatMember(componentValue, "a", 1.0f);
			image.SortingOrder = static_cast<int16_t>(GetIntMember(componentValue, "sortOrder", image.SortingOrder));
			image.SortingLayer = static_cast<uint8_t>(GetIntMember(componentValue, "sortLayer", image.SortingLayer));
			image.FilterMode = static_cast<Filter>(GetIntMember(componentValue, "filterMode",
				static_cast<int>(image.FilterMode)));

			image.TextureHandle = LoadTextureFromValue(
				componentValue,
				"textureAsset",
				"texture",
				image.FilterMode,
				Wrap::Clamp,
				Wrap::Clamp,
				&image.TextureAssetId);
			if (image.FilterMode != Filter::Default) {
				if (auto* tex = TextureManager::GetTexture(image.TextureHandle); tex) {
					tex->SetFilter(image.FilterMode);
				}
			}
			image.SpriteName = GetStringMember(componentValue, "spriteName", std::string{});
			ReadImageSlicingAndMaterial(componentValue, image);
			return true;
		}

		// Not hand-serialized: resolve through the registry by serializedName.
		{
			Entity entityWrapper = scene.GetEntity(entity);
			bool handled = false;
			SceneManager::GetComponentRegistryOrBuiltIn().ForEachComponentInfo(
				[&](const std::type_index&, const ComponentInfo& info) {
					if (handled) return;
					if (!info.deserialize || !info.has || !info.add) return;
					if (info.serializedName != componentName) return;
					try {
						if (!info.has(entityWrapper)) {
							info.add(entityWrapper);
						}
						info.deserialize(entityWrapper, componentValue);
						handled = true;
					} catch (const std::exception& e) {
						FNK_CORE_ERROR_TAG("SceneSerializer",
							"Package component '{}' deserialize (single) threw: {}",
							info.serializedName, e.what());
					} catch (...) {
						FNK_CORE_ERROR_TAG("SceneSerializer",
							"Package component '{}' deserialize (single) threw an unknown exception",
							info.serializedName);
					}
				});
			if (handled) return true;
		}

		return false;
	}

	bool SceneSerializer::ResetComponent(Scene& scene, EntityHandle entity, std::string_view componentName) {
		Value defaults = Value::MakeObject();
		return DeserializeComponent(scene, entity, componentName, defaults);
	}

	EntityHandle SceneSerializer::DeserializePrefabInstance(Scene& scene, const Json::Value& entityValue) {
		const uint64_t prefabGuid = ResolvePrefabGuid(entityValue);
		if (prefabGuid == 0) {
			FNK_CORE_WARN_TAG("SceneSerializer", "Prefab instance missing PrefabGUID");
			return entt::null;
		}
		const uint64_t instanceUuid = GetUInt64Member(entityValue, "uuid", 0);
		if (instanceUuid != 0) {
			EntityHandle existingOwner = entt::null;
			if (scene.TryResolveEntityRef(instanceUuid, existingOwner)) {
				FNK_CORE_ERROR_TAG("SceneSerializer", "Prefab instance identity {} is already in use", instanceUuid);
				return entt::null;
			}
		}

		PrefabDefinition definition;
		if (!LoadPrefabDefinition(prefabGuid, definition)) {
			// Failing here would fail the whole scene load, leaving the user unable to open the scene to fix it.
			// Keep a placeholder that preserves the instance JSON (uuid, parent, overrides) for the next save.
			FNK_CORE_WARN_TAG("SceneSerializer",
				"Could not load prefab {}; keeping a placeholder so the scene still opens and the instance data is preserved",
				prefabGuid);
			Entity placeholder = scene.CreateEntity("Missing Prefab (" + std::to_string(prefabGuid) + ")");
			const EntityHandle handle = placeholder.GetHandle();
			scene.SetEntityMetaData(handle, EntityOrigin::Prefab, AssetGUID(prefabGuid), AssetGUID(0), instanceUuid);
			scene.GetRegistry().emplace_or_replace<UnresolvedPrefabInstanceComponent>(
				handle, UnresolvedPrefabInstanceComponent{ Json::Stringify(entityValue, false) });
			return handle;
		}

		if (!ApplyEntityOverrides(definition, entityValue)) return entt::null;

		// Retarget the template's internal entity refs (child-to-child, child-to-root) onto this instance's
		// own freshly-built entities. Drain ONLY the fixups this instance queued — scene load defers other
		// entities' refs to the same queue and resolves them in the final pass, so a full drain here would
		// resolve a sibling's forward ref before its target exists.
		const bool wasDirty = scene.IsDirty();
		const bool wasUiDirty = scene.IsUIDirty();
		const std::size_t fixupStart = scene.GetPendingEntityRefFixupCount();
		EntityHandle root = entt::null;
		{
			Scene::EntityRefRemapScope remapScope(scene);
			root = DeserializeEntityTree(scene, definition.Entities, EntityOrigin::Prefab, prefabGuid);
			if (root == entt::null) return entt::null;
			PrefabRefreshRollbackGuard rollback(scene, entt::null, root, 0,
				fixupStart, wasDirty, wasUiDirty);
			if (root != entt::null && instanceUuid != 0) {
				if (!scene.SetEntityPersistentID(root, instanceUuid)) {
					return entt::null;
				}
				const uint64_t templateRootId = GetUInt64Member(definition.Entities.front(), "uuid", 0);
				scene.RegisterEntityRefRemap(templateRootId, instanceUuid);
			}
			PinPrefabInstanceChildIds(scene, root);
			scene.RunPendingEntityRefFixupsFrom(fixupStart);
			rollback.Commit();
		}
		return root;
	}

	EntityHandle SceneSerializer::InstantiatePrefab(Scene& scene, uint64_t prefabGuid) {
		if (prefabGuid == 0 || AssetRegistry::GetKind(prefabGuid) != AssetKind::Prefab) {
			return entt::null;
		}

		PrefabTemplateCache& cache = PrefabTemplateCache::Get();
		if (const auto baked = cache.Find(prefabGuid); baked) {
			if (baked->bakeable) {
				EntityHandle root = cache.Hydrate(prefabGuid, scene);
				if (root != entt::null) {
					PinPrefabInstanceChildIds(scene, root);
					scene.RunPendingEntityRefFixups();
					return root;
				}
				}
		}

		PrefabDefinition definition;
		if (!LoadPrefabDefinition(prefabGuid, definition)) {
			return entt::null;
		}

		const std::size_t fixupCountBefore = scene.GetPendingEntityRefFixupCount();
		std::size_t fixupCountAfter = fixupCountBefore;
		EntityHandle root = entt::null;
		{
			// Retarget the template's internal entity refs onto this instance's children. The scope must stay
			// active across the drain: ref resolution is deferred and translates through the remap at drain time.
			Scene::EntityRefRemapScope remapScope(scene);
			root = DeserializeEntityTree(scene, definition.Entities, EntityOrigin::Prefab, prefabGuid);
			fixupCountAfter = scene.GetPendingEntityRefFixupCount();
			PinPrefabInstanceChildIds(scene, root);
			scene.RunPendingEntityRefFixupsFrom(fixupCountBefore);
		}

		if (root != entt::null && cache.Find(prefabGuid) == nullptr) {
			const std::size_t fixupsAdded =
				(fixupCountAfter >= fixupCountBefore) ? (fixupCountAfter - fixupCountBefore) : 0u;
			cache.CaptureFromLive(prefabGuid, scene, root, fixupsAdded);
		}

		// Awaken the freshly spawned subtree the way scene-start does, so a prefab instantiated at
		// runtime auto-plays its PlayOnAwake particles/audio instead of waiting for a start pass that
		// already ran. Only the slow path needs this: PlayOnAwake-bearing components aren't trivially
		// copyable, so such a prefab is never bakeable and always lands here, not in cache.Hydrate().
		if (root != entt::null && Application::GetIsPlaying()) {
			scene.PlayOnAwakeSubtree(root);
		}

		return root;
	}

	bool SceneSerializer::ApplyPrefabInstanceOverrides(Scene& scene, EntityHandle entity) {
		if (entity == entt::null || !scene.IsValid(entity) || scene.GetEntityOrigin(entity) != EntityOrigin::Prefab) {
			return false;
		}

		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entity));
		const std::string prefabPath = AssetRegistry::ResolvePath(prefabGuid);
		if (prefabGuid == 0 || prefabPath.empty()) {
			return false;
		}

		try {
			if (!SaveEntityToFile(scene, entity, prefabPath)) {
				return false;
			}
			// Invalidate inline to close the TOCTOU window before the FileWatcher fires.
			PrefabTemplateCache::Get().Invalidate(prefabGuid);
			AssetRegistry::MarkDirty();
			AssetRegistry::Sync();
			scene.MarkDirty();
			return true;
		}
		catch (const std::exception& exception) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "ApplyPrefabInstanceOverrides failed: {}", exception.what());
			return false;
		}
	}

	EntityHandle SceneSerializer::RevertPrefabInstanceOverride(
		Scene& scene,
		EntityHandle entity,
		const std::string& overridePath) {
		if (entity == entt::null || !scene.IsValid(entity) || scene.GetEntityOrigin(entity) != EntityOrigin::Prefab) {
			return entt::null;
		}

		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entity));
		if (prefabGuid == 0) {
			return entt::null;
		}

		// Empty path rebuilds before destroying the live subtree so malformed sources cannot erase it.
		if (overridePath.empty()) {
			EntityHandle root = GetPrefabInstanceRoot(scene, entity);
			if (root == entt::null) {
				return entt::null;
			}

			PrefabDefinition definition;
			if (!LoadPrefabDefinition(prefabGuid, definition) || definition.Entities.empty()) {
				return entt::null;
			}

			auto& registry = scene.GetRegistry();
			uint64_t preservedInstanceUuid = 0;
			if (registry.all_of<UUIDComponent>(root)) {
				preservedInstanceUuid = static_cast<uint64_t>(registry.get<UUIDComponent>(root).Id);
			}
			EntityHandle preservedParent = entt::null;
			if (registry.all_of<HierarchyComponent>(root)) {
				preservedParent = registry.get<HierarchyComponent>(root).Parent;
			}

			const bool wasDirty = scene.IsDirty();
			const bool wasUiDirty = scene.IsUIDirty();
			auto restoreDirtyState = [&]() {
				if (wasDirty) scene.MarkDirty(); else scene.ClearDirty();
				if (wasUiDirty) scene.MarkUIDirty(); else scene.ClearUIDirty();
			};
			const std::size_t fixupStart = scene.GetPendingEntityRefFixupCount();
			EntityHandle freshRoot = entt::null;
			try {
				// Rebuild resolves the template's internal refs against the fresh subtree; drain only this
				// rebuild's fixups so we don't disturb anything else still pending.
				Scene::EntityRefRemapScope remapScope(scene);
				freshRoot = DeserializeEntityTree(scene, definition.Entities, EntityOrigin::Prefab, prefabGuid);
				if (freshRoot == entt::null) {
					restoreDirtyState();
					return entt::null;
				}
				PrefabRefreshRollbackGuard rollback(scene, root, freshRoot, preservedInstanceUuid,
					fixupStart, wasDirty, wasUiDirty);
				if (preservedParent != entt::null && registry.valid(preservedParent)) {
					scene.GetEntity(freshRoot).SetParent(scene.GetEntity(preservedParent));
				}
				if (preservedInstanceUuid != 0) {
					if (!scene.SetEntityPersistentID(freshRoot, preservedInstanceUuid, root)) {
						return entt::null;
					}
					rollback.MarkIdentityTransferred();
					const uint64_t templateRootId = GetUInt64Member(definition.Entities.front(), "uuid", 0);
					scene.RegisterEntityRefRemap(templateRootId, preservedInstanceUuid);
				}
				scene.RunPendingEntityRefFixupsFrom(fixupStart);
				if (registry.all_of<UUIDComponent>(root)) {
					registry.get<UUIDComponent>(root).Id = UUID(0);
				}
				scene.DestroyEntity(root);
				rollback.Commit();
			}
			catch (const std::exception& exception) {
				restoreDirtyState();
				FNK_CORE_ERROR_TAG("SceneSerializer", "Prefab revert build failed: {}", exception.what());
				return entt::null;
			}
			catch (...) {
				restoreDirtyState();
				FNK_CORE_ERROR_TAG("SceneSerializer", "Prefab revert build failed with an unknown exception");
				return entt::null;
			}

			scene.MarkDirty();
			return freshRoot;
		}

		Value prefabEntityValue;
		if (!LoadPrefabEntityValue(prefabGuid, prefabEntityValue)) {
			return entt::null;
		}

		Value replacementValue = SerializeEntityFull(scene, entity);
		RemoveEntityIdentityMembers(replacementValue);

		const Value* baseValue = GetOverrideValueAtPath(prefabEntityValue, overridePath);
		if (!baseValue || !ApplyOverridePath(replacementValue, overridePath, *baseValue)) {
			return entt::null;
		}

		RemoveEntityIdentityMembers(replacementValue);
		const bool wasDirty = scene.IsDirty();
		const bool wasUiDirty = scene.IsUIDirty();
		const uint64_t preservedId = scene.GetEntityPersistentID(entity);
		EntityHandle preservedParent = entt::null;
		if (scene.HasComponent<HierarchyComponent>(entity)) {
			preservedParent = scene.GetComponent<HierarchyComponent>(entity).Parent;
		}
		const std::size_t fixupStart = scene.GetPendingEntityRefFixupCount();
		EntityHandle replacement = entt::null;
		try {
			replacement = DeserializeFullEntity(scene, replacementValue, EntityOrigin::Prefab, prefabGuid);
			if (replacement == entt::null) return entt::null;
			PrefabRefreshRollbackGuard rollback(scene, entity, replacement, preservedId,
				fixupStart, wasDirty, wasUiDirty);
			if (preservedParent != entt::null && scene.IsValid(preservedParent)) {
				scene.GetEntity(replacement).SetParent(scene.GetEntity(preservedParent));
			}
			if (preservedId != 0) {
				if (!scene.SetEntityPersistentID(replacement, preservedId, entity)) return entt::null;
				rollback.MarkIdentityTransferred();
			}
			scene.RunPendingEntityRefFixupsFrom(fixupStart);
			if (scene.HasComponent<UUIDComponent>(entity)) {
				scene.GetComponent<UUIDComponent>(entity).Id = UUID(0);
			}
			scene.DestroyEntity(entity);
			rollback.Commit();
		}
		catch (const std::exception& exception) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Prefab override revert failed: {}", exception.what());
			return entt::null;
		}
		catch (...) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "Prefab override revert failed with an unknown exception");
			return entt::null;
		}
		scene.MarkDirty();
		return replacement;
	}

	EntityHandle SceneSerializer::GetPrefabInstanceRoot(Scene& scene, EntityHandle entity) {
		if (entity == entt::null || !scene.IsValid(entity)) {
			return entt::null;
		}
		if (scene.GetEntityOrigin(entity) != EntityOrigin::Prefab) {
			return entt::null;
		}

		auto& registry = scene.GetRegistry();
		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entity));
		if (prefabGuid == 0) {
			return entt::null;
		}

		if (!registry.all_of<PrefabInstanceComponent>(entity)) {
			return entt::null;
		}

		// Walk up while still inside this prefab (same Origin+GUID); every node in the tree has PrefabInstanceComponent,
		// not just the root. Stop at a nested instance root so instance B parented under instance A stays its own instance.
		constexpr int k_MaxAncestorWalk = 1024;
		EntityHandle root = entity;
		for (int i = 0; i < k_MaxAncestorWalk; ++i) {
			if (IsPrefabInstanceRoot(scene, root)) {
				break;
			}
			const EntityHandle parent = GetSamePrefabParent(scene, root, prefabGuid);
			if (parent == entt::null) {
				break;
			}
			root = parent;
		}
		return root;
	}

	bool SceneSerializer::IsPrefabInstanceRoot(Scene& scene, EntityHandle entity) {
		if (entity == entt::null || !scene.IsValid(entity) || scene.GetEntityOrigin(entity) != EntityOrigin::Prefab) {
			return false;
		}
		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entity));
		if (GetSamePrefabParent(scene, entity, prefabGuid) == entt::null) {
			return true;
		}

		// Parent is the same prefab: either an internal child, or a separate instance dropped under another
		// instance. Every instance root carries the prefab root's source id, which the topmost same-prefab
		// ancestor (always a root) also carries; internal children have their own distinct source ids.
		constexpr int k_MaxAncestorWalk = 1024;
		EntityHandle top = entity;
		for (int i = 0; i < k_MaxAncestorWalk; ++i) {
			const EntityHandle parent = GetSamePrefabParent(scene, top, prefabGuid);
			if (parent == entt::null) break;
			top = parent;
		}
		const uint64_t sourceId = GetPrefabSourceIdOrZero(scene, entity);
		return sourceId != 0 && sourceId == GetPrefabSourceIdOrZero(scene, top);
	}

	bool SceneSerializer::HasPrefabInstanceOverrides(Scene& scene, EntityHandle entity) {
		const EntityHandle root = GetPrefabInstanceRoot(scene, entity);
		if (root == entt::null) {
			return false;
		}

		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(root));
		PrefabDefinition definition;
		if (!LoadPrefabDefinition(prefabGuid, definition) || definition.Entities.empty()) {
			// Orphan / unresolvable source — no overrides we can act on.
			return false;
		}

		Value rootOverrides;
		Value entityOverrides;
		BuildPrefabInstanceOverrideSet(scene, root, definition, rootOverrides, entityOverrides);
		return !rootOverrides.GetObject().empty() || !entityOverrides.GetObject().empty();
	}

	EntityHandle SceneSerializer::RefreshPrefabInstance(Scene& scene, EntityHandle existing,
		const Json::Value& previousSourceEntityValue) {
		if (existing == entt::null || !scene.IsValid(existing)) return entt::null;
		if (scene.GetEntityOrigin(existing) != EntityOrigin::Prefab) return entt::null;

		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(existing));
		if (prefabGuid == 0) return entt::null;

		// Orphaned prefab: leave instance untouched rather than corrupting it.
		PrefabDefinition newSource;
		if (!LoadPrefabDefinition(prefabGuid, newSource) || newSource.Entities.empty()) {
			return existing;
		}

		// Skip child entities (their SourceEntityId != prefab root's id); the root's refresh rebuilds them. Legacy instances (id==0) fall through.
		const uint64_t rootSourceId = GetPrefabSourceId(newSource.Entities.front());
		const uint64_t existingSourceId = GetPrefabInstanceSourceId(scene, existing);
		if (rootSourceId != 0 && existingSourceId != 0 && existingSourceId != rootSourceId) {
			return entt::null;
		}

		// Diff against OLD source to capture per-field overrides the user kept across the edit.
		PrefabDefinition oldSource;
		if (!ReadPrefabDefinitionFromRoot(previousSourceEntityValue, oldSource)) {
			oldSource.Entities.push_back(previousSourceEntityValue);
		}

		Value overrides;
		Value entityOverrides;
		BuildPrefabInstanceOverrideSet(scene, existing, oldSource, overrides, entityOverrides);

		// Wrapper shape matches ApplyOverrides â€” object with "Overrides" member keyed by dot-path.
		Value overridesWrapper = Value::MakeObject();
		overridesWrapper.AddMember("Overrides", std::move(overrides));
		if (!entityOverrides.GetObject().empty()) {
			overridesWrapper.AddMember("EntityOverrides", std::move(entityOverrides));
		}
		if (!ApplyEntityOverrides(newSource, overridesWrapper)) return existing;

		auto& registry = scene.GetRegistry();
		uint64_t preservedInstanceUuid = 0;
		if (registry.all_of<UUIDComponent>(existing)) {
			preservedInstanceUuid = static_cast<uint64_t>(registry.get<UUIDComponent>(existing).Id);
		}
		EntityHandle preservedParent = entt::null;
		if (registry.all_of<HierarchyComponent>(existing)) {
			preservedParent = registry.get<HierarchyComponent>(existing).Parent;
		}
		const bool wasDirty = scene.IsDirty();
		const bool wasUiDirty = scene.IsUIDirty();
		auto restoreDirtyState = [&]() {
			if (wasDirty) scene.MarkDirty(); else scene.ClearDirty();
			if (wasUiDirty) scene.MarkUIDirty(); else scene.ClearUIDirty();
		};

		// Build first so a malformed replacement never destroys the live instance.
		const std::size_t fixupStart = scene.GetPendingEntityRefFixupCount();
		EntityHandle freshRoot = entt::null;
		bool treeComplete = false;
		try {
			// Retarget the refreshed template's internal entity refs onto the rebuilt subtree; drain only
			// this rebuild's fixups while the remap is active.
			Scene::EntityRefRemapScope remapScope(scene);
			freshRoot = DeserializeEntityTree(scene, newSource.Entities, EntityOrigin::Prefab,
				prefabGuid, true, &treeComplete);
			if (freshRoot == entt::null || !treeComplete) {
				restoreDirtyState();
				return entt::null;
			}

			PrefabRefreshRollbackGuard rollback(scene, existing, freshRoot, preservedInstanceUuid,
				fixupStart, wasDirty, wasUiDirty);
			if (preservedParent != entt::null && registry.valid(preservedParent)) {
				scene.GetEntity(freshRoot).SetParent(scene.GetEntity(preservedParent));
			}
			if (preservedInstanceUuid != 0) {
				if (!scene.SetEntityPersistentID(freshRoot, preservedInstanceUuid, existing)) {
					return entt::null;
				}
				rollback.MarkIdentityTransferred();
				const uint64_t templateRootId = GetUInt64Member(newSource.Entities.front(), "uuid", 0);
				scene.RegisterEntityRefRemap(templateRootId, preservedInstanceUuid);
			}
			scene.RunPendingEntityRefFixupsFrom(fixupStart);
			if (registry.all_of<UUIDComponent>(existing)) {
				registry.get<UUIDComponent>(existing).Id = UUID(0);
			}
			scene.DestroyEntity(existing);
			rollback.Commit();
		}
		catch (const std::exception& exception) {
			restoreDirtyState();
			FNK_CORE_ERROR_TAG("SceneSerializer", "Prefab refresh build failed: {}", exception.what());
			return entt::null;
		}
		catch (...) {
			restoreDirtyState();
			FNK_CORE_ERROR_TAG("SceneSerializer", "Prefab refresh build failed with an unknown exception");
			return entt::null;
		}

		// The destroy+create above swapped the instance onto a NEW entity handle. For a
		// root-level, childless instance, preservedParent is null (so SetParent never runs)
		// and the registry entity count is unchanged (one destroyed, one created) — so
		// NOTHING bumps the hierarchy version. The editor's hierarchy-order cache rebuilds
		// only on a version or entity-count change (ImGuiEditorLayer hierarchy reconcile),
		// so it would keep the stale destroyed handle and never surface the rebuilt root
		// until the scene is reloaded — the instance appears to vanish from the panel.
		// Bump the structure version so the editor reconciles the swap this frame.
		// (No-op cost at runtime: m_HierarchyVersion is read only by the editor.)
		scene.MarkHierarchyStructureDirty();
		return freshRoot;
	}

	std::vector<SceneSerializer::ExternalPrefabChange> SceneSerializer::TakeExternallyChangedPrefabs() {
		std::vector<ExternalPrefabChange> changes;
		std::lock_guard<std::mutex> lock(g_PrefabRootCacheMutex);
		for (auto it = g_PrefabRootCache.begin(); it != g_PrefabRootCache.end();) {
			const FileStamp stamp = FileStamp::Read(it->second.Path);
			if (stamp == it->second.Stamp) {
				++it;
				continue;
			}
			// A deleted prefab leaves its instances as they are; there is nothing to refresh them from.
			if (stamp.Exists) {
				SupersedePrefabRootLocked(it->first, it->second);
			}
			it = g_PrefabRootCache.erase(it);
		}
		changes.reserve(g_SupersededPrefabRoots.size());
		for (auto& [key, change] : g_SupersededPrefabRoots) {
			changes.push_back(std::move(change));
		}
		g_SupersededPrefabRoots.clear();
		return changes;
	}

	void SceneSerializer::ForgetCachedPrefabRoot(const std::string& path) {
		const std::string key = PrefabRootCacheKey(path);
		std::lock_guard<std::mutex> lock(g_PrefabRootCacheMutex);
		g_PrefabRootCache.erase(key);
		g_SupersededPrefabRoots.erase(key);
	}

	void Detail::RestampCachedPrefabRoot(const std::string& path, const FileStamp& before) {
		const std::string key = PrefabRootCacheKey(path);
		std::lock_guard<std::mutex> lock(g_PrefabRootCacheMutex);
		const auto it = g_PrefabRootCache.find(key);
		if (it != g_PrefabRootCache.end() && it->second.Stamp == before) {
			it->second.Stamp = FileStamp::Read(path);
		}
	}

	bool SceneSerializer::ComputeInstanceOverrides(Scene& scene, EntityHandle entity, Json::Value& outOverrides) {
		outOverrides = Json::Value::MakeObject();
		if (entity == entt::null || !scene.IsValid(entity)) return false;
		if (scene.GetEntityOrigin(entity) != EntityOrigin::Prefab) return false;

		const uint64_t prefabGuid = static_cast<uint64_t>(scene.GetPrefabGUID(entity));
		if (prefabGuid == 0) return false;

		PrefabDefinition sourceDefinition;
		if (!LoadPrefabDefinition(prefabGuid, sourceDefinition) || sourceDefinition.Entities.empty()) return false;

		const uint64_t sourceId = GetPrefabInstanceSourceId(scene, entity);
		const Value* sourceValue = nullptr;
		if (sourceId != 0) {
			const auto sourceById = BuildSourceEntityMap(sourceDefinition);
			const auto sourceIt = sourceById.find(sourceId);
			if (sourceIt != sourceById.end()) {
				sourceValue = sourceIt->second;
			}
		}
		if (!sourceValue) {
			sourceValue = &sourceDefinition.Entities.front();
		}

		std::unordered_map<uint32_t, uint64_t> sourceIds;
		sourceIds[static_cast<uint32_t>(entity)] = sourceId;
		Value currentInstance = SerializePrefabComparableEntity(scene, entity, sourceIds);
		Value sourceComparable = *sourceValue;
		RemoveEntityComparisonIdentityMembers(sourceComparable);
		RemoveEntityComparisonIdentityMembers(currentInstance);

		NormalizeLegacyColliderKeysForDiff(sourceComparable, currentInstance);
		BuildOverridePatch(sourceComparable, currentInstance, {}, outOverrides);
		return true;
	}

	EntityHandle SceneSerializer::LoadEntityFromFile(Scene& scene, const std::string& path) {
		try {
			if (!File::Exists(path)) {
				FNK_CORE_WARN_TAG("SceneSerializer", "Prefab file not found: {}", path);
				return entt::null;
			}

			const uint64_t prefabGuid = AssetRegistry::GetOrCreateAssetUUID(path);
			if (prefabGuid != 0 && AssetRegistry::GetKind(prefabGuid) == AssetKind::Prefab) {
				EntityHandle instance = InstantiatePrefab(scene, prefabGuid);
				if (instance != entt::null) {
					return instance;
				}
			}

			Value root;
			std::string readError;
			if (!SceneSerializerStorage::ReadRootFromFile(path, root, &readError) || !root.IsObject()) {
				FNK_CORE_ERROR_TAG("SceneSerializer", "Failed to parse prefab {}: {}", path, readError);
				return entt::null;
			}

			PrefabDefinition definition;
			if (!ReadPrefabDefinitionFromRoot(root, definition)) {
				FNK_CORE_WARN_TAG("SceneSerializer", "No prefab block in file: {}", path);
				return entt::null;
			}

			// Same self-ref retargeting as InstantiatePrefab, for the fallback path taken when the file
			// isn't a tracked Prefab asset (e.g. a loose .prefab outside the project / headless load).
			const std::size_t fixupStart = scene.GetPendingEntityRefFixupCount();
			EntityHandle root2 = entt::null;
			{
				Scene::EntityRefRemapScope remapScope(scene);
				root2 = DeserializeEntityTree(scene, definition.Entities, EntityOrigin::Prefab, prefabGuid);
				scene.RunPendingEntityRefFixupsFrom(fixupStart);
			}
			return root2;
		}
		catch (const std::exception& exception) {
			FNK_CORE_ERROR_TAG("SceneSerializer", "LoadEntityFromFile failed: {}", exception.what());
			return entt::null;
		}
	}

} // namespace Funke
