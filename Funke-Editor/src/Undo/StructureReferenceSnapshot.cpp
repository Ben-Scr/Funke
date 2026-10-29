#include <pch.hpp>
#include "Undo/StructureReferenceSnapshot.hpp"

#include "Inspector/PropertyDescriptor.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <unordered_set>

namespace Funke {
	namespace {
		bool IsEntityReference(const PropertyValue& value) {
			return value.Type == PropertyType::ComponentRef
				|| (value.Type == PropertyType::EntityRef && value.StringValue != "prefab");
		}

		bool ReferencesAffected(const PropertyValue& value, const std::unordered_set<uint64_t>& affected) {
			if (IsEntityReference(value)) return value.UIntValue != 0 && affected.contains(value.UIntValue);
			return value.Type == PropertyType::List && std::any_of(value.ListValue.begin(), value.ListValue.end(),
				[&](const PropertyValue& item) { return ReferencesAffected(item, affected); });
		}

		bool CanContainReferences(const PropertyDescriptor& property) {
			return property.Type == PropertyType::EntityRef || property.Type == PropertyType::ComponentRef
				|| property.Type == PropertyType::List;
		}

		void CaptureProperties(const Entity& entity, uint64_t ownerId, const std::string& componentName,
			const std::vector<PropertyDescriptor>& properties, std::vector<std::string>& path,
			StructureReferenceState& result) {
			for (const PropertyDescriptor& property : properties) {
				path.push_back(property.Name);
				if (property.Get && property.Set && CanContainReferences(property)) {
					result.push_back({ ownerId, componentName, path, property.Get(entity) });
				}
				if (property.Get && !property.VariantBranches.empty()) {
					const PropertyValue tag = property.Get(entity);
					for (const auto& branch : property.VariantBranches) {
						if (branch.TagValue == tag.IntValue) {
							CaptureProperties(entity, ownerId, componentName, branch.Properties, path, result);
							break;
						}
					}
				}
				path.pop_back();
			}
		}

		const PropertyDescriptor* FindProperty(const Entity& entity, const std::vector<PropertyDescriptor>& properties,
			std::span<const std::string> path) {
			if (path.empty()) return nullptr;
			const auto it = std::find_if(properties.begin(), properties.end(),
				[&](const PropertyDescriptor& property) { return property.Name == path.front(); });
			if (it == properties.end()) return nullptr;
			if (path.size() == 1) return &*it;
			if (!it->Get) return nullptr;
			const PropertyValue tag = it->Get(entity);
			for (const auto& branch : it->VariantBranches) {
				if (branch.TagValue == tag.IntValue) return FindProperty(entity, branch.Properties, path.subspan(1));
			}
			return nullptr;
		}

		void ClearMissingReferences(const Scene& scene, PropertyValue& value) {
			if (IsEntityReference(value) && value.UIntValue != 0) {
				EntityHandle handle = entt::null;
				if (!scene.TryResolveEntityRef(value.UIntValue, handle)) value.UIntValue = 0;
			}
			else if (value.Type == PropertyType::List) {
				for (PropertyValue& item : value.ListValue) ClearMissingReferences(scene, item);
			}
		}
	}

	StructureReferenceState CaptureStructureReferences(Scene& scene) {
		StructureReferenceState result;
		const auto& components = SceneManager::GetComponentRegistryOrBuiltIn().All();
		std::vector<std::string> path;
		for (EntityHandle handle : scene.GetRegistry().view<entt::entity>()) {
			const Entity entity = scene.GetEntity(handle);
			const uint64_t ownerId = scene.GetEntityPersistentID(handle);
			for (const auto& [type, info] : components) {
				if (info.properties.empty() || !info.has || !info.has(entity)) continue;
				CaptureProperties(entity, ownerId, info.serializedName, info.properties, path, result);
			}
		}
		return result;
	}

	void RetainAffectedStructureReferences(StructureReferenceState& before, StructureReferenceState& after,
		std::span<const uint64_t> affectedIds) {
		const std::unordered_set<uint64_t> affected(affectedIds.begin(), affectedIds.end());
		using Key = std::tuple<uint64_t, std::string, std::vector<std::string>>;
		std::set<Key> keys;
		for (const auto* state : { &before, &after }) {
			for (const auto& entry : *state) {
				if (ReferencesAffected(entry.Value, affected)) {
					keys.emplace(entry.OwnerId, entry.ComponentName, entry.PropertyPath);
				}
			}
		}
		for (auto* state : { &before, &after }) {
			std::erase_if(*state, [&](const auto& entry) {
				return !keys.contains(Key{ entry.OwnerId, entry.ComponentName, entry.PropertyPath });
			});
		}
	}

	void RestoreStructureReferences(Scene& scene, const StructureReferenceState& state) {
		const auto& components = SceneManager::GetComponentRegistryOrBuiltIn();
		for (const auto& entry : state) {
			EntityHandle owner = entt::null;
			if (!scene.TryResolveEntityRef(entry.OwnerId, owner)) continue;
			Entity entity = scene.GetEntity(owner);
			const ComponentInfo* info = components.FindBySerializedName(entry.ComponentName);
			if (!info || !info->has || !info->has(entity)) continue;
			const PropertyDescriptor* property = FindProperty(entity, info->properties, entry.PropertyPath);
			if (!property || !property->Set || property->Type != entry.Value.Type) continue;
			PropertyValue value = entry.Value;
			// Some setters retain the old handle for an unresolved nonzero id; deleted targets must clear it.
			ClearMissingReferences(scene, value);
			property->Set(entity, value);
		}
	}
}
