#include <pch.hpp>
#include "Scripting/ScriptAttachment.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"

namespace Funke::EditorScriptAttachment {
	bool AttachScriptToEntity(Entity entity, Scene& scene, const EditorScriptDiscovery::ScriptEntry& scriptEntry)
	{
		if (scriptEntry.ClassName.empty() || scriptEntry.Type == ScriptType::Unknown) {
			return false;
		}

		if (scriptEntry.Type == ScriptType::Bolt && scriptEntry.IsNativeComponent) {
			auto& registry = SceneManager::GetComponentRegistry();
			const auto* info = registry.FindBySerializedName(scriptEntry.ClassName);
			if (!entity.IsValid() || !info || !info->IsBoltStruct() || info->has(entity)) return false;
			if (!registry.AddWithDependencies(entity, *info)) return false;
			scene.MarkDirty();
			return true;
		}
		if (!entity.HasComponent<ScriptComponent>()) {
			entity.AddComponent<ScriptComponent>();
		}

		auto& scriptComponent = entity.GetComponent<ScriptComponent>();
		const bool alreadyHadScript = scriptComponent.HasScript(scriptEntry.ClassName, scriptEntry.Type);

		if (!alreadyHadScript) {
			scriptComponent.AddScript(scriptEntry.ClassName, scriptEntry.Type);
		}

		// Also populate DynamicComponentStorage so HasNativeComponent<T>()/GetRef<T>() in scripts can find it; done unconditionally so re-attach after failed registration also seeds the storage.
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

}
