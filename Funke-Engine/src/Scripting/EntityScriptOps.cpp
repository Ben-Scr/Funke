#include "pch.hpp"
#include "Scripting/EntityScriptOps.hpp"

#include "Scene/Scene.hpp"
#include "Scripting/NativeScript.hpp"
#include "Scripting/NativeScriptHost.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptInstance.hpp"
#include "Scripting/ScriptSystem.hpp"

namespace Funke::EntityScriptOps::detail {
	namespace {
		ScriptInstance* FindInstance(Scene& scene, EntityHandle entity, const std::string& name) {
			if (!scene.IsValid(entity)) return nullptr;
			auto* component = scene.GetRegistry().try_get<ScriptComponent>(entity);
			if (!component) return nullptr;
			for (auto& instance : component->Scripts) {
				if (instance.GetType() == ScriptType::Native && instance.GetClassName() == name) return &instance;
			}
			return nullptr;
		}

		struct NativeSceneScope {
			explicit NativeSceneScope(Scene& scene) : Execution(scene), Previous(ScriptEngine::GetScene()) {
				ScriptEngine::SetScene(&scene);
			}
			~NativeSceneScope() { ScriptEngine::SetScene(Previous); }
			SceneExecutionScope Execution;
			Scene* Previous;
		};
	}

	NativeScript* AddScriptByName(Scene* scene, EntityHandle entity, const char* name) {
		if (!scene || entity == entt::null || !name || !*name) return nullptr;
		entt::registry& registry = scene->GetRegistry();
		if (!registry.valid(entity)) return nullptr;
		const std::string className = name;
		NativeSceneScope sceneScope(*scene);

		ScriptComponent& sc = registry.all_of<ScriptComponent>(entity)
			? registry.get<ScriptComponent>(entity)
			: registry.emplace<ScriptComponent>(entity);

		sc.AddScript(className, ScriptType::Native);
		ScriptInstance* added = FindInstance(*scene, entity, className);
		if (!added) return nullptr;
		if (added->HasNativeInstance()) return added->GetNativePtr();

		added->Bind(entity);
		auto& host = ScriptSystem::GetNativeHost();
		NativeScript* native = host.CreateInstance(className, entity, scene);
		if (!native) return nullptr;
		added = FindInstance(*scene, entity, className);
		if (!added) {
			host.DestroyInstance(native);
			return nullptr;
		}
		added->SetNativePtr(native);
		const std::string prefix = className + ".";
		for (const auto& [key, value] : scene->GetComponent<ScriptComponent>(entity).PendingFieldValues) {
			if (key.size() > prefix.size() && key.rfind(prefix, 0) == 0) host.SetFieldValue(native, key.substr(prefix.size()), value);
		}
		added->MarkStarted();

		// Start can add scripts or remove this entity; never retain a vector element across user code.
		bool started = false;
		try {
			native->Start();
			started = true;
		}
		catch (const std::exception& error) {
			FNK_CORE_ERROR_TAG("NativeScriptHost", "Native script '{}' failed during Start: {}", className, error.what());
		}
		catch (...) {
			FNK_CORE_ERROR_TAG("NativeScriptHost", "Native script '{}' failed during Start", className);
		}
		if (!started) host.DestroyInstance(native);
		added = FindInstance(*scene, entity, className);
		if (!added || added->GetNativePtr() != native) return nullptr;
		if (!started) {
			added->Unbind();
			return nullptr;
		}
		return added->GetNativePtr();
	}

	NativeScript* GetNativeScriptOnEntity(Scene* scene, EntityHandle entity, const char* name) {
		if (!scene || entity == entt::null || !name) return nullptr;
		entt::registry& registry = scene->GetRegistry();
		if (!registry.valid(entity)) return nullptr;
		auto* sc = registry.try_get<ScriptComponent>(entity);
		if (!sc) return nullptr;
		for (auto& inst : sc->Scripts) {
			if (inst.GetType() != ScriptType::Native) continue;
			if (inst.GetClassName() != name) continue;
			return inst.GetNativePtr();
		}
		return nullptr;
	}

	bool HasNativeScriptOnEntity(Scene* scene, EntityHandle entity, const char* name) {
		return GetNativeScriptOnEntity(scene, entity, name) != nullptr;
	}

	bool RemoveNativeScriptOnEntity(Scene* scene, EntityHandle entity, const char* name) {
		if (!scene || entity == entt::null || !name) return false;
		entt::registry& registry = scene->GetRegistry();
		if (!registry.valid(entity)) return false;
		auto* sc = registry.try_get<ScriptComponent>(entity);
		if (!sc) return false;
		for (size_t index = 0; index < sc->Scripts.size(); ++index) {
			if (sc->Scripts[index].GetType() != ScriptType::Native) continue;
			if (sc->Scripts[index].GetClassName() != name) continue;
			return ScriptSystem::RemoveScript(scene->GetEntity(entity), index);
		}
		return false;
	}

} // namespace Funke::EntityScriptOps::detail
