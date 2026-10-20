#include "pch.hpp"
#include "Scripting/ScriptBindingGuard.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptEntityReference.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptEditorGui.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Scripting/NativeScriptHost.hpp"
#include "Scripting/DataAssetManager.hpp"
#include "Collections/Viewport.hpp"
#include "Core/Application.hpp"
#include "Core/Input.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#include "Core/Log.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneDefinition.hpp"
#include "Scene/ProjectSceneLoader.hpp"
#include "DevTools/DevConsole.hpp"
#include "Systems/AudioUpdateSystem.hpp"
#include "Systems/UIEventSystem.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Project/ProjectManager.hpp"
#include "Project/FunkeProject.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#endif
#include "Components/General/NameComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/Graphics/ImageComponent.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/UI/ButtonComponent.hpp"
#include "Components/UI/CircularSliderComponent.hpp"
#include "Components/UI/ContentSizeFitterComponent.hpp"
#include "Components/UI/DropdownComponent.hpp"
#include "Components/UI/GridLayoutGroupComponent.hpp"
#include "Components/UI/HorizontalLayoutGroupComponent.hpp"
#include "Components/UI/InputFieldComponent.hpp"
#include "Components/UI/InteractableComponent.hpp"
#include "Components/UI/MaskComponent.hpp"
#include "Components/UI/ScrollRectComponent.hpp"
#include "Components/UI/ScrollbarComponent.hpp"
#include "Components/UI/SliderComponent.hpp"
#include "Components/UI/ToggleComponent.hpp"
#include "Components/UI/VerticalLayoutGroupComponent.hpp"
#include "Components/UI/WidthConstraintComponent.hpp"
#include "Components/Tags.hpp"
#include "Audio/AudioManager.hpp"
#include "Graphics/TextureManager.hpp"
#include "Graphics/Text/FontManager.hpp"
#include "Graphics/Gizmo.hpp"
#include "Jobs/ParallelFor.hpp"
#include "Profiling/Profiler.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>

namespace Funke {
	bool ScriptBindings::IsScriptInputEnabled()
	{
		return Application::s_Instance && Application::s_Instance->m_IsScriptInputEnabled && !DevConsole::IsOpen();
	}

	EntityHandle ToEntityHandle(uint64_t id)
	{
		return static_cast<EntityHandle>(static_cast<uint32_t>(id));
	}

	uint64_t FromEntityHandle(EntityHandle handle)
	{
		return static_cast<uint64_t>(static_cast<uint32_t>(handle));
	}

	uint64_t GetEntityScriptId(const Scene& scene, EntityHandle handle)
	{
		// Returns the persistent UUID so all script-side entity IDs match Entity.ID and serialized refs; falls back to entt handle during mid-construction before UUIDComponent is added.
		if (scene.IsValid(handle)) {
			const uint64_t persistentId = scene.GetEntityPersistentID(handle);
			if (persistentId != 0) {
				return persistentId;
			}
		}

		return FromEntityHandle(handle);
	}

	bool TryResolveEntityByUUID(const Scene& scene, uint64_t entityID, EntityHandle& outHandle)
	{
		return scene.TryResolveEntityRef(entityID, outHandle);
	}

	bool ResolveEntityReference(uint64_t entityID, Scene*& outScene, EntityHandle& outHandle)
	{
		outScene = nullptr;
		outHandle = entt::null;

		if (entityID == 0) {
			return false;
		}

		// Never fall back to ToEntityHandle(entityID) — UUIDs would truncate and silently alias entt handles.
		Scene* currentScene = GetCurrentExecutionScene();
		if (!currentScene) {
			currentScene = ScriptEngine::GetScene();
		}
		if (currentScene) {
			if (TryResolveEntityByUUID(*currentScene, entityID, outHandle)) {
				outScene = currentScene;
				return true;
			}
		}

		SceneManager::ForeachLoadedScene([&](const Scene& scene) {
			if (outScene || &scene == currentScene) {
				return;
			}

			EntityHandle resolvedHandle = entt::null;
			if (TryResolveEntityByUUID(scene, entityID, resolvedHandle)) {
				outScene = const_cast<Scene*>(&scene);
				outHandle = resolvedHandle;
			}
		});

		return outScene != nullptr;
	}

	Scene* GetScene()
	{
		if (Scene* scene = GetCurrentExecutionScene()) {
			return scene;
		}

		Scene* scene = ScriptEngine::GetScene();
		if (scene) {
			return scene;
		}

		return ResolveImplicitScene();
	}

	// HAZARD: never return this pointer to managed code — it is per-thread scratch for the two-call size/copy pattern only; thread-local re-entrant races caused all unbuffered thunks to be removed.
	thread_local std::string s_StringReturnBuffer;

	// MUST swallow exceptions here: bindings are called via [UnmanagedCallersOnly] and unwinding
	// through CoreCLR frames is UB. Allocations (std::string, JSON parse, etc.) can throw std::bad_alloc.
	#define FNK_BINDING_TRY try
	#define FNK_BINDING_CATCH(default_return)                                                  \
		catch (const std::exception& _fnk_ex) {                                                \
			FNK_CORE_ERROR_TAG("ScriptBinding", "exception in {}: {}", __func__, _fnk_ex.what()); \
			return default_return;                                                             \
		}                                                                                      \
		catch (...) {                                                                          \
			FNK_CORE_ERROR_TAG("ScriptBinding", "unknown exception in {}", __func__);          \
			return default_return;                                                             \
		}
	// Void-return variant (no return statement in catch).
	#define FNK_BINDING_CATCH_VOID                                                             \
		catch (const std::exception& _fnk_ex) {                                                \
			FNK_CORE_ERROR_TAG("ScriptBinding", "exception in {}: {}", __func__, _fnk_ex.what()); \
		}                                                                                      \
		catch (...) {                                                                          \
			FNK_CORE_ERROR_TAG("ScriptBinding", "unknown exception in {}", __func__);          \
		}

	static int CopyStringToBuffer(std::string_view value, char* outBuffer, int capacity)
	{
		const int requiredBytes = static_cast<int>(std::min(
			value.size(),
			static_cast<size_t>(std::numeric_limits<int>::max())));
		if (outBuffer && capacity > 0) {
			const int bytesToCopy = std::min(requiredBytes, capacity - 1);
			if (bytesToCopy > 0) {
				std::memcpy(outBuffer, value.data(), static_cast<size_t>(bytesToCopy));
			}
			outBuffer[bytesToCopy] = '\0';
		}
		return requiredBytes;
	}

	static int CopyCStringToBuffer(const char* value, char* outBuffer, int capacity)
	{
		return CopyStringToBuffer(value ? std::string_view(value) : std::string_view(), outBuffer, capacity);
	}

#if FUNKE_WITH_2D
	bool SetParentKeepingWorld2D(Scene& scene, EntityHandle handle, Entity newParent);
#endif
#if FUNKE_WITH_3D
	bool SetParentKeepingWorld3D(Scene& scene, EntityHandle handle, Entity newParent);
#endif
	void PopulateNonComponentBindings(NativeBindings& b);
	void PopulatePostProcessingBindings(NativeBindings& b);
	void PopulateEcbBindings(NativeBindings& b);
	void PopulateJobsBindings(NativeBindings& b);
	void PopulateMaterialBindings(NativeBindings& b);
	void PopulateAnimationBindings(NativeBindings& b);
	void PopulateLocalizationBindings(NativeBindings& b);

	// IsValid re-check required: entity may be destroyed between ResolveEntityReference and the registry access.
	#define GET_COMPONENT(Type, entityID, failReturn) \
		Scene* scene = nullptr; \
		EntityHandle handle = entt::null; \
		if (!ResolveEntityReference(entityID, scene, handle)) return failReturn; \
		if (!scene->IsValid(handle)) return failReturn; \
		if (!scene->HasComponent<Type>(handle)) return failReturn; \
		auto& comp = scene->GetComponent<Type>(handle)


	static const ComponentInfo* FindComponentByNameUncached(const ComponentRegistry& registry, std::string_view name) {
		// Hash fast path 1: name matches a registered serializedName directly.
		if (const ComponentInfo* hit = registry.FindBySerializedName(name)) {
			return hit;
		}
		// Hash fast path 2: name is "<SerializedName>Component" (C# convention).
		// Trim the suffix without an allocation.
		constexpr std::string_view kComponentSuffix = "Component";
		if (name.size() > kComponentSuffix.size()) {
			const std::string_view tail = name.substr(name.size() - kComponentSuffix.size());
			if (tail == kComponentSuffix) {
				const std::string_view stem = name.substr(0, name.size() - kComponentSuffix.size());
				if (const ComponentInfo* hit = registry.FindBySerializedName(stem)) {
					return hit;
				}
			}
		}

		// Slow path: displayName / normalized-displayName variants. Kept for
		// the rare component whose C# wrapper uses a hand-written name that
		// doesn't match serializedName (legacy / package types).
		const ComponentInfo* found = nullptr;
		registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
			if (found) return;
			if (info.displayName == name) { found = &info; return; }
			if (!info.displayName.empty()) {
				// Compare against the de-spaced displayName + "Component" without
				// materialising a temporary string: walk both sides character by
				// character, skipping spaces on the displayName side.
				std::size_t i = 0;
				bool match = true;
				for (char c : info.displayName) {
					if (c == ' ') continue;
					if (i >= name.size() || name[i] != c) { match = false; break; }
					++i;
				}
				if (match && (name.size() - i) == kComponentSuffix.size()
					&& name.substr(i) == kComponentSuffix)
				{
					found = &info;
				}
			}
		});
		return found;
	}

	struct ComponentNameHash {
		using is_transparent = void;
		size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
	};

	// C# sends display names ("Transform 2D") that miss the serializedName hash index, so without
	// this memo every component access from a script would fall into the linear slow path.
	static const ComponentInfo* FindComponentByName(std::string_view name) {
		if (name.empty()) return nullptr;
		const auto& registry = SceneManager::GetComponentRegistry();

		// Thread-local: bindings also run on job-system workers.
		thread_local uint64_t t_Generation = ~0ull;
		thread_local std::unordered_map<std::string, const ComponentInfo*, ComponentNameHash, std::equal_to<>> t_Cache;
		if (t_Generation != registry.GetGeneration()) {
			t_Cache.clear();
			t_Generation = registry.GetGeneration();
		}

		if (const auto it = t_Cache.find(name); it != t_Cache.end()) {
			return it->second;
		}
		const ComponentInfo* info = FindComponentByNameUncached(registry, name);
		if (t_Cache.size() >= 1024) t_Cache.clear(); // bound misses from arbitrary script strings
		t_Cache.emplace(std::string(name), info);
		return info;
	}

	static const ComponentInfo* FindComponentByName(const char* name) {
		return FindComponentByName(std::string_view(name ? name : ""));
	}

	static bool HasManagedComponent(Scene& scene, EntityHandle handle, const std::string& className) {
		if (!scene.IsValid(handle) || !scene.HasComponent<ScriptComponent>(handle)) return false;
		return scene.GetComponent<ScriptComponent>(handle).HasManagedComponent(className);
	}

	static bool AddManagedComponent(Scene& scene, EntityHandle handle, const std::string& className) {
		if (!scene.IsValid(handle) || className.empty()) return false;
		if (!scene.HasComponent<ScriptComponent>(handle)) {
			scene.AddComponent<ScriptComponent>(handle);
		}
		auto& scriptComponent = scene.GetComponent<ScriptComponent>(handle);
		if (scriptComponent.HasManagedComponent(className)) return true;
		scriptComponent.AddManagedComponent(className);
		scene.MarkDirty();
		return true;
	}

	static bool RemoveManagedComponent(Scene& scene, EntityHandle handle, const std::string& className) {
		if (!scene.IsValid(handle) || !scene.HasComponent<ScriptComponent>(handle)) return false;
		const bool removed = scene.GetComponent<ScriptComponent>(handle).RemoveManagedComponent(className);
		if (removed) scene.MarkDirty();
		return removed;
	}

	static int Funke_Entity_GetManagedComponentFieldsBuffer(uint64_t entityID, const char* componentName, char* outBuffer, int capacity)
	{
		FNK_BINDING_TRY {
			Scene* scene = nullptr;
			EntityHandle handle = entt::null;
			const std::string className = componentName ? componentName : "";
			if (className.empty() || !ResolveEntityReference(entityID, scene, handle)
				|| !scene->HasComponent<ScriptComponent>(handle)) {
				return CopyCStringToBuffer("{}", outBuffer, capacity);
			}

			const auto& scriptComponent = scene->GetComponent<ScriptComponent>(handle);
			if (!scriptComponent.HasManagedComponent(className)) {
				return CopyCStringToBuffer("{}", outBuffer, capacity);
			}

			Json::Value root = Json::Value::MakeObject();
			const std::string prefix = className + ".";
			for (const auto& [key, value] : scriptComponent.PendingFieldValues) {
				if (key.rfind(prefix, 0) != 0) {
					continue;
				}
				root.AddMember(key.substr(prefix.size()), Json::Value(value));
			}

			const std::string serialized = Json::Stringify(root, false);
			return CopyCStringToBuffer(serialized.c_str(), outBuffer, capacity);
		} FNK_BINDING_CATCH(CopyCStringToBuffer("{}", outBuffer, capacity));
	}

	static int Funke_Entity_GetIsStatic(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return scene->HasComponent<StaticTag>(handle) ? 1 : 0;
	}

	static void Funke_Entity_SetIsStatic(uint64_t entityID, int isStatic)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return;

		const bool shouldBeStatic = isStatic != 0;
		const bool currentlyStatic = scene->HasComponent<StaticTag>(handle);
		if (shouldBeStatic == currentlyStatic) return;

		if (shouldBeStatic) {
			scene->AddComponent<StaticTag>(handle);
		}
		else {
			scene->RemoveComponent<StaticTag>(handle);
		}
		scene->MarkDirty();
	}

	// Authored state only: DisabledTag without InheritedDisabledTag means user-disabled. Ancestor disable doesn't change the authored intent.
	static int Funke_Entity_GetIsEnabled(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		// Authored-disabled = DisabledTag without InheritedDisabledTag.
		// Anything else (no tag, or inherited-only tag) is authored-enabled.
		const bool authoredDisabled = scene->HasComponent<DisabledTag>(handle)
			&& !scene->HasComponent<InheritedDisabledTag>(handle);
		return authoredDisabled ? 0 : 1;
	}

	// Runtime flag: false if disabled for ANY reason (authored or inherited). Use this — not GetIsEnabled — to check if entity is actually live this frame.
	static int Funke_Entity_GetIsEnabledInHierarchy(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return scene->HasComponent<DisabledTag>(handle) ? 0 : 1;
	}

	static void Funke_Entity_SetIsEnabled(uint64_t entityID, int isEnabled)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return;

		// Compare authored state: SetIsEnabled(true) on a child with a disabled parent flips authored intent so the next parent re-enable cascade restores it.
		const bool shouldBeEnabled = isEnabled != 0;
		const bool authoredDisabled = scene->HasComponent<DisabledTag>(handle)
			&& !scene->HasComponent<InheritedDisabledTag>(handle);
		const bool currentlyEnabled = !authoredDisabled;
		if (shouldBeEnabled == currentlyEnabled) return;

		scene->GetEntity(handle).SetEnabled(shouldBeEnabled);
		scene->MarkDirty();
	}

	struct QueryComponentRequirement {
		const ComponentInfo* Native = nullptr;
		std::string ManagedClassName;

		bool Has(Scene& scene, EntityHandle handle, Entity entity) const {
			if (Native && Native->has) {
				return Native->has(entity);
			}
			return HasManagedComponent(scene, handle, ManagedClassName);
		}
	};

	static bool BuildComponentRequirement(std::string_view name, QueryComponentRequirement& out) {
		if (name.empty()) {
			return false;
		}

		const ComponentInfo* info = FindComponentByName(name);
		if (info && info->has) {
			out.Native = info;
			return true;
		}

		// Owning string allocated here because QueryComponentRequirement outlives the caller's transient string_view.
		out.ManagedClassName.assign(name);
		return true;
	}

	int Funke_Entity_HasComponent(uint64_t entityID, const char* componentName)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info || !info->has) return HasManagedComponent(*scene, handle, componentName ? componentName : "") ? 1 : 0;
		return info->has(scene->GetEntity(handle)) ? 1 : 0;
	}

	int Funke_Entity_AddComponent(uint64_t entityID, const char* componentName)
	{
		FNK_BINDING_TRY {
			Scene* scene = nullptr;
			EntityHandle handle = entt::null;
			if (!ResolveEntityReference(entityID, scene, handle)) return 0;

			const ComponentInfo* info = FindComponentByName(componentName);
			if (!info || !info->add) return AddManagedComponent(*scene, handle, componentName ? componentName : "") ? 1 : 0;

			Entity entity = scene->GetEntity(handle);
			if (info->has && info->has(entity)) return 1;

			// Dynamic components bypass AddWithDependencies (keyed on type_index in m_map, not m_dynamicMap);
			// also must seed a ScriptComponent.Scripts Managed entry so the inspector field renderer finds it.
			if (info->isDynamic) {
				info->add(entity);
				if (!entity.HasComponent<ScriptComponent>()) {
					entity.AddComponent<ScriptComponent>();
				}
				auto& sc = entity.GetComponent<ScriptComponent>();
				if (!sc.HasScript(componentName, ScriptType::Managed)) {
					sc.AddScript(componentName, ScriptType::Managed);
				}
				return 1;
			}

			// 0 when refused because the entity has a conflicting component (the registry logs which one).
			return SceneManager::GetComponentRegistry().AddWithDependencies(entity, info->typeId) ? 1 : 0;
		} FNK_BINDING_CATCH(0)
	}

	int Funke_Entity_RemoveComponent(uint64_t entityID, const char* componentName)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info || !info->remove) return RemoveManagedComponent(*scene, handle, componentName ? componentName : "") ? 1 : 0;

		Entity entity = scene->GetEntity(handle);
		if (info->has && !info->has(entity)) return 0;

		// Dynamic components are dual-attached; tear down via ScriptSystem::RemoveScript so both the storage row and Scripts entry drop together.
		if (info->isDynamic) {
			if (entity.HasComponent<ScriptComponent>()) {
				auto& sc = entity.GetComponent<ScriptComponent>();
				for (size_t i = 0; i < sc.Scripts.size(); ++i) {
					if (sc.Scripts[i].GetClassName() == componentName &&
						sc.Scripts[i].GetType() == ScriptType::Managed) {
						ScriptSystem::RemoveScript(entity, i);
						return 1;
					}
				}
			}
			// No paired Scripts entry (pre-Fix1 runtime state). Drop the
			// storage row directly so the C# call still has the documented
			// effect.
			info->remove(entity);
			return 1;
		}

		info->remove(entity);
		return 1;
	}

	// ── Entity_AddScript / HasScript / RemoveScript ─────────────────

	int Funke_Entity_AddScript(uint64_t entityID, const char* className)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		if (!className || !*className) return 0;

		if (!scene->HasComponent<ScriptComponent>(handle)) {
			scene->AddComponent<ScriptComponent>(handle);
		}
		auto& scriptComponent = scene->GetComponent<ScriptComponent>(handle);
		if (scriptComponent.HasScript(className)) {
			return 1;
		}
		scriptComponent.AddScript(className, ScriptType::Managed);
		scene->MarkDirty();

		// Synchronous bind + Awake so the returned C# wrapper is already live; without this, AddComponent<MyScript>() would return null until the next ScriptSystem tick.
		ScriptInstance* added = nullptr;
		for (auto& inst : scriptComponent.Scripts) {
			if (inst.GetClassName() == className && inst.GetType() == ScriptType::Managed) {
				added = &inst;
			}
		}
		if (added && !added->HasAnyInstance() && ScriptEngine::ClassExists(className)) {
			added->Bind(handle);
			// CreateScriptInstance derives the entity id from ScriptEngine's current scene. Outside a ScriptSystem
			// pass (GlobalSystem, UI callback) that is null or another scene, which bound the script to a bogus id.
			SceneExecutionScope executionScope(*scene);
			Scene* previousScriptScene = ScriptEngine::GetScene();
			ScriptEngine::SetScene(scene);
			uint32_t gcHandle = 0;
			try {
				gcHandle = ScriptEngine::CreateScriptInstance(className, handle);
				if (gcHandle != 0) {
					added->SetGCHandle(gcHandle);
					ScriptEngine::InvokeAwake(gcHandle);
				}
			}
			catch (...) {
				ScriptEngine::SetScene(previousScriptScene);
				throw;
			}
			ScriptEngine::SetScene(previousScriptScene);
		}
		return 1;
	}

	int Funke_Entity_HasScript(uint64_t entityID, const char* className)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		if (!className || !*className) return 0;
		if (!scene->HasComponent<ScriptComponent>(handle)) return 0;
		return scene->GetComponent<ScriptComponent>(handle).HasScript(className) ? 1 : 0;
	}

	int Funke_Entity_RemoveScript(uint64_t entityID, const char* className)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		if (!className || !*className) return 0;
		if (!scene->HasComponent<ScriptComponent>(handle)) return 0;

		// Detach the matching slots first, then run teardown. Teardown invokes user code
		// (OnDisable/OnDestroy) that may add or remove scripts on this entity, which would
		// invalidate an iterator into Scripts if it ran mid-loop.
		struct PendingTeardown {
			uint32_t ManagedHandle = 0;
			bool WasEnabled = false;
			NativeScript* NativeInstance = nullptr;
		};
		std::vector<PendingTeardown> teardowns;

		auto& scriptComponent = scene->GetComponent<ScriptComponent>(handle);
		bool removedAny = false;
		for (auto it = scriptComponent.Scripts.begin(); it != scriptComponent.Scripts.end(); ) {
			if (it->GetClassName() == className) {
				PendingTeardown teardown;
				if (it->GetType() == ScriptType::Native && it->HasNativeInstance()) {
					teardown.NativeInstance = it->GetNativePtr();
				}
				if (it->GetType() == ScriptType::Managed && it->HasManagedInstance()) {
					teardown.ManagedHandle = it->GetGCHandle();
					teardown.WasEnabled = it->HasEnabled();
				}
				teardowns.push_back(teardown);
				it = scriptComponent.Scripts.erase(it);
				removedAny = true;
			}
			else {
				++it;
			}
		}

		// Full teardown, matching ScriptSystem's: OnDisable -> OnDestroy -> DestroyScriptInstance.
		// Without DestroyScriptInstance the managed object stayed registered — it kept receiving
		// lifecycle fan-out after being destroyed and was still returned by GetComponent<T>().
		for (const PendingTeardown& teardown : teardowns) {
			if (teardown.NativeInstance) {
				ScriptSystem::GetNativeHost().DestroyInstance(teardown.NativeInstance);
			}
			if (teardown.ManagedHandle != 0) {
				if (teardown.WasEnabled) {
					ScriptEngine::InvokeOnDisable(teardown.ManagedHandle);
				}
				ScriptEngine::InvokeOnDestroy(teardown.ManagedHandle);
				ScriptEngine::DestroyScriptInstance(teardown.ManagedHandle);
			}
		}

		if (removedAny) scene->MarkDirty();
		return removedAny ? 1 : 0;
	}

	// True for the world/local Transform2DComponent. Handing a *writable* raw transform pointer
	// to managed code (a QueryRef row or a single component ref) lets a script move an entity by
	// writing pool memory directly — but a ref-write only flips the component's own m_Dirty bit;
	// unlike the by-handle Transform2D_Set* path it can't reach back and call
	// Scene::MarkTransformDirty. So we trip the hierarchy-dirty gate here, where the pointer is
	// handed out, so TransformHierarchySystem re-derives child world transforms from any moved
	// parent this frame. Without it, moving a parent from C# leaves its children behind.
	static bool IsTransform2DComponentInfo(const ComponentInfo* info)
	{
#if FUNKE_WITH_2D
		static const std::type_index kTransform2DType(typeid(Transform2DComponent));
		return info && info->typeId == kTransform2DType;
#else
		(void)info;
		return false;
#endif
	}

	// Validity contract: pointer is invalidated by any structural change to the pool; callers must refetch each frame.
	static void* Funke_Entity_GetComponentPtr(uint64_t entityID, const char* componentName)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return nullptr;

		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info || !info->getRaw) return nullptr;

		void* raw = info->getRaw(scene->GetEntity(handle));
		// entt::null marks the gate without adding a per-entity list entry — the hierarchy pass
		// reprocesses every root regardless, so the flag alone is enough and stays allocation-free.
		if (raw && IsTransform2DComponentInfo(info)) scene->MarkTransformDirty(entt::null);
		return raw;
	}

	// Managed side wraps this in `ref readonly`, so skipping the dirty gate is safe and avoids a full hierarchy pass per read.
	static void* Funke_Entity_GetComponentPtrReadOnly(uint64_t entityID, const char* componentName)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return nullptr;

		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info || !info->getRaw) return nullptr;
		return info->getRaw(scene->GetEntity(handle));
	}

	static int Funke_Entity_GetComponentSize(const char* componentName)
	{
		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info) return 0;
		return static_cast<int>(info->rawSize);
	}

	uint64_t Funke_Entity_Clone(uint64_t sourceEntityID)
	{
		Scene* targetScene = GetScene();
		if (!targetScene) return 0;

		Scene* sourceScene = nullptr;
		EntityHandle sourceHandle = entt::null;
		if (!ResolveEntityReference(sourceEntityID, sourceScene, sourceHandle)) return 0;

		Entity source = sourceScene->GetEntity(sourceHandle);
		Entity clone = targetScene->CloneEntity(source);
		if (!clone.IsValid()) return 0;
		return GetEntityScriptId(*targetScene, clone.GetHandle());
	}

	uint64_t Funke_Entity_InstantiatePrefab(uint64_t prefabGuid)
	{
		Scene* targetScene = GetScene();
		if (!targetScene) return 0;

		const EntityHandle instance = SceneSerializer::InstantiatePrefab(*targetScene, prefabGuid);
		return instance != entt::null ? GetEntityScriptId(*targetScene, instance) : 0;
	}

	// Called by Entity.Instantiate once the clone is parented and placed, so OnAwake sees the spawn position.
	static void Funke_Entity_CreateScriptInstances(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return;
		ScriptSystem::CreateScriptInstancesNow(*scene, handle);
	}

	int Funke_Entity_GetOrigin(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return static_cast<int>(EntityOrigin::Runtime);
		return static_cast<int>(scene->GetEntityOrigin(handle));
	}

	uint64_t Funke_Entity_GetRuntimeID(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return scene->GetRuntimeID(handle);
	}

	uint64_t Funke_Entity_GetSceneGUID(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return static_cast<uint64_t>(scene->GetSceneEntityGUID(handle));
	}

	uint64_t Funke_Entity_GetPrefabGUID(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return static_cast<uint64_t>(scene->GetPrefabGUID(handle));
	}

	// ── Scene ───────────────────────────────────────────────────────────

	static int Funke_Scene_GetActiveSceneNameBuffer(char* outBuffer, int capacity)
	{
		FNK_BINDING_TRY {
			Scene* scene = GetScene();
			if (!scene) return CopyCStringToBuffer("", outBuffer, capacity);
			return CopyCStringToBuffer(scene->GetName().c_str(), outBuffer, capacity);
		} FNK_BINDING_CATCH(CopyCStringToBuffer("", outBuffer, capacity));
	}

	static int CountSceneEntities(const Scene& scene) {
		int count = 0;
		auto view = scene.GetRegistry().view<entt::entity>();
		for (EntityHandle entityHandle : view) {
			if (scene.GetRegistry().valid(entityHandle)) {
				++count;
			}
		}

		return count;
	}

	static int Funke_Scene_GetEntityCount() {
		Scene* scene = GetScene();
		if (!scene) return 0;
		return CountSceneEntities(*scene);
	}

	static int Funke_Scene_GetEntityCountByName(const char* sceneName) {
		if (!sceneName || sceneName[0] == '\0') return 0;
		auto scene = SceneManager::GetLoadedScene(sceneName).lock();
		if (!scene || !scene->IsLoaded()) return 0;
		return CountSceneEntities(*scene);
	}

	static int Funke_Scene_GetEntityNameByUUIDBuffer(uint64_t uuid, char* outBuffer, int capacity)
	{
		Scene* scene = GetScene();
		if (!scene) return CopyCStringToBuffer("", outBuffer, capacity);

		Scene* resolvedScene = nullptr;
		EntityHandle resolvedHandle = entt::null;
		if (ResolveEntityReference(uuid, resolvedScene, resolvedHandle)
			&& resolvedScene
			&& resolvedScene->HasComponent<NameComponent>(resolvedHandle)) {
			return CopyCStringToBuffer(resolvedScene->GetComponent<NameComponent>(resolvedHandle).Name.c_str(), outBuffer, capacity);
		}

		auto view = scene->GetRegistry().view<UUIDComponent, NameComponent>();
		for (auto [ent, uuidComp, nameComp] : view.each()) {
			if (static_cast<uint64_t>(uuidComp.Id) == uuid) {
				return CopyCStringToBuffer(nameComp.Name.c_str(), outBuffer, capacity);
			}
		}
		return CopyCStringToBuffer("", outBuffer, capacity);
	}

	static int LoadSceneByName(const char* sceneName, bool additive) {
		return ProjectSceneLoader::Load(sceneName ? std::string(sceneName) : std::string(), additive) ? 1 : 0;
	}

	static int Funke_Scene_LoadAdditive(const char* sceneName) {
		return LoadSceneByName(sceneName, true);
	}

	static int Funke_Scene_Load(const char* sceneName) {
		FNK_BINDING_TRY {
			return LoadSceneByName(sceneName, false);
		} FNK_BINDING_CATCH(0)
	}

	static void Funke_Scene_Unload(const char* sceneName) {
		SceneManager::UnloadScene(sceneName);
	}

	static int Funke_Scene_SetActive(const char* sceneName) {
		return SceneManager::SetActiveScene(sceneName) ? 1 : 0;
	}

	static int Funke_Scene_Reload(const char* sceneName) {
		auto result = SceneManager::ReloadScene(sceneName);
		return result.lock() ? 1 : 0;
	}

	static int Funke_Scene_SetSceneSystemEnabled(const char* sceneName, const char* className, int enabled) {
		if (!sceneName || !className || sceneName[0] == '\0' || className[0] == '\0') return 0;
		auto scene = SceneManager::GetLoadedScene(sceneName).lock();
		return scene && scene->SetSceneSystemEnabled(className, enabled != 0) ? 1 : 0;
	}

	static int Funke_Scene_IsSceneSystemEnabled(const char* sceneName, const char* className) {
		if (!sceneName || !className || sceneName[0] == '\0' || className[0] == '\0') return 0;
		auto scene = SceneManager::GetLoadedScene(sceneName).lock();
		return scene && scene->IsSceneSystemEnabled(className) ? 1 : 0;
	}

	static void Funke_Scene_SetGlobalSystemEnabled(const char* className, int enabled) {
		ScriptEngine::SetGlobalSystemEnabled(className ? className : "", enabled != 0);
	}

	static int Funke_Scene_DoesSceneExist(const char* sceneName) {
		if (!sceneName || sceneName[0] == '\0') return 0;
		return SceneManager::HasSceneDefinition(sceneName) ? 1 : 0;
	}

	static int Funke_Scene_GetLoadedCount() {
		return static_cast<int>(SceneManager::GetLoadedSceneCount());
	}

	static int Funke_Scene_GetLoadedSceneNameAtBuffer(int index, char* outBuffer, int capacity)
	{
		if (index < 0) {
			return CopyCStringToBuffer("", outBuffer, capacity);
		}

		const Scene* scene = SceneManager::GetLoadedSceneAt(static_cast<size_t>(index));
		return CopyCStringToBuffer(scene ? scene->GetName().c_str() : "", outBuffer, capacity);
	}

	// ── Scene by GUID ───────────────────────────────────────────────────

	static std::string ResolveSceneNameForGuid(uint64_t sceneGuid) {
		if (sceneGuid == 0) return {};
		if (AssetRegistry::GetKind(sceneGuid) != AssetKind::Scene) return {};

		const std::string path = AssetRegistry::ResolvePath(sceneGuid);
		if (path.empty()) return {};

		return std::filesystem::path(path).stem().string();
	}

	static int Funke_Scene_LoadByGuid(uint64_t sceneGuid) {
		const std::string sceneName = ResolveSceneNameForGuid(sceneGuid);
		if (sceneName.empty()) {
			FNK_CORE_ERROR_TAG("ScriptBindings",
				"Scene_LoadByGuid: no Scene asset registered for GUID {}.", sceneGuid);
			return 0;
		}
		return LoadSceneByName(sceneName.c_str(), false);
	}

	static int Funke_Scene_LoadAdditiveByGuid(uint64_t sceneGuid) {
		const std::string sceneName = ResolveSceneNameForGuid(sceneGuid);
		if (sceneName.empty()) {
			FNK_CORE_ERROR_TAG("ScriptBindings",
				"Scene_LoadAdditiveByGuid: no Scene asset registered for GUID {}.", sceneGuid);
			return 0;
		}
		return LoadSceneByName(sceneName.c_str(), true);
	}

	static void Funke_Scene_UnloadByGuid(uint64_t sceneGuid) {
		const std::string sceneName = ResolveSceneNameForGuid(sceneGuid);
		if (sceneName.empty()) {
			FNK_CORE_WARN_TAG("ScriptBindings",
				"Scene_UnloadByGuid: no Scene asset registered for GUID {}.", sceneGuid);
			return;
		}
		SceneManager::UnloadScene(sceneName);
	}

	static int Funke_Scene_SetActiveByGuid(uint64_t sceneGuid) {
		const std::string sceneName = ResolveSceneNameForGuid(sceneGuid);
		if (sceneName.empty()) return 0;
		return SceneManager::SetActiveScene(sceneName) ? 1 : 0;
	}

	static int Funke_Scene_ReloadByGuid(uint64_t sceneGuid) {
		const std::string sceneName = ResolveSceneNameForGuid(sceneGuid);
		if (sceneName.empty()) return 0;
		auto result = SceneManager::ReloadScene(sceneName);
		return result.lock() ? 1 : 0;
	}

	static int Funke_Scene_DoesSceneExistByGuid(uint64_t sceneGuid) {
		return ResolveSceneNameForGuid(sceneGuid).empty() ? 0 : 1;
	}

	static uint64_t Funke_Scene_GetActiveSceneGuid() {
		Scene* scene = GetScene();
		if (!scene) return 0;
		const FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) return 0;
		const std::string path = project->GetSceneFilePath(scene->GetName());
		if (path.empty()) return 0;
		return AssetRegistry::GetOrCreateAssetUUID(path);
	}

	// ── Scene Query ─────────────────────────────────────────────────────

	static Scene* ResolveLoadedSceneForQuery(const char* sceneName) {
		if (!sceneName || sceneName[0] == '\0') {
			return GetScene();
		}

		auto scene = SceneManager::GetLoadedScene(sceneName).lock();
		return scene && scene->IsLoaded() ? scene.get() : nullptr;
	}

	static int QueryEntitiesInScene(Scene* scene, const char* componentNames, uint64_t* outEntityIDs, int maxOut) {
		if (!scene || !componentNames || !outEntityIDs || maxOut <= 0) return 0;
		if (!scene->IsLoaded()) return 0;

		// string_view walk avoids the two-heap-allocations-per-token cost of the old std::string + substr approach in this hot per-frame path.
		std::vector<QueryComponentRequirement> requirements;
		const std::string_view names(componentNames);
		size_t start = 0;
		while (start <= names.size()) {
			size_t end = names.find('|', start);
			if (end == std::string_view::npos) end = names.size();
			const std::string_view token = names.substr(start, end - start);
			if (!token.empty()) {
				QueryComponentRequirement requirement;
				if (!BuildComponentRequirement(token, requirement)) return 0;
				requirements.push_back(std::move(requirement));
			}
			if (end == names.size()) break;
			start = end + 1;
		}
		if (requirements.empty()) return 0;

		int count = 0;
		auto& registry = scene->GetRegistry();

		// Fast path for single native component: iterate its typed storage (O(matchingEntities)) instead of the registry-wide walk (O(totalEntities × types)).
		if (requirements.size() == 1
			&& requirements[0].Native
			&& requirements[0].Native->storageHash != 0)
		{
			const auto* storage = registry.storage(requirements[0].Native->storageHash);
			if (!storage) return 0;
			for (auto entityHandle : *storage) {
				if (count < maxOut)
					outEntityIDs[count] = GetEntityScriptId(*scene, entityHandle);
				count++;
			}
			return count;
		}

		auto view = registry.view<entt::entity>();
		for (EntityHandle entityHandle : view) {
			if (!registry.valid(entityHandle)) continue;

			Entity entity = scene->GetEntity(entityHandle);
			bool match = true;
			for (const auto& requirement : requirements) {
				if (!requirement.Has(*scene, entityHandle, entity)) { match = false; break; }
			}
			if (match) {
				if (count < maxOut)
					outEntityIDs[count] = GetEntityScriptId(*scene, entityHandle);
				count++;
			}
		}
		return count;
	}

	static int Funke_Scene_QueryEntities(const char* componentNames, uint64_t* outEntityIDs, int maxOut) {
		FNK_BINDING_TRY {
			return QueryEntitiesInScene(GetScene(), componentNames, outEntityIDs, maxOut);
		} FNK_BINDING_CATCH(0);
	}

	static int Funke_Scene_QueryEntitiesInScene(const char* sceneName, const char* componentNames, uint64_t* outEntityIDs, int maxOut) {
		FNK_BINDING_TRY {
			return QueryEntitiesInScene(ResolveLoadedSceneForQuery(sceneName), componentNames, outEntityIDs, maxOut);
		} FNK_BINDING_CATCH(0);
	}

	static int Funke_Asset_IsValid(uint64_t assetId)
	{
		return AssetRegistry::Exists(assetId) ? 1 : 0;
	}

	static uint64_t Funke_Asset_GetOrCreateUUIDFromPath(const char* path)
	{
		if (!path || path[0] == '\0') {
			return 0;
		}

		return AssetRegistry::GetOrCreateAssetUUID(path);
	}

	static int Funke_Asset_GetPathBuffer(uint64_t assetId, char* outBuffer, int capacity)
	{
		FNK_BINDING_TRY {
			const std::string path = AssetRegistry::ResolvePath(assetId);
			return CopyCStringToBuffer(path.c_str(), outBuffer, capacity);
		} FNK_BINDING_CATCH(CopyCStringToBuffer("", outBuffer, capacity));
	}

	static int Funke_Asset_GetDisplayNameBuffer(uint64_t assetId, char* outBuffer, int capacity)
	{
		FNK_BINDING_TRY {
			const std::string name = AssetRegistry::GetDisplayName(assetId);
			return CopyCStringToBuffer(name.c_str(), outBuffer, capacity);
		} FNK_BINDING_CATCH(CopyCStringToBuffer("", outBuffer, capacity));
	}

	static int Funke_Asset_GetKind(uint64_t assetId)
	{
		return static_cast<int>(AssetRegistry::GetKind(assetId));
	}

	static int Funke_DataAsset_EnsureLoaded(uint64_t guid)
	{
		return DataAssetManager::Load(guid) ? 1 : 0;
	}

	static int Funke_Asset_FindAllBuffer(const char* pathPrefix, int kind, char* outBuffer, int capacity)
	{
		FNK_BINDING_TRY {
			Json::Value ids = Json::Value::MakeArray();
			for (const AssetRegistry::Record& record : AssetRegistry::FindAll(
					 static_cast<AssetKind>(kind),
					 pathPrefix ? pathPrefix : "")) {
				ids.Append(Json::Value(std::to_string(record.Id)));
			}

			const std::string serialized = Json::Stringify(ids, false);
			return CopyCStringToBuffer(serialized.c_str(), outBuffer, capacity);
		} FNK_BINDING_CATCH(CopyCStringToBuffer("[]", outBuffer, capacity));
	}

	static int Funke_Texture_LoadAsset(uint64_t assetId)
	{
		return TextureManager::LoadTextureByUUID(assetId).IsValid() ? 1 : 0;
	}

	static int Funke_Texture_GetWidth(uint64_t assetId)
	{
		TextureHandle handle = TextureManager::LoadTextureByUUID(assetId);
		Texture2D* texture = TextureManager::GetTexture(handle);
		return texture ? static_cast<int>(texture->GetWidth()) : 0;
	}

	static int Funke_Texture_GetHeight(uint64_t assetId)
	{
		TextureHandle handle = TextureManager::LoadTextureByUUID(assetId);
		Texture2D* texture = TextureManager::GetTexture(handle);
		return texture ? static_cast<int>(texture->GetHeight()) : 0;
	}

	// ── Runtime / procedural textures ───────────────────────────────────
	// rgba is width*height*4 RGBA8 bytes owned by the C# side. Returns the
	// synthetic UUID (0 on failure) which the script assigns to a
	// SpriteRenderer / Image like any other texture.
	static uint64_t Funke_Texture_CreateRuntime(int width, int height, const uint8_t* rgba, int filter)
	{
		return TextureManager::CreateRuntimeTexture(width, height, rgba,
			static_cast<Filter>(filter), Wrap::Clamp, Wrap::Clamp);
	}
	// Re-upload the full image (byteCount must be width*height*4). Returns 1 on success.
	static int Funke_Texture_UpdateRuntime(uint64_t runtimeUuid, const uint8_t* rgba, int byteCount)
	{
		if (byteCount < 0) return 0;
		return TextureManager::UpdateRuntimeTexture(runtimeUuid, rgba,
			static_cast<size_t>(byteCount)) ? 1 : 0;
	}
	static void Funke_Texture_DestroyRuntime(uint64_t runtimeUuid)
	{
		TextureManager::DestroyRuntimeTexture(runtimeUuid);
	}
	static int Funke_Texture_IsRuntime(uint64_t uuid)
	{
		return TextureManager::IsRuntimeTextureUUID(uuid) ? 1 : 0;
	}

	static uint64_t Funke_Texture_GetDefaultAssetUUID(uint8_t which)
	{
		// Cap is exclusive of any future enum entries — `Invisible` is the
		// last documented value. Anything beyond gets a zero GUID so the
		// C# layer surfaces the missing-asset as null.
		if (which > static_cast<uint8_t>(DefaultTexture::Invisible)) {
			return 0;
		}
		TextureHandle handle = TextureManager::GetDefaultTexture(static_cast<DefaultTexture>(which));
		if (!handle.IsValid()) {
			return 0;
		}
		return TextureManager::GetTextureAssetUUID(handle);
	}

	static int Funke_Audio_LoadAsset(uint64_t assetId)
	{
		return AudioManager::LoadAudioByUUID(assetId).IsValid() ? 1 : 0;
	}

	static int Funke_Font_LoadAsset(uint64_t assetId)
	{
		// DO NOT bake an atlas here — atlas slots are per (uuid, pixelSize); TextRenderer bakes at FontSize on first draw.
		return assetId != 0 && AssetRegistry::IsFont(assetId) ? 1 : 0;
	}

	static void Funke_Audio_PlayOneShotAsset(uint64_t assetId, float volume)
	{
		AudioHandle handle = AudioManager::LoadAudioByUUID(assetId);
		if (handle.IsValid()) {
			AudioManager::PlayOneShot(handle, volume);
		}
	}

	static bool ParseComponentNames(const char* names, std::vector<QueryComponentRequirement>& out) {
		if (!names || names[0] == '\0') return true; // empty is valid (no filter)
		const std::string_view str(names);
		size_t start = 0;
		while (start <= str.size()) {
			size_t end = str.find('|', start);
			if (end == std::string_view::npos) end = str.size();
			const std::string_view token = str.substr(start, end - start);
			if (!token.empty()) {
				QueryComponentRequirement requirement;
				if (!BuildComponentRequirement(token, requirement)) return false;
				out.push_back(std::move(requirement));
			}
			if (end == str.size()) break;
			start = end + 1;
		}
		return true;
	}

	static int QueryEntitiesFilteredInScene(
		Scene* scene,
		const char* withComponents,
		const char* withoutComponents,
		const char* mustHaveComponents,
		int enableFilter,
		uint64_t* outEntityIDs, int maxOut)
	{
		if (!scene || !outEntityIDs || maxOut <= 0) return 0;
		if (!scene->IsLoaded()) return 0;

		std::vector<QueryComponentRequirement> withInfos, withoutInfos, mustHaveInfos;
		if (!ParseComponentNames(withComponents, withInfos)) return 0;
		if (!ParseComponentNames(withoutComponents, withoutInfos)) return 0;
		if (!ParseComponentNames(mustHaveComponents, mustHaveInfos)) return 0;
		if (withInfos.empty() && mustHaveInfos.empty()) return 0;

		int count = 0;
		auto& registry = scene->GetRegistry();
		auto view = registry.view<entt::entity>();

		for (auto entityHandle : view) {
			if (!registry.valid(entityHandle)) continue;

			Entity entity = scene->GetEntity(entityHandle);

			// Enable filter: 1 = enabled only, 2 = disabled only
			if (enableFilter == 1 && registry.all_of<DisabledTag>(entityHandle)) continue;
			if (enableFilter == 2 && !registry.all_of<DisabledTag>(entityHandle)) continue;

			// WITH: entity must have all these components
			bool match = true;
			for (const auto& requirement : withInfos) {
				if (!requirement.Has(*scene, entityHandle, entity)) { match = false; break; }
			}
			if (!match) continue;

			// MUST HAVE (With<>): entity must have these too
			for (const auto& requirement : mustHaveInfos) {
				if (!requirement.Has(*scene, entityHandle, entity)) { match = false; break; }
			}
			if (!match) continue;

			// WITHOUT: entity must NOT have any of these
			for (const auto& requirement : withoutInfos) {
				if (requirement.Has(*scene, entityHandle, entity)) { match = false; break; }
			}
			if (!match) continue;

			if (count < maxOut)
				outEntityIDs[count] = GetEntityScriptId(*scene, entityHandle);
			count++;
		}
		return count;
	}

	static int Funke_Scene_QueryEntitiesFiltered(
		const char* withComponents,
		const char* withoutComponents,
		const char* mustHaveComponents,
		int enableFilter,
		uint64_t* outEntityIDs, int maxOut)
	{
		return QueryEntitiesFilteredInScene(GetScene(), withComponents, withoutComponents, mustHaveComponents, enableFilter, outEntityIDs, maxOut);
	}

	// Carries raw sparse_set* so the inner loop uses direct contains/value calls instead of std::function dispatch (one virtual-style call per entity×pool was the dominant cost at 10K+ entities).
	struct ResolvedQueryPool {
		const ComponentInfo*     Info = nullptr;
		entt::sparse_set*        Storage = nullptr;     // EnTT-backed (built-in components)
		DynamicComponentStorage* DynStorage = nullptr;  // dynamic (user-authored C# components)
		DynamicComponentStorage::SceneView DynView;
		bool RequiresValue = true;  // write/readonly pools must yield pointers
	};

	static bool ResolveQueryPools(
		entt::registry& registry,
		const char* names,
		std::vector<ResolvedQueryPool>& out,
		bool requireValue)
	{
		if (!names || names[0] == '\0') return true;
		const std::string_view str(names);
		size_t start = 0;
		while (start <= str.size()) {
			size_t end = str.find('|', start);
			if (end == std::string_view::npos) end = str.size();
			const std::string_view token = str.substr(start, end - start);
			if (!token.empty()) {
				const ComponentInfo* info = FindComponentByName(token);
				if (!info) return false;
				// Write/readonly pools need a payload pointer. Empty / tag /
				// managed-only components can't supply one, so we reject them
				// here exactly as the prior ParseQueryViewPools did.
				if (requireValue && !info->getRaw) return false;
				if (!info->has) return false;
				// Both-null = pool never instantiated = zero rows for required pools / trivially satisfied for without pools.
				entt::sparse_set* storage =
					info->storageHash != 0 ? registry.storage(info->storageHash) : nullptr;
				DynamicComponentStorage* dynStorage =
					(storage == nullptr) ? info->dynamicStorage : nullptr;
				const auto dynView = dynStorage
					? dynStorage->GetSceneView(registry)
					: DynamicComponentStorage::SceneView{};
				out.push_back({ info, storage, dynStorage, dynView, requireValue });
			}
			if (end == str.size()) break;
			start = end + 1;
		}
		return true;
	}

	// Iterates the smallest AND-pool as driver; all other pools are probed via inline contains(). Cost scales with min(pool sizes) — O(matchingEntities) not O(totalEntities).
	static int Funke_Scene_OpenQueryViewImpl(
		const char* sceneName,
		const char* writeNames,
		const char* readonlyNames,
		const char* mustHaveNames,
		const char* withoutNames,
		int enableFilter,
		void** outPointers,
		uint64_t* outEntityIDs,
		int maxRows)
	{
		FNK_BINDING_TRY {
			Scene* scene = ResolveLoadedSceneForQuery(sceneName);
			if (!scene || !scene->IsLoaded()) return 0;
			auto& registry = scene->GetRegistry();

			FUNKE_PROFILE_SCOPE("Scene.OpenQueryView");

			std::vector<ResolvedQueryPool> writePools, roPools, withPools, withoutPools;
			if (!ResolveQueryPools(registry, writeNames,    writePools,   /*requireValue=*/true))  return 0;
			if (!ResolveQueryPools(registry, readonlyNames, roPools,      /*requireValue=*/true))  return 0;
			if (!ResolveQueryPools(registry, mustHaveNames, withPools,    /*requireValue=*/false)) return 0;
			if (!ResolveQueryPools(registry, withoutNames,  withoutPools, /*requireValue=*/false)) return 0;

			const size_t writeCount = writePools.size();
			const size_t roCount = roPools.size();
			const size_t poolCount = writeCount + roCount;
			if (poolCount == 0) return 0;  // empty query is a no-op

			// A writable Transform2D view lets the script move entities via ref-writes, which can't
			// flag the scene themselves; trip the hierarchy gate so moved parents propagate to their
			// children this frame. Placed before the single-pool fast path so that path is covered too.
			for (const ResolvedQueryPool& wp : writePools) {
				if (IsTransform2DComponentInfo(wp.Info)) { scene->MarkTransformDirty(entt::null); break; }
			}

			if (!outEntityIDs
				&& writeCount == 1
				&& roCount == 0
				&& withPools.empty()
				&& withoutPools.empty()
				&& writePools[0].Info->fillRawPointers) {
				return writePools[0].Info->fillRawPointers(registry, outPointers, maxRows, enableFilter);
			}

			auto poolSize = [](const ResolvedQueryPool& p) -> size_t {
				if (p.Storage)    return p.Storage->size();
				if (p.DynStorage) return p.DynView.Size();
				return 0;
			};

			// Empty-required short-circuit: any AND-pool with zero entities
			// means no entity in the scene satisfies the query.
			for (const auto& p : writePools) { if (poolSize(p) == 0) return 0; }
			for (const auto& p : roPools)    { if (poolSize(p) == 0) return 0; }
			for (const auto& p : withPools)  { if (poolSize(p) == 0) return 0; }

			// Pick the driver: smallest pool across writes ∪ readonlys ∪ must-haves.
			// The pointer is stable across the rest of this call (we don't touch
			// the pool vectors after this point).
			const ResolvedQueryPool* driver = nullptr;
			size_t driverSize = std::numeric_limits<size_t>::max();
			auto considerPool = [&](const std::vector<ResolvedQueryPool>& pools) {
				for (const auto& p : pools) {
					const size_t s = poolSize(p);
					if (s < driverSize) { driver = &p; driverSize = s; }
				}
			};
			considerPool(writePools);
			considerPool(roPools);
			considerPool(withPools);
			// At least one of write/ro must exist (poolCount > 0), so driver is
			// guaranteed non-null here. Belt-and-braces:
			if (!driver) return 0;

			const entt::sparse_set* disabledStorage = nullptr;
			if (enableFilter != 0) {
				disabledStorage = registry.storage(entt::type_hash<DisabledTag>::value());
				// enableFilter == 2 (DisabledOnly) with no disabled storage = 0 rows
				if (enableFilter == 2 && (!disabledStorage || disabledStorage->empty())) return 0;
			}

			// Vectors, not fixed arrays: With<>/Without<> chains and [WithAll]/[WithoutAll] have no upper bound.
			struct OtherPool {
				entt::sparse_set*        ss = nullptr;
				DynamicComponentStorage::SceneView dv;
			};
			std::vector<OtherPool> otherWrite, otherRo, otherWith, withoutP;
			otherWrite.reserve(writePools.size());
			otherRo.reserve(roPools.size());
			otherWith.reserve(withPools.size());
			withoutP.reserve(withoutPools.size());
			for (const auto& p : writePools) { if (&p != driver) otherWrite.push_back({ p.Storage, p.DynView }); }
			for (const auto& p : roPools)    { if (&p != driver) otherRo.push_back({ p.Storage, p.DynView }); }
			for (const auto& p : withPools)  { if (&p != driver) otherWith.push_back({ p.Storage, p.DynView }); }
			for (const auto& p : withoutPools) {
				// Both null = component pool never created = nobody has it →
				// the without-filter is trivially satisfied for every entity.
				// Drop the entry so the inner loop's contains() doesn't branch.
				if (p.Storage || p.DynStorage) {
					withoutP.push_back({ p.Storage, p.DynView });
				}
			}
			const size_t otherWriteCount = otherWrite.size();
			const size_t otherRoCount = otherRo.size();
			const size_t otherWithCount = otherWith.size();
			const size_t withoutCountResolved = withoutP.size();

			auto poolContains = [](const OtherPool& op, EntityHandle h) -> bool {
				return op.ss ? op.ss->contains(h) : op.dv.Contains(h);
			};

			auto matchesAllFilters = [&](EntityHandle h) -> bool {
				if (enableFilter == 1) {
					if (disabledStorage && disabledStorage->contains(h)) return false;
				} else if (enableFilter == 2) {
					// disabledStorage non-null guaranteed by the early-out above.
					if (!disabledStorage->contains(h)) return false;
				}
				for (size_t i = 0; i < otherWriteCount; ++i) if (!poolContains(otherWrite[i], h)) return false;
				for (size_t i = 0; i < otherRoCount;    ++i) if (!poolContains(otherRo[i],    h)) return false;
				for (size_t i = 0; i < otherWithCount;  ++i) if (!poolContains(otherWith[i],  h)) return false;
				for (size_t i = 0; i < withoutCountResolved; ++i) if (poolContains(withoutP[i], h)) return false;
				return true;
			};

			auto fillRow = [&](void** rowBase, EntityHandle h) {
				for (size_t j = 0; j < writeCount; ++j) {
					rowBase[j] = writePools[j].Storage
						? writePools[j].Storage->value(h)
						: writePools[j].DynView.Get(h);
				}
				for (size_t j = 0; j < roCount; ++j) {
					rowBase[writeCount + j] = roPools[j].Storage
						? roPools[j].Storage->value(h)
						: roPools[j].DynView.Get(h);
				}
			};

			// Driver packed array — random-accessible by index.
			// entt::sparse_set::data() and std::vector::data() both return contiguous
			// pointers into the dense entity list.
			const EntityHandle* drivePacked = driver->Storage
				? driver->Storage->data()
				: driver->DynView.Entities().data();

			// Path A (bitmap): one matchesAllFilters call per entity, result recorded in stack bitmap, then fill pass — faster when chunks fit in kMaxStackBitmap.
			// Path B (chunked-claim): two sweeps per chunk (count then fill) — fallback when chunks exceed bitmap budget.
			constexpr size_t kParallelThreshold = 1024;
			constexpr size_t kMaxStackBitmap = 8192;  // one bool per chunk entity, stack-allocated
			const int workerCount = JobSystem::GetWorkerCount();
			const bool tryParallel = driverSize >= kParallelThreshold && workerCount > 1;
			const size_t targetChunks = static_cast<size_t>(workerCount) * 4;
			const size_t expectedChunkSize = (targetChunks > 0)
				? (driverSize + targetChunks - 1) / targetChunks
				: driverSize;
			const bool useBitmap = tryParallel && expectedChunkSize <= kMaxStackBitmap;

			if (useBitmap) {
				FUNKE_PROFILE_SCOPE("Scene.OpenQueryView.Snapshot.ParallelBitmap");
				std::atomic<int> writeCursor{ 0 };
				ParallelFor(0, driverSize, [&](size_t lo, size_t hi) {
					const size_t chunkLen = hi - lo;
					// Defensive fallback if auto-grain exceeds stack budget.
					if (chunkLen > kMaxStackBitmap) {
						int lc = 0;
						for (size_t i = lo; i < hi; ++i) {
							if (matchesAllFilters(drivePacked[i])) ++lc;
						}
						if (lc == 0) return;
						const int ms = writeCursor.fetch_add(lc, std::memory_order_relaxed);
						if (!outPointers) return;
						int mi = ms;
						for (size_t i = lo; i < hi; ++i) {
							const EntityHandle h = drivePacked[i];
							if (!matchesAllFilters(h)) continue;
							if (mi < maxRows) {
								fillRow(outPointers + static_cast<size_t>(mi) * poolCount, h);
								if (outEntityIDs) {
									outEntityIDs[mi] = GetEntityScriptId(*scene, h);
								}
							}
							++mi;
						}
						return;
					}
					// Sweep 1: filter into bitmap, count matches.
					bool matches[kMaxStackBitmap];
					int localCount = 0;
					for (size_t i = lo; i < hi; ++i) {
						const bool m = matchesAllFilters(drivePacked[i]);
						matches[i - lo] = m;
						localCount += static_cast<int>(m);
					}
					if (localCount == 0) return;
					// Claim a contiguous output region (always — needed for total count).
					const int myStart = writeCursor.fetch_add(localCount, std::memory_order_relaxed);
					if (!outPointers) return;  // count-only call: skip the write sweep.
					// Sweep 2: write only matched rows using the bitmap. No filter recompute.
					int myIdx = myStart;
					for (size_t i = lo; i < hi; ++i) {
						if (!matches[i - lo]) continue;
						if (myIdx < maxRows) {
							fillRow(outPointers + static_cast<size_t>(myIdx) * poolCount, drivePacked[i]);
							if (outEntityIDs) {
								outEntityIDs[myIdx] = GetEntityScriptId(*scene, drivePacked[i]);
							}
						}
						++myIdx;
					}
				});
				return writeCursor.load(std::memory_order_relaxed);
			}

			if (tryParallel) {
				FUNKE_PROFILE_SCOPE("Scene.OpenQueryView.Snapshot.ParallelChunked");
				std::atomic<int> writeCursor{ 0 };
				ParallelFor(0, driverSize, [&](size_t lo, size_t hi) {
					int localCount = 0;
					for (size_t i = lo; i < hi; ++i) {
						if (matchesAllFilters(drivePacked[i])) ++localCount;
					}
					if (localCount == 0) return;
					const int myStart = writeCursor.fetch_add(localCount, std::memory_order_relaxed);
					if (!outPointers) return;
					int myIdx = myStart;
					for (size_t i = lo; i < hi; ++i) {
						const EntityHandle h = drivePacked[i];
						if (!matchesAllFilters(h)) continue;
						if (myIdx < maxRows) {
							fillRow(outPointers + static_cast<size_t>(myIdx) * poolCount, h);
							if (outEntityIDs) {
								outEntityIDs[myIdx] = GetEntityScriptId(*scene, h);
							}
						}
						++myIdx;
					}
				});
				return writeCursor.load(std::memory_order_relaxed);
			}

			// Serial fallback — small queries, or job system unavailable.
			FUNKE_PROFILE_SCOPE("Scene.OpenQueryView.Snapshot.Serial");
			int rowIndex = 0;
			for (size_t i = 0; i < driverSize; ++i) {
				const EntityHandle h = drivePacked[i];
				if (!matchesAllFilters(h)) continue;
				if (outPointers && rowIndex < maxRows) {
					fillRow(outPointers + static_cast<size_t>(rowIndex) * poolCount, h);
					if (outEntityIDs) {
						outEntityIDs[rowIndex] = GetEntityScriptId(*scene, h);
					}
				}
				++rowIndex;
			}
			return rowIndex;
		} FNK_BINDING_CATCH(0);
	}

	static int Funke_Scene_OpenQueryView(
		const char* sceneName,
		const char* writeNames,
		const char* readonlyNames,
		const char* mustHaveNames,
		const char* withoutNames,
		int enableFilter,
		void** outPointers,
		int maxRows)
	{
		return Funke_Scene_OpenQueryViewImpl(
			sceneName, writeNames, readonlyNames, mustHaveNames, withoutNames,
			enableFilter, outPointers, nullptr, maxRows);
	}

	static int Funke_Scene_OpenQueryViewWithEntities(
		const char* sceneName,
		const char* writeNames,
		const char* readonlyNames,
		const char* mustHaveNames,
		const char* withoutNames,
		int enableFilter,
		void** outPointers,
		uint64_t* outEntityIDs,
		int maxRows)
	{
		return Funke_Scene_OpenQueryViewImpl(
			sceneName, writeNames, readonlyNames, mustHaveNames, withoutNames,
			enableFilter, outPointers, outEntityIDs, maxRows);
	}

	static int Funke_Scene_QueryEntitiesFilteredInScene(
		const char* sceneName,
		const char* withComponents,
		const char* withoutComponents,
		const char* mustHaveComponents,
		int enableFilter,
		uint64_t* outEntityIDs, int maxOut)
	{
		return QueryEntitiesFilteredInScene(ResolveLoadedSceneForQuery(sceneName), withComponents, withoutComponents, mustHaveComponents, enableFilter, outEntityIDs, maxOut);
	}

	// ── NameComponent ───────────────────────────────────────────────────

	static int Funke_NameComponent_GetNameBuffer(uint64_t entityID, char* outBuffer, int capacity)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle) || !scene->HasComponent<NameComponent>(handle)) {
			return CopyCStringToBuffer("", outBuffer, capacity);
		}

		return CopyCStringToBuffer(scene->GetComponent<NameComponent>(handle).Name.c_str(), outBuffer, capacity);
	}

	static void Funke_NameComponent_SetName(uint64_t entityID, const char* name)
	{
		FNK_BINDING_TRY {
			Scene* scene = nullptr;
			EntityHandle handle = entt::null;
			if (!ResolveEntityReference(entityID, scene, handle)) return;

			const std::string nextName = name ? name : "";
			if (nextName.empty()) {
				if (scene->HasComponent<NameComponent>(handle)) {
					scene->RemoveComponent<NameComponent>(handle);
					scene->MarkDirty();
				}
				return;
			}

			if (scene->HasComponent<NameComponent>(handle)) {
				scene->GetComponent<NameComponent>(handle).Name = nextName;
			}
			else {
				scene->AddComponent<NameComponent>(handle, nextName);
			}
			scene->MarkDirty();
		} FNK_BINDING_CATCH_VOID
	}

	// ── Entity hierarchy ──

	static uint64_t Funke_Entity_GetParent(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		Entity parent = scene->GetEntity(handle).GetParent();
		if (!parent.IsValid()) return 0;
		return GetEntityScriptId(*scene, parent.GetHandle());
	}

	static int Funke_Entity_SetParent(uint64_t entityID, uint64_t parentEntityID, int worldPositionStays)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		Entity entity = scene->GetEntity(handle);

		// Resolve the new parent (0 == detach to scene root).
		Entity newParent = Entity::Null;
		if (parentEntityID != 0) {
			Scene* parentScene = nullptr;
			EntityHandle parentHandle = entt::null;
			if (!ResolveEntityReference(parentEntityID, parentScene, parentHandle)) return 0;

			// Cross-scene parenting would silently corrupt the parent's HierarchyComponent
			// (the child's handle wouldn't resolve in the parent's registry), so refuse it.
			if (parentScene != scene) {
				FNK_CORE_WARN_TAG("ScriptBinding", "Entity.SetParent across scenes is not supported");
				return 0;
			}
			newParent = scene->GetEntity(parentHandle);
		}

#if FUNKE_WITH_2D
		if (worldPositionStays != 0 && SetParentKeepingWorld2D(*scene, handle, newParent)) return 1;
#endif
#if FUNKE_WITH_3D
		if (worldPositionStays != 0 && SetParentKeepingWorld3D(*scene, handle, newParent)) return 1;
#endif
		(void)worldPositionStays;
		entity.SetParent(newParent);
		return 1;
	}

	static int Funke_Entity_GetChildCount(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return static_cast<int>(scene->GetEntity(handle).GetChildren().size());
	}

	static uint64_t Funke_Entity_GetChildAt(uint64_t entityID, int index)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		const auto& children = scene->GetEntity(handle).GetChildren();
		if (index < 0 || static_cast<size_t>(index) >= children.size()) return 0;
		return GetEntityScriptId(*scene, children[static_cast<size_t>(index)]);
	}

	static int Funke_Entity_GetChildren(uint64_t entityID, uint64_t* outIDs, int maxOut)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		const auto& children = scene->GetEntity(handle).GetChildren();
		const int total = static_cast<int>(children.size());
		if (!outIDs || maxOut <= 0) return total;

		const int writeCount = std::min(total, maxOut);
		for (int i = 0; i < writeCount; ++i) {
			outIDs[i] = GetEntityScriptId(*scene, children[static_cast<size_t>(i)]);
		}
		return total;
	}

	// Front-most Interactable UI entity under the pointer (or 0). Reads UIEventSystem's per-frame
	// hover result, which is already gated on an enabled InteractableComponent.
	static int Funke_UI_GetHoveredEntity(uint64_t* outEntityID) {
		if (!outEntityID) return 0;
		*outEntityID = 0;

		Scene* scene = GetScene();
		if (!scene) return 0;

		auto* uiEvents = scene->GetSystem<UIEventSystem>();
		if (!uiEvents) return 0;

		EntityHandle hovered = uiEvents->GetHoveredEntity(*scene);
		if (hovered == entt::null || !scene->IsValid(hovered)) return 0;

		*outEntityID = GetEntityScriptId(*scene, hovered);
		return *outEntityID != 0 ? 1 : 0;
	}

	// ── Dynamic component registration (runtime, reflection-driven) ─────

	static uint32_t Funke_Component_RegisterDynamic(
		const char* displayName,
		const char* serializedName,
		const char* subcategory,
		uint32_t category,
		uint32_t size,
		uint32_t alignment)
	{
		const std::string display = displayName ? displayName : std::string{};
		const std::string serial  = serializedName ? serializedName : display;
		const std::string sub     = subcategory ? subcategory : std::string{ "Custom" };
		const ComponentCategory cat = (category == 1)
			? ComponentCategory::Tag
			: ComponentCategory::Component;
		auto& registry = SceneManager::GetComponentRegistry();
		return registry.RegisterDynamic(display, serial, sub, cat, size, alignment);
	}

	static void Funke_Component_UnregisterAllDynamic()
	{
		auto& registry = SceneManager::GetComponentRegistry();
		registry.UnregisterAllDynamic();
	}

	// ── TextRenderer ────────────────────────────────────────────────────

	static int Funke_TextRenderer_GetTextBuffer(uint64_t entityID, char* outBuffer, int capacity)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle) || !scene->HasComponent<TextRendererComponent>(handle)) {
			return CopyCStringToBuffer("", outBuffer, capacity);
		}
		return CopyCStringToBuffer(scene->GetComponent<TextRendererComponent>(handle).Text.c_str(), outBuffer, capacity);
	}

	static void Funke_TextRenderer_SetText(uint64_t entityID, const char* text)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.Text = text ? text : "";
	}

	static uint64_t Funke_TextRenderer_GetFont(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);

		uint64_t assetId = static_cast<uint64_t>(comp.FontAssetId);
		if (assetId == 0 && FontManager::IsValid(comp.ResolvedFont)) {
			assetId = FontManager::GetFontAssetUUID(comp.ResolvedFont);
			if (assetId != 0) {
				comp.FontAssetId = UUID(assetId);
			}
		}
		return assetId;
	}

	static void Funke_TextRenderer_SetFont(uint64_t entityID, uint64_t assetId)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );

		comp.FontAssetId = UUID(assetId);
		// Force the renderer to re-resolve next frame at the current FontSize.
		comp.ResolvedFont = FontHandle{};
	}

	static float Funke_TextRenderer_GetFontSize(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 32.0f);
		return comp.FontSize;
	}

	static void Funke_TextRenderer_SetFontSize(uint64_t entityID, float size)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.FontSize = size;
		// A new pixel size needs a different atlas slot — invalidate the cache.
		comp.ResolvedFont = FontHandle{};
	}

	static void Funke_TextRenderer_GetColor(uint64_t entityID, float* r, float* g, float* b, float* a)
	{
		GET_COMPONENT(TextRendererComponent, entityID, (void)(*r = 1, *g = 1, *b = 1, *a = 1));
		*r = comp.Color.r; *g = comp.Color.g; *b = comp.Color.b; *a = comp.Color.a;
	}

	static void Funke_TextRenderer_SetColor(uint64_t entityID, float r, float g, float b, float a)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.Color = { r, g, b, a };
	}

	static float Funke_TextRenderer_GetLetterSpacing(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0.0f);
		return comp.LetterSpacing;
	}

	static void Funke_TextRenderer_SetLetterSpacing(uint64_t entityID, float spacing)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.LetterSpacing = spacing;
	}

	static float Funke_TextRenderer_GetLineSpacing(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0.0f);
		return comp.LineSpacing;
	}

	static void Funke_TextRenderer_SetLineSpacing(uint64_t entityID, float spacing)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.LineSpacing = spacing;
	}

	// values: outlineWidth, outline r g b a, shadowOffsetX, shadowOffsetY, shadow r g b a.
	static void Funke_TextRenderer_GetEffects(uint64_t entityID, float* values, int count)
	{
		if (!values || count <= 0) return;
		GET_COMPONENT(TextRendererComponent, entityID, );
		const float all[] = {
			comp.OutlineWidth, comp.OutlineColor.r, comp.OutlineColor.g, comp.OutlineColor.b, comp.OutlineColor.a,
			comp.ShadowOffset.x, comp.ShadowOffset.y, comp.ShadowColor.r, comp.ShadowColor.g, comp.ShadowColor.b, comp.ShadowColor.a,
		};
		std::copy_n(all, std::min<int>(count, static_cast<int>(std::size(all))), values);
	}

	static void Funke_TextRenderer_SetEffects(uint64_t entityID, const float* values, int count)
	{
		if (!values || count < 11) return;
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.OutlineWidth = std::isfinite(values[0]) ? std::max(values[0], 0.0f) : 0.0f;
		comp.OutlineColor = Color{ values[1], values[2], values[3], values[4] };
		comp.ShadowOffset = Vec2{ values[5], values[6] };
		comp.ShadowColor = Color{ values[7], values[8], values[9], values[10] };
	}

	static int Funke_TextRenderer_GetHAlign(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);
		return static_cast<int>(comp.HAlign);
	}

	static void Funke_TextRenderer_SetHAlign(uint64_t entityID, int align)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.HAlign = static_cast<TextAlignment>(align);
	}

	static int Funke_TextRenderer_GetVAlign(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);
		return static_cast<int>(comp.VAlign);
	}

	static void Funke_TextRenderer_SetVAlign(uint64_t entityID, int align)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.VAlign = static_cast<TextVerticalAlignment>(align);
	}

	static int Funke_TextRenderer_GetWrapMode(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);
		return static_cast<int>(comp.WrapMode);
	}

	static void Funke_TextRenderer_SetWrapMode(uint64_t entityID, int mode)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.WrapMode = static_cast<TextWrapMode>(mode);
	}

	// WrapWidth get/set thunks removed alongside the field. The C#
	// TextRenderer.WrapWidth property is gone too; use Margin to
	// inset wrapped text.

	static int Funke_TextRenderer_GetSortingOrder(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);
		return comp.SortingOrder;
	}

	static void Funke_TextRenderer_SetSortingOrder(uint64_t entityID, int order)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.SortingOrder = static_cast<int16_t>(order);
	}

	static int Funke_TextRenderer_GetSortingLayer(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);
		return comp.SortingLayer;
	}

	static void Funke_TextRenderer_SetSortingLayer(uint64_t entityID, int layer)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.SortingLayer = static_cast<uint8_t>(layer);
	}

	// ── AudioSource ─────────────────────────────────────────────────────

	static void Funke_AudioSource_Play(uint64_t entityID) {
		GET_COMPONENT(AudioSourceComponent, entityID, );
		// Started from a script: begins at the entity now, not where the system saw it last frame.
		if (comp.IsSpatial()) AudioUpdateSystem::SyncPosition(scene->GetRegistry(), handle, comp, 0.0f);
		AudioManager::PlayAudioSource(comp);
	}
	static void Funke_AudioSource_Pause(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::PauseAudioSource(comp); }
	static void Funke_AudioSource_Stop(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::StopAudioSource(comp); }
	static void Funke_AudioSource_Resume(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::ResumeAudioSource(comp); }

	static float Funke_AudioSource_GetVolume(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 1.0f); return comp.GetVolume(); }
	static void  Funke_AudioSource_SetVolume(uint64_t entityID, float volume) { GET_COMPONENT(AudioSourceComponent, entityID, ); comp.SetVolume(std::clamp(volume, 0.0f, 1.0f)); }
	static float Funke_AudioSource_GetPitch(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 1.0f); return comp.GetPitch(); }
	static void  Funke_AudioSource_SetPitch(uint64_t entityID, float pitch) { GET_COMPONENT(AudioSourceComponent, entityID, ); comp.SetPitch(std::clamp(pitch, 0.1f, 3.0f)); }
	static int   Funke_AudioSource_GetLoop(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 0); return comp.IsLooping() ? 1 : 0; }
	static void  Funke_AudioSource_SetLoop(uint64_t entityID, int loop) { GET_COMPONENT(AudioSourceComponent, entityID, ); comp.SetLoop(loop != 0); }
	static int   Funke_AudioSource_IsPlaying(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 0); return comp.IsPlaying() ? 1 : 0; }
	static int   Funke_AudioSource_IsPaused(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 0); return comp.IsPaused() ? 1 : 0; }

	// values: spatial (0/1), minDistance, maxDistance, rolloff, panning, dopplerLevel
	static void Funke_AudioSource_GetSpatial(uint64_t entityID, float* values, int count) {
		if (!values) return;
		const AudioSourceComponent fallback;
		const AudioSourceComponent* source = &fallback;
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (ResolveEntityReference(entityID, scene, handle) && scene->IsValid(handle) && scene->HasComponent<AudioSourceComponent>(handle)) {
			source = &scene->GetComponent<AudioSourceComponent>(handle);
		}
		const AudioSpatialSettings& s = source->GetSpatialSettings();
		const float packed[6] = { source->IsSpatial() ? 1.0f : 0.0f, s.MinDistance, s.MaxDistance, static_cast<float>(s.Rolloff), s.Panning, s.DopplerLevel };
		for (int i = 0; i < count && i < 6; ++i) values[i] = packed[i];
	}

	static void Funke_AudioSource_SetSpatial(uint64_t entityID, const float* values, int count) {
		GET_COMPONENT(AudioSourceComponent, entityID, );
		if (!values) return;
		AudioSpatialSettings s = comp.GetSpatialSettings();
		if (count > 1) s.MinDistance = values[1];
		if (count > 2) s.MaxDistance = values[2];
		if (count > 3) s.Rolloff = static_cast<AudioRolloff>(std::clamp(static_cast<int>(values[3]), 0, 1));
		if (count > 4) s.Panning = values[4];
		if (count > 5) s.DopplerLevel = values[5];
		comp.SetSpatialSettings(s);
		if (count > 0 && (values[0] != 0.0f) != comp.IsSpatial()) {
			if (values[0] != 0.0f) AudioUpdateSystem::SyncPosition(scene->GetRegistry(), handle, comp, 0.0f);
			comp.SetSpatial(values[0] != 0.0f);
		}
	}

	// values: x, y, z, volume, minDistance, maxDistance, rolloff, panning, dopplerLevel
	static void Funke_Audio_PlayOneShotAt(uint64_t assetId, const float* values, int count) {
		if (!values || count < 3) return;
		const AudioHandle audio = AudioManager::LoadAudioByUUID(assetId);
		if (!audio.IsValid()) return;
		AudioSpatialSettings s;
		const float volume = count > 3 ? values[3] : 1.0f;
		if (count > 4) s.MinDistance = values[4];
		if (count > 5) s.MaxDistance = values[5];
		if (count > 6) s.Rolloff = static_cast<AudioRolloff>(std::clamp(static_cast<int>(values[6]), 0, 1));
		if (count > 7) s.Panning = values[7];
		if (count > 8) s.DopplerLevel = values[8];
		AudioManager::PlayOneShotAt(audio, glm::vec3(values[0], values[1], values[2]), std::max(volume, 0.0f), s);
	}

	static uint64_t Funke_AudioSource_GetAudio(uint64_t entityID)
	{
		GET_COMPONENT(AudioSourceComponent, entityID, 0);

		// Cache UUID resolved from a loaded handle so subsequent calls are O(1).
		uint64_t assetId = static_cast<uint64_t>(comp.GetAudioAssetId());
		if (assetId == 0 && comp.GetAudioHandle().IsValid()) {
			assetId = AudioManager::GetAudioAssetUUID(comp.GetAudioHandle());
			if (assetId != 0) {
				comp.SetAudioAssetId(UUID(assetId));
			}
		}
		return assetId;
	}

	static void Funke_AudioSource_SetAudio(uint64_t entityID, uint64_t assetId)
	{
		GET_COMPONENT(AudioSourceComponent, entityID, );

		if (assetId == 0) {
			comp.SetAudioHandle(AudioHandle(), UUID(0));
			return;
		}

		comp.SetAudioHandle(AudioManager::LoadAudioByUUID(assetId), UUID(assetId));
	}

	// ── Gizmos ──────────────────────────────────────────────────────────

	static void Funke_Gizmo_DrawLine(float x1, float y1, float x2, float y2) { Gizmo::DrawLine({ x1, y1 }, { x2, y2 }); }
	static void Funke_Gizmo_DrawWireSquare(float cx, float cy, float sx, float sy, float degrees) { Gizmo::DrawWireSquare({ cx, cy }, { sx, sy }, degrees); }
	static void Funke_Gizmo_DrawWireCircle(float cx, float cy, float radius, int segments) { Gizmo::DrawWireCircle({ cx, cy }, radius, segments); }
	static void Funke_Gizmo_DrawSquare(float cx, float cy, float sx, float sy, float degrees) { Gizmo::DrawSquare({ cx, cy }, { sx, sy }, degrees); }
	static void Funke_Gizmo_DrawCircle(float cx, float cy, float radius, int segments) { Gizmo::DrawCircle({ cx, cy }, radius, segments); }
	static void Funke_Gizmo_DrawText(const char* text, float x, float y, float rotation, float size) { Gizmo::DrawText(text ? text : "", { x, y }, rotation, size); }
	static void Funke_Gizmo_SetColor(float r, float g, float b, float a) { Gizmo::SetColor(Color(r, g, b, a)); }
	static void Funke_Gizmo_GetColor(float* r, float* g, float* b, float* a) { Color c = Gizmo::GetColor(); *r = c.r; *g = c.g; *b = c.b; *a = c.a; }
	static float Funke_Gizmo_GetLineWidth() { return Gizmo::GetLineWidth(); }
	static void Funke_Gizmo_SetLineWidth(float width) { Gizmo::SetLineWidth(width); }
	static void Funke_Gizmo_SetMaxVertices(int maxVertices) { Gizmo::SetMaxVertices(static_cast<size_t>(maxVertices < 0 ? 0 : maxVertices)); }
	static int Funke_Gizmo_GetMaxVertices() { return static_cast<int>(Gizmo::GetMaxVertices()); }
	static int Funke_Gizmo_GetRegisteredVertices() { return static_cast<int>(Gizmo::GetRegisteredVertices()); }
		static int Funke_Gizmo_GetEnabled() { return Gizmo::IsEnabled() ? 1 : 0; }
		static void Funke_Gizmo_SetEnabled(int enabled) { Gizmo::SetEnabled(enabled != 0); }

	// ── UI: RectTransform2D ─────────────────────────────────────────────

	struct RectLayoutReference {
		Vec2 Min{ 0.0f, 0.0f };
		Vec2 Max{ 0.0f, 0.0f };
		Vec2 Pivot{ 0.0f, 0.0f };
		Vec2 Scale{ 1.0f, 1.0f };
		float Rotation = 0.0f;
	};

	static bool TryGetUiViewportSize(int& outWidth, int& outHeight) {
		const Window::UIRegion uiRegion = Window::GetUIRegion();
		if (uiRegion.IsActive()) {
			outWidth = uiRegion.Width;
			outHeight = uiRegion.Height;
			return outWidth > 0 && outHeight > 0;
		}

		Viewport* viewport = Window::GetMainViewport();
		if (!viewport || viewport->GetWidth() <= 0 || viewport->GetHeight() <= 0) {
			outWidth = 0;
			outHeight = 0;
			return false;
		}

		outWidth = viewport->GetWidth();
		outHeight = viewport->GetHeight();
		return true;
	}

	static bool TryGetRootRectReference(RectLayoutReference& outReference) {
		int vpW = 0;
		int vpH = 0;
		if (!TryGetUiViewportSize(vpW, vpH)) {
			return false;
		}

		// Same parent rect and scale UILayoutSystem gives UI roots: the safe area of the UI surface.
		const float halfW = static_cast<float>(vpW) * 0.5f;
		const float halfH = static_cast<float>(vpH) * 0.5f;
		const Window::SafeAreaInsets insets = Window::GetSafeAreaInsets();
		outReference.Min = Vec2{ -halfW + insets.Left, -halfH + insets.Bottom };
		outReference.Max = Vec2{ halfW - insets.Right, halfH - insets.Top };
		const FunkeProject* project = ProjectManager::GetCurrentProject();
		const float uiScale = project
			? project->ComputeUIScale(outReference.Max.x - outReference.Min.x, outReference.Max.y - outReference.Min.y)
			: 1.0f;
		outReference.Pivot = Vec2{ 0.0f, 0.0f };
		outReference.Scale = Vec2{ uiScale, uiScale };
		outReference.Rotation = 0.0f;
		return true;
	}

	static Vec2 ScreenPositionToUiSpace(float x, float y, int viewportWidth, int viewportHeight) {
		return Vec2{
			x - static_cast<float>(viewportWidth) * 0.5f,
			static_cast<float>(viewportHeight) * 0.5f - y
		};
	}

	static Vec2 UiSpaceToScreenPosition(const Vec2& position, int viewportWidth, int viewportHeight) {
		return Vec2{
			position.x + static_cast<float>(viewportWidth) * 0.5f,
			static_cast<float>(viewportHeight) * 0.5f - position.y
		};
	}

	static Vec2 RotateAround(const Vec2& point, const Vec2& pivot, float rotation) {
		if (rotation == 0.0f) return point;
		const float c = std::cos(rotation);
		const float s = std::sin(rotation);
		const float dx = point.x - pivot.x;
		const float dy = point.y - pivot.y;
		return Vec2{
			pivot.x + c * dx - s * dy,
			pivot.y + s * dx + c * dy
		};
	}

	static RectLayoutReference ResolveRectReference(
		const RectTransform2DComponent& rect,
		const RectLayoutReference& parent)
	{
		RectLayoutReference resolved;
		resolved.Rotation = parent.Rotation + rect.LocalRotation;
		resolved.Scale = Vec2{
			parent.Scale.x * rect.LocalScale.x,
			parent.Scale.y * rect.LocalScale.y
		};

		const Vec2 parentSize{ parent.Max.x - parent.Min.x, parent.Max.y - parent.Min.y };
		const Vec2 normMin{
			std::min(rect.AnchorMin.x, rect.AnchorMax.x),
			std::min(rect.AnchorMin.y, rect.AnchorMax.y)
		};
		const Vec2 normMax{
			std::max(rect.AnchorMin.x, rect.AnchorMax.x),
			std::max(rect.AnchorMin.y, rect.AnchorMax.y)
		};

		const Vec2 anchorBL{
			parent.Min.x + parentSize.x * normMin.x,
			parent.Min.y + parentSize.y * normMin.y
		};
		const Vec2 anchorTR{
			parent.Min.x + parentSize.x * normMax.x,
			parent.Min.y + parentSize.y * normMax.y
		};
		const Vec2 anchorCenter{
			(anchorBL.x + anchorTR.x) * 0.5f,
			(anchorBL.y + anchorTR.y) * 0.5f
		};

		Vec2 pivotWorld{
			anchorCenter.x + rect.AnchoredPosition.x * parent.Scale.x,
			anchorCenter.y + rect.AnchoredPosition.y * parent.Scale.y
		};
		pivotWorld = RotateAround(pivotWorld, parent.Pivot, parent.Rotation);

		const Vec2 finalSize{
			rect.SizeDelta.x * resolved.Scale.x,
			rect.SizeDelta.y * resolved.Scale.y
		};
		resolved.Min = Vec2{
			pivotWorld.x - finalSize.x * rect.Pivot.x,
			pivotWorld.y - finalSize.y * rect.Pivot.y
		};
		resolved.Max = Vec2{ resolved.Min.x + finalSize.x, resolved.Min.y + finalSize.y };
		resolved.Pivot = pivotWorld;
		return resolved;
	}

	static bool TryResolveRectReference(Scene& scene, EntityHandle entity, RectLayoutReference& outReference, int depth = 0);

	static bool TryResolveParentRectReference(Scene& scene, EntityHandle entity, RectLayoutReference& outReference, int depth) {
		EntityHandle parent = entt::null;
		if (scene.HasComponent<HierarchyComponent>(entity)) {
			parent = scene.GetComponent<HierarchyComponent>(entity).Parent;
		}

		if (parent != entt::null && scene.IsValid(parent) && scene.HasComponent<RectTransform2DComponent>(parent)) {
			return TryResolveRectReference(scene, parent, outReference, depth + 1);
		}

		return TryGetRootRectReference(outReference);
	}

	static bool TryResolveRectReference(Scene& scene, EntityHandle entity, RectLayoutReference& outReference, int depth) {
		if (depth > 128 || !scene.IsValid(entity) || !scene.HasComponent<RectTransform2DComponent>(entity)) {
			return false;
		}

		const auto& rect = scene.GetComponent<RectTransform2DComponent>(entity);
		if (rect.ResolvedValid) {
			outReference.Min = rect.ResolvedMin;
			outReference.Max = rect.ResolvedMax;
			outReference.Pivot = rect.ResolvedPivot;
			outReference.Scale = rect.Scale;
			outReference.Rotation = rect.Rotation;
			return true;
		}

		RectLayoutReference parent;
		if (!TryResolveParentRectReference(scene, entity, parent, depth)) {
			return false;
		}
		outReference = ResolveRectReference(rect, parent);
		return true;
	}

	static Vec2 ResolveAnchorCenter(const RectTransform2DComponent& rect, const RectLayoutReference& parent) {
		const Vec2 parentSize{ parent.Max.x - parent.Min.x, parent.Max.y - parent.Min.y };
		const Vec2 normMin{
			std::min(rect.AnchorMin.x, rect.AnchorMax.x),
			std::min(rect.AnchorMin.y, rect.AnchorMax.y)
		};
		const Vec2 normMax{
			std::max(rect.AnchorMin.x, rect.AnchorMax.x),
			std::max(rect.AnchorMin.y, rect.AnchorMax.y)
		};

		const Vec2 anchorBL{
			parent.Min.x + parentSize.x * normMin.x,
			parent.Min.y + parentSize.y * normMin.y
		};
		const Vec2 anchorTR{
			parent.Min.x + parentSize.x * normMax.x,
			parent.Min.y + parentSize.y * normMax.y
		};
		return Vec2{
			(anchorBL.x + anchorTR.x) * 0.5f,
			(anchorBL.y + anchorTR.y) * 0.5f
		};
	}

	static void Funke_RectTransform_GetScreenPosition(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 0.0f, *outY = 0.0f));

		int vpW = 0;
		int vpH = 0;
		if (!TryGetUiViewportSize(vpW, vpH)) {
			*outX = 0.0f;
			*outY = 0.0f;
			return;
		}

		RectLayoutReference resolved;
		if (!TryResolveRectReference(*scene, handle, resolved)) {
			*outX = 0.0f;
			*outY = 0.0f;
			return;
		}

		const Vec2 center{
			(resolved.Min.x + resolved.Max.x) * 0.5f,
			(resolved.Min.y + resolved.Max.y) * 0.5f
		};
		const Vec2 screenPosition = UiSpaceToScreenPosition(center, vpW, vpH);
		*outX = screenPosition.x;
		*outY = screenPosition.y;
	}

	static void Funke_RectTransform_SetScreenPosition(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );

		int vpW = 0;
		int vpH = 0;
		if (!TryGetUiViewportSize(vpW, vpH)) {
			return;
		}

		RectLayoutReference parent;
		if (!TryResolveParentRectReference(*scene, handle, parent, 0)) {
			return;
		}

		const Vec2 uiPosition = ScreenPositionToUiSpace(x, y, vpW, vpH);
		const Vec2 rectScale{
			parent.Scale.x * comp.LocalScale.x,
			parent.Scale.y * comp.LocalScale.y
		};
		const Vec2 finalSize{
			comp.SizeDelta.x * rectScale.x,
			comp.SizeDelta.y * rectScale.y
		};
		const Vec2 desiredPivot{
			uiPosition.x + finalSize.x * (comp.Pivot.x - 0.5f),
			uiPosition.y + finalSize.y * (comp.Pivot.y - 0.5f)
		};
		const Vec2 unrotated = RotateAround(desiredPivot, parent.Pivot, -parent.Rotation);
		const Vec2 anchorCenter = ResolveAnchorCenter(comp, parent);
		const float scaleX = parent.Scale.x != 0.0f ? parent.Scale.x : 1.0f;
		const float scaleY = parent.Scale.y != 0.0f ? parent.Scale.y : 1.0f;
		comp.AnchoredPosition = Vec2{
			(unrotated.x - anchorCenter.x) / scaleX,
			(unrotated.y - anchorCenter.y) / scaleY
		};

		const RectLayoutReference resolved = ResolveRectReference(comp, parent);
		comp.Rotation = resolved.Rotation;
		comp.Scale = resolved.Scale;
		comp.ResolvedMin = resolved.Min;
		comp.ResolvedMax = resolved.Max;
		comp.ResolvedPivot = resolved.Pivot;
		comp.ResolvedValid = true;
	}

	static void Funke_RectTransform_GetAnchorMin(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 0.5f, *outY = 0.5f));
		*outX = comp.AnchorMin.x; *outY = comp.AnchorMin.y;
	}
	static void Funke_RectTransform_SetAnchorMin(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.AnchorMin = { x, y };
	}
	static void Funke_RectTransform_GetAnchorMax(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 0.5f, *outY = 0.5f));
		*outX = comp.AnchorMax.x; *outY = comp.AnchorMax.y;
	}
	static void Funke_RectTransform_SetAnchorMax(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.AnchorMax = { x, y };
	}
	static void Funke_RectTransform_GetPivot(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 0.5f, *outY = 0.5f));
		*outX = comp.Pivot.x; *outY = comp.Pivot.y;
	}
	static void Funke_RectTransform_SetPivot(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.Pivot = { x, y };
	}
	static void Funke_RectTransform_GetAnchoredPosition(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 0.0f, *outY = 0.0f));
		*outX = comp.AnchoredPosition.x; *outY = comp.AnchoredPosition.y;
	}
	static void Funke_RectTransform_SetAnchoredPosition(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.AnchoredPosition = { x, y };
	}
	static void Funke_RectTransform_GetSizeDelta(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 100.0f, *outY = 100.0f));
		*outX = comp.SizeDelta.x; *outY = comp.SizeDelta.y;
	}
	static void Funke_RectTransform_SetSizeDelta(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.SizeDelta = { x, y };
	}
	// Get* returns world value (set by UILayoutSystem); Set* writes Local* — world writes would be overwritten on next layout pass.
	static float Funke_RectTransform_GetRotation(uint64_t entityID) {
		GET_COMPONENT(RectTransform2DComponent, entityID, 0.0f);
		return comp.Rotation;
	}
	static void Funke_RectTransform_SetRotation(uint64_t entityID, float rotation) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.LocalRotation = rotation;
	}
	static void Funke_RectTransform_GetScale(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 1.0f, *outY = 1.0f));
		*outX = comp.Scale.x; *outY = comp.Scale.y;
	}
	static void Funke_RectTransform_SetScale(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.LocalScale = { x, y };
	}
	static float Funke_RectTransform_GetLocalRotation(uint64_t entityID) {
		GET_COMPONENT(RectTransform2DComponent, entityID, 0.0f);
		return comp.LocalRotation;
	}
	static void Funke_RectTransform_SetLocalRotation(uint64_t entityID, float rotation) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.LocalRotation = rotation;
	}
	static void Funke_RectTransform_GetLocalScale(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 1.0f, *outY = 1.0f));
		*outX = comp.LocalScale.x; *outY = comp.LocalScale.y;
	}
	static void Funke_RectTransform_SetLocalScale(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.LocalScale = { x, y };
	}
	static void Funke_RectTransform_GetResolvedSize(uint64_t entityID, float* outW, float* outH) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outW = 0.0f, *outH = 0.0f));
		const Vec2 size = comp.GetSize();
		*outW = size.x; *outH = size.y;
	}

	// ── UI: Image ───────────────────────────────────────────────────────

	static void Funke_Image_GetColor(uint64_t entityID, float* r, float* g, float* b, float* a) {
		GET_COMPONENT(ImageComponent, entityID, (void)(*r = 1, *g = 1, *b = 1, *a = 1));
		*r = comp.Color.r; *g = comp.Color.g; *b = comp.Color.b; *a = comp.Color.a;
	}
	static void Funke_Image_SetColor(uint64_t entityID, float r, float g, float b, float a) {
		GET_COMPONENT(ImageComponent, entityID, );
		comp.Color = Color{ r, g, b, a };
	}
	static uint64_t Funke_Image_GetTexture(uint64_t entityID) {
		GET_COMPONENT(ImageComponent, entityID, 0ull);
		return static_cast<uint64_t>(comp.TextureAssetId);
	}
	static void Funke_Image_SetTexture(uint64_t entityID, uint64_t assetId) {
		GET_COMPONENT(ImageComponent, entityID, );
		comp.TextureAssetId = UUID(assetId);
		comp.TextureHandle = (assetId != 0)
			? TextureManager::LoadTextureByUUID(assetId)
			: TextureHandle{};
	}
	static int Funke_Image_GetSpriteNameBuffer(uint64_t entityID, char* outBuffer, int capacity) {
		GET_COMPONENT(ImageComponent, entityID, CopyCStringToBuffer("", outBuffer, capacity));
		return CopyStringToBuffer(comp.SpriteName, outBuffer, capacity);
	}
	static void Funke_Image_SetSpriteName(uint64_t entityID, const char* name) {
		GET_COMPONENT(ImageComponent, entityID, );
		comp.SpriteName = name ? name : "";
	}
	static int Funke_Image_GetSortingOrder(uint64_t entityID) {
		GET_COMPONENT(ImageComponent, entityID, 0);
		return comp.SortingOrder;
	}
	static void Funke_Image_SetSortingOrder(uint64_t entityID, int order) {
		GET_COMPONENT(ImageComponent, entityID, );
		comp.SortingOrder = static_cast<int16_t>(order);
	}
	static int Funke_Image_GetSortingLayer(uint64_t entityID) {
		GET_COMPONENT(ImageComponent, entityID, 0);
		return comp.SortingLayer;
	}
	static void Funke_Image_SetSortingLayer(uint64_t entityID, int layer) {
		GET_COMPONENT(ImageComponent, entityID, );
		comp.SortingLayer = static_cast<uint8_t>(layer);
	}
	static uint64_t Funke_Image_GetMaterial(uint64_t entityID) {
		GET_COMPONENT(ImageComponent, entityID, 0);
		return comp.Material;
	}
	static void Funke_Image_SetMaterial(uint64_t entityID, uint64_t materialGuid) {
		GET_COMPONENT(ImageComponent, entityID, );
		comp.Material = materialGuid;
	}
	static void Funke_Image_GetSlicing(uint64_t entityID, float* values, int count) {
		GET_COMPONENT(ImageComponent, entityID, );
		const float packed[6] = { static_cast<float>(comp.Type), comp.BorderLeft, comp.BorderBottom, comp.BorderRight, comp.BorderTop, comp.BorderScale };
		for (int i = 0; i < count && i < 6; ++i) values[i] = packed[i];
	}
	static void Funke_Image_SetSlicing(uint64_t entityID, const float* values, int count) {
		GET_COMPONENT(ImageComponent, entityID, );
		if (count < 6) return;
		comp.Type = static_cast<ImageType>(std::clamp(static_cast<int>(std::lround(values[0])), 0, static_cast<int>(ImageType::Tiled)));
		comp.BorderLeft = std::max(values[1], 0.0f);
		comp.BorderBottom = std::max(values[2], 0.0f);
		comp.BorderRight = std::max(values[3], 0.0f);
		comp.BorderTop = std::max(values[4], 0.0f);
		comp.BorderScale = std::max(values[5], 0.0f);
	}

	// ── UI: Interactable ────────────────────────────────────────────────

	static int Funke_Interactable_GetInteractable(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.Interactable ? 1 : 0;
	}
	static void Funke_Interactable_SetInteractable(uint64_t entityID, int value) {
		GET_COMPONENT(InteractableComponent, entityID, );
		comp.Interactable = value != 0;
	}
	static int Funke_Interactable_GetIsHovered(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.IsHovered ? 1 : 0;
	}
	static int Funke_Interactable_GetIsClicked(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.IsClicked ? 1 : 0;
	}
	static int Funke_Interactable_GetIsPressed(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.IsPressed ? 1 : 0;
	}
	static int Funke_Interactable_GetIsMouseDown(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.IsMouseDown ? 1 : 0;
	}
	static int Funke_Interactable_GetIsMouseUp(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.IsMouseUp ? 1 : 0;
	}
	static int Funke_Interactable_GetFocusable(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.Focusable ? 1 : 0;
	}
	static void Funke_Interactable_SetFocusable(uint64_t entityID, int value) {
		GET_COMPONENT(InteractableComponent, entityID, );
		comp.Focusable = value != 0;
		// When opting out at runtime, drop any leftover focus state so
		// the next UIFocusSystem tick doesn't see a stale "I'm focused"
		// flag on a non-focusable widget.
		if (!comp.Focusable) {
			comp.IsFocused = false;
		}
	}
	static int Funke_Interactable_GetIsFocused(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.IsFocused ? 1 : 0;
	}
	static void Funke_Interactable_SetIsFocused(uint64_t entityID, int value) {
		GET_COMPONENT(InteractableComponent, entityID, );
		// Programmatic focus is honoured for one frame. UIFocusSystem
		// reconciles next tick — if the widget isn't Focusable, the
		// flag will be cleared and IsFocused will drop back to false.
		comp.IsFocused = (value != 0);
	}

	// ── UI: state-color bindings (Button / Toggle / Slider / InputField / Dropdown) ──

	#define WIDGET_COLOR_BINDING(COMP, MEMBER, GETTER, SETTER) \
		static void GETTER(uint64_t entityID, float* r, float* g, float* b, float* a) { \
			GET_COMPONENT(COMP, entityID, (void)(*r = 1, *g = 1, *b = 1, *a = 1)); \
			*r = comp.MEMBER.r; *g = comp.MEMBER.g; *b = comp.MEMBER.b; *a = comp.MEMBER.a; \
		} \
		static void SETTER(uint64_t entityID, float r, float g, float b, float a) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.MEMBER = Color{ r, g, b, a }; \
		}

	WIDGET_COLOR_BINDING(ButtonComponent, NormalColor,   Funke_Button_GetNormalColor,   Funke_Button_SetNormalColor)
	WIDGET_COLOR_BINDING(ButtonComponent, HoveredColor,  Funke_Button_GetHoveredColor,  Funke_Button_SetHoveredColor)
	WIDGET_COLOR_BINDING(ButtonComponent, PressedColor,  Funke_Button_GetPressedColor,  Funke_Button_SetPressedColor)
	WIDGET_COLOR_BINDING(ButtonComponent, DisabledColor, Funke_Button_GetDisabledColor, Funke_Button_SetDisabledColor)

	WIDGET_COLOR_BINDING(SliderComponent, NormalColor,   Funke_Slider_GetNormalColor,   Funke_Slider_SetNormalColor)
	WIDGET_COLOR_BINDING(SliderComponent, HoveredColor,  Funke_Slider_GetHoveredColor,  Funke_Slider_SetHoveredColor)
	WIDGET_COLOR_BINDING(SliderComponent, PressedColor,  Funke_Slider_GetPressedColor,  Funke_Slider_SetPressedColor)
	WIDGET_COLOR_BINDING(SliderComponent, DisabledColor, Funke_Slider_GetDisabledColor, Funke_Slider_SetDisabledColor)

	WIDGET_COLOR_BINDING(ToggleComponent, NormalColor,   Funke_Toggle_GetNormalColor,   Funke_Toggle_SetNormalColor)
	WIDGET_COLOR_BINDING(ToggleComponent, HoveredColor,  Funke_Toggle_GetHoveredColor,  Funke_Toggle_SetHoveredColor)
	WIDGET_COLOR_BINDING(ToggleComponent, PressedColor,  Funke_Toggle_GetPressedColor,  Funke_Toggle_SetPressedColor)
	WIDGET_COLOR_BINDING(ToggleComponent, DisabledColor, Funke_Toggle_GetDisabledColor, Funke_Toggle_SetDisabledColor)

	WIDGET_COLOR_BINDING(InputFieldComponent, NormalColor,   Funke_InputField_GetNormalColor,   Funke_InputField_SetNormalColor)
	WIDGET_COLOR_BINDING(InputFieldComponent, HoveredColor,  Funke_InputField_GetHoveredColor,  Funke_InputField_SetHoveredColor)
	WIDGET_COLOR_BINDING(InputFieldComponent, PressedColor,  Funke_InputField_GetPressedColor,  Funke_InputField_SetPressedColor)
	WIDGET_COLOR_BINDING(InputFieldComponent, DisabledColor, Funke_InputField_GetDisabledColor, Funke_InputField_SetDisabledColor)

	WIDGET_COLOR_BINDING(DropdownComponent, NormalColor,   Funke_Dropdown_GetNormalColor,   Funke_Dropdown_SetNormalColor)
	WIDGET_COLOR_BINDING(DropdownComponent, HoveredColor,  Funke_Dropdown_GetHoveredColor,  Funke_Dropdown_SetHoveredColor)
	WIDGET_COLOR_BINDING(DropdownComponent, PressedColor,  Funke_Dropdown_GetPressedColor,  Funke_Dropdown_SetPressedColor)
	WIDGET_COLOR_BINDING(DropdownComponent, DisabledColor, Funke_Dropdown_GetDisabledColor, Funke_Dropdown_SetDisabledColor)

	// Optional focus-state tint — alpha == 0 sentinel = "skip", so
	// scripts can read / write a fully-zeroed color to opt out of any
	// visual focus indicator without affecting navigation.
	WIDGET_COLOR_BINDING(ButtonComponent,    FocusedColor, Funke_Button_GetFocusedColor,    Funke_Button_SetFocusedColor)
	WIDGET_COLOR_BINDING(ToggleComponent,    FocusedColor, Funke_Toggle_GetFocusedColor,    Funke_Toggle_SetFocusedColor)
	WIDGET_COLOR_BINDING(SliderComponent,    FocusedColor, Funke_Slider_GetFocusedColor,    Funke_Slider_SetFocusedColor)
	WIDGET_COLOR_BINDING(InputFieldComponent,FocusedColor, Funke_InputField_GetFocusedColor,Funke_InputField_SetFocusedColor)
	WIDGET_COLOR_BINDING(DropdownComponent,  FocusedColor, Funke_Dropdown_GetFocusedColor,  Funke_Dropdown_SetFocusedColor)
	#undef WIDGET_COLOR_BINDING

	// ── UI: TransitionMode + per-state sprite UUIDs ─────────────────

	#define WIDGET_TRANSITIONMODE_BINDING(COMP, GETTER, SETTER) \
		static int GETTER(uint64_t entityID) { \
			GET_COMPONENT(COMP, entityID, 0); \
			return static_cast<int>(comp.TransitionMode); \
		} \
		static void SETTER(uint64_t entityID, int mode) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.TransitionMode = static_cast<UITransitionMode>(mode); \
		}

	#define WIDGET_SPRITE_BINDING(COMP, MEMBER, GETTER, SETTER) \
		static uint64_t GETTER(uint64_t entityID) { \
			GET_COMPONENT(COMP, entityID, 0ull); \
			return static_cast<uint64_t>(comp.MEMBER); \
		} \
		static void SETTER(uint64_t entityID, uint64_t uuid) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.MEMBER = UUID(uuid); \
		}

	WIDGET_TRANSITIONMODE_BINDING(ButtonComponent,    Funke_Button_GetTransitionMode,    Funke_Button_SetTransitionMode)
	WIDGET_TRANSITIONMODE_BINDING(ToggleComponent,    Funke_Toggle_GetTransitionMode,    Funke_Toggle_SetTransitionMode)
	WIDGET_TRANSITIONMODE_BINDING(SliderComponent,    Funke_Slider_GetTransitionMode,    Funke_Slider_SetTransitionMode)
	WIDGET_TRANSITIONMODE_BINDING(InputFieldComponent,Funke_InputField_GetTransitionMode,Funke_InputField_SetTransitionMode)
	WIDGET_TRANSITIONMODE_BINDING(DropdownComponent,  Funke_Dropdown_GetTransitionMode,  Funke_Dropdown_SetTransitionMode)

	WIDGET_SPRITE_BINDING(ButtonComponent, NormalSprite,   Funke_Button_GetNormalSprite,   Funke_Button_SetNormalSprite)
	WIDGET_SPRITE_BINDING(ButtonComponent, HoveredSprite,  Funke_Button_GetHoveredSprite,  Funke_Button_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(ButtonComponent, PressedSprite,  Funke_Button_GetPressedSprite,  Funke_Button_SetPressedSprite)
	WIDGET_SPRITE_BINDING(ButtonComponent, DisabledSprite, Funke_Button_GetDisabledSprite, Funke_Button_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(ButtonComponent, FocusedSprite,  Funke_Button_GetFocusedSprite,  Funke_Button_SetFocusedSprite)

	WIDGET_SPRITE_BINDING(ToggleComponent, NormalSprite,   Funke_Toggle_GetNormalSprite,   Funke_Toggle_SetNormalSprite)
	WIDGET_SPRITE_BINDING(ToggleComponent, HoveredSprite,  Funke_Toggle_GetHoveredSprite,  Funke_Toggle_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(ToggleComponent, PressedSprite,  Funke_Toggle_GetPressedSprite,  Funke_Toggle_SetPressedSprite)
	WIDGET_SPRITE_BINDING(ToggleComponent, DisabledSprite, Funke_Toggle_GetDisabledSprite, Funke_Toggle_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(ToggleComponent, FocusedSprite,  Funke_Toggle_GetFocusedSprite,  Funke_Toggle_SetFocusedSprite)

	WIDGET_SPRITE_BINDING(SliderComponent, NormalSprite,   Funke_Slider_GetNormalSprite,   Funke_Slider_SetNormalSprite)
	WIDGET_SPRITE_BINDING(SliderComponent, HoveredSprite,  Funke_Slider_GetHoveredSprite,  Funke_Slider_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(SliderComponent, PressedSprite,  Funke_Slider_GetPressedSprite,  Funke_Slider_SetPressedSprite)
	WIDGET_SPRITE_BINDING(SliderComponent, DisabledSprite, Funke_Slider_GetDisabledSprite, Funke_Slider_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(SliderComponent, FocusedSprite,  Funke_Slider_GetFocusedSprite,  Funke_Slider_SetFocusedSprite)

	WIDGET_SPRITE_BINDING(InputFieldComponent, NormalSprite,   Funke_InputField_GetNormalSprite,   Funke_InputField_SetNormalSprite)
	WIDGET_SPRITE_BINDING(InputFieldComponent, HoveredSprite,  Funke_InputField_GetHoveredSprite,  Funke_InputField_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(InputFieldComponent, PressedSprite,  Funke_InputField_GetPressedSprite,  Funke_InputField_SetPressedSprite)
	WIDGET_SPRITE_BINDING(InputFieldComponent, DisabledSprite, Funke_InputField_GetDisabledSprite, Funke_InputField_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(InputFieldComponent, FocusedSprite,  Funke_InputField_GetFocusedSprite,  Funke_InputField_SetFocusedSprite)

	WIDGET_SPRITE_BINDING(DropdownComponent, NormalSprite,   Funke_Dropdown_GetNormalSprite,   Funke_Dropdown_SetNormalSprite)
	WIDGET_SPRITE_BINDING(DropdownComponent, HoveredSprite,  Funke_Dropdown_GetHoveredSprite,  Funke_Dropdown_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(DropdownComponent, PressedSprite,  Funke_Dropdown_GetPressedSprite,  Funke_Dropdown_SetPressedSprite)
	WIDGET_SPRITE_BINDING(DropdownComponent, DisabledSprite, Funke_Dropdown_GetDisabledSprite, Funke_Dropdown_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(DropdownComponent, FocusedSprite,  Funke_Dropdown_GetFocusedSprite,  Funke_Dropdown_SetFocusedSprite)
	#undef WIDGET_TRANSITIONMODE_BINDING
	#undef WIDGET_SPRITE_BINDING

	// ── UI: IsReadOnly + entity-ref + popup-option-color bindings ──
	// Entity-ref fields marshal as persistent UUID so refs survive scene reload.
	#define WIDGET_BOOL_BINDING(COMP, MEMBER, GETTER, SETTER) \
		static int GETTER(uint64_t entityID) { \
			GET_COMPONENT(COMP, entityID, 0); \
			return comp.MEMBER ? 1 : 0; \
		} \
		static void SETTER(uint64_t entityID, int value) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.MEMBER = (value != 0); \
		}

	#define WIDGET_ENTITYREF_BINDING(COMP, MEMBER, GETTER, SETTER) \
		static uint64_t GETTER(uint64_t entityID) { \
			GET_COMPONENT(COMP, entityID, 0ull); \
			if (comp.MEMBER == entt::null) return 0ull; \
			return scene->GetEntityPersistentID(comp.MEMBER); \
		} \
		static void SETTER(uint64_t entityID, uint64_t refUuid) { \
			GET_COMPONENT(COMP, entityID, ); \
			if (refUuid == 0) { comp.MEMBER = entt::null; return; } \
			EntityHandle resolved = entt::null; \
			if (scene->TryResolveEntityRef(refUuid, resolved)) { \
				comp.MEMBER = resolved; \
			} \
		}

	WIDGET_BOOL_BINDING(ToggleComponent,   IsReadOnly, Funke_Toggle_GetIsReadOnly,   Funke_Toggle_SetIsReadOnly)
	WIDGET_BOOL_BINDING(SliderComponent,   IsReadOnly, Funke_Slider_GetIsReadOnly,   Funke_Slider_SetIsReadOnly)
	WIDGET_BOOL_BINDING(DropdownComponent, IsReadOnly, Funke_Dropdown_GetIsReadOnly, Funke_Dropdown_SetIsReadOnly)

	WIDGET_ENTITYREF_BINDING(ButtonComponent,     TargetGraphic,    Funke_Button_GetTargetGraphic,        Funke_Button_SetTargetGraphic)
	WIDGET_ENTITYREF_BINDING(SliderComponent,     FillEntity,       Funke_Slider_GetFillEntity,           Funke_Slider_SetFillEntity)
	WIDGET_ENTITYREF_BINDING(SliderComponent,     HandleEntity,     Funke_Slider_GetHandleEntity,         Funke_Slider_SetHandleEntity)
	WIDGET_ENTITYREF_BINDING(SliderComponent,     BackgroundEntity, Funke_Slider_GetBackgroundEntity,     Funke_Slider_SetBackgroundEntity)
	WIDGET_ENTITYREF_BINDING(ToggleComponent,     CheckmarkEntity,  Funke_Toggle_GetCheckmarkEntity,      Funke_Toggle_SetCheckmarkEntity)
	WIDGET_ENTITYREF_BINDING(InputFieldComponent, TextEntity,       Funke_InputField_GetTextEntity,       Funke_InputField_SetTextEntity)
	WIDGET_ENTITYREF_BINDING(InputFieldComponent, VerticalScrollbarEntity, Funke_InputField_GetVerticalScrollbarEntity, Funke_InputField_SetVerticalScrollbarEntity)
	WIDGET_ENTITYREF_BINDING(DropdownComponent,   LabelEntity,      Funke_Dropdown_GetLabelEntity,        Funke_Dropdown_SetLabelEntity)

	// New per-state popup option colors. Reuse the existing color
	// macro so the marshalling shape stays identical.
	#define WIDGET_COLOR_BINDING(COMP, MEMBER, GETTER, SETTER) \
		static void GETTER(uint64_t entityID, float* r, float* g, float* b, float* a) { \
			GET_COMPONENT(COMP, entityID, (void)(*r = 1, *g = 1, *b = 1, *a = 1)); \
			*r = comp.MEMBER.r; *g = comp.MEMBER.g; *b = comp.MEMBER.b; *a = comp.MEMBER.a; \
		} \
		static void SETTER(uint64_t entityID, float r, float g, float b, float a) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.MEMBER = Color{ r, g, b, a }; \
		}

	WIDGET_COLOR_BINDING(DropdownComponent, OptionNormalColor,    Funke_Dropdown_GetOptionNormalColor,    Funke_Dropdown_SetOptionNormalColor)
	WIDGET_COLOR_BINDING(DropdownComponent, OptionHoverColor,     Funke_Dropdown_GetOptionHoverColor,     Funke_Dropdown_SetOptionHoverColor)
	WIDGET_COLOR_BINDING(DropdownComponent, OptionPressedColor,   Funke_Dropdown_GetOptionPressedColor,   Funke_Dropdown_SetOptionPressedColor)
	WIDGET_COLOR_BINDING(DropdownComponent, OptionSelectedColor,  Funke_Dropdown_GetOptionSelectedColor,  Funke_Dropdown_SetOptionSelectedColor)
	WIDGET_COLOR_BINDING(DropdownComponent, PopupBackgroundColor, Funke_Dropdown_GetPopupBackgroundColor, Funke_Dropdown_SetPopupBackgroundColor)
	WIDGET_COLOR_BINDING(DropdownComponent, OptionTextColor,      Funke_Dropdown_GetOptionTextColor,      Funke_Dropdown_SetOptionTextColor)
	#undef WIDGET_COLOR_BINDING
	#undef WIDGET_BOOL_BINDING
	#undef WIDGET_ENTITYREF_BINDING

	// ── UI: Slider ──────────────────────────────────────────────────────

	static float Funke_Slider_GetValue(uint64_t entityID) {
		GET_COMPONENT(SliderComponent, entityID, 0.0f);
		return comp.Value;
	}
	static void Funke_Slider_SetValue(uint64_t entityID, float value) {
		GET_COMPONENT(SliderComponent, entityID, );
		comp.Value = std::clamp(value, std::min(comp.MinValue, comp.MaxValue), std::max(comp.MinValue, comp.MaxValue));
	}
	static float Funke_Slider_GetMinValue(uint64_t entityID) {
		GET_COMPONENT(SliderComponent, entityID, 0.0f);
		return comp.MinValue;
	}
	static void Funke_Slider_SetMinValue(uint64_t entityID, float value) {
		GET_COMPONENT(SliderComponent, entityID, );
		comp.MinValue = value;
	}
	static float Funke_Slider_GetMaxValue(uint64_t entityID) {
		GET_COMPONENT(SliderComponent, entityID, 1.0f);
		return comp.MaxValue;
	}
	static void Funke_Slider_SetMaxValue(uint64_t entityID, float value) {
		GET_COMPONENT(SliderComponent, entityID, );
		comp.MaxValue = value;
	}
	static int Funke_Slider_GetWholeNumbers(uint64_t entityID) {
		GET_COMPONENT(SliderComponent, entityID, 0);
		return comp.WholeNumbers ? 1 : 0;
	}
	static void Funke_Slider_SetWholeNumbers(uint64_t entityID, int value) {
		GET_COMPONENT(SliderComponent, entityID, );
		comp.WholeNumbers = value != 0;
	}
	static int Funke_Slider_GetValueChangedThisFrame(uint64_t entityID) {
		GET_COMPONENT(SliderComponent, entityID, 0);
		return comp.ValueChangedThisFrame ? 1 : 0;
	}
	// Prevents UIEventSystem from double-firing OnValueChanged when C# SetValue fires immediately before the tick diff.
	static void Funke_Slider_MarkValueObserved(uint64_t entityID) {
		GET_COMPONENT(SliderComponent, entityID, );
		comp.LastObservedValue = comp.Value;
		comp.ValueObserved = true;
	}

	// ── UI: Toggle ──────────────────────────────────────────────────────

	static int Funke_Toggle_GetIsOn(uint64_t entityID) {
		GET_COMPONENT(ToggleComponent, entityID, 0);
		return comp.IsOn ? 1 : 0;
	}
	static void Funke_Toggle_SetIsOn(uint64_t entityID, int value) {
		GET_COMPONENT(ToggleComponent, entityID, );
		comp.IsOn = value != 0;
	}
	static int Funke_Toggle_GetValueChangedThisFrame(uint64_t entityID) {
		GET_COMPONENT(ToggleComponent, entityID, 0);
		return comp.ValueChangedThisFrame ? 1 : 0;
	}
	// See Funke_Slider_MarkValueObserved — same role for Toggle.IsOn.
	static void Funke_Toggle_MarkIsOnObserved(uint64_t entityID) {
		GET_COMPONENT(ToggleComponent, entityID, );
		comp.LastObservedIsOn = comp.IsOn;
		comp.ValueObserved = true;
	}

	// ── UI: InputField ──────────────────────────────────────────────────

	static int Funke_InputField_GetTextBuffer(uint64_t entityID, char* outBuffer, int capacity) {
		GET_COMPONENT(InputFieldComponent, entityID, CopyStringToBuffer({}, outBuffer, capacity));
		return CopyStringToBuffer(comp.Text, outBuffer, capacity);
	}
	static void Funke_InputField_SetText(uint64_t entityID, const char* text) {
		GET_COMPONENT(InputFieldComponent, entityID, );
		comp.Text = text ? text : "";
	}
	static int Funke_InputField_GetPlaceholderTextBuffer(uint64_t entityID, char* outBuffer, int capacity) {
		GET_COMPONENT(InputFieldComponent, entityID, CopyStringToBuffer({}, outBuffer, capacity));
		return CopyStringToBuffer(comp.PlaceholderText, outBuffer, capacity);
	}
	static void Funke_InputField_SetPlaceholderText(uint64_t entityID, const char* text) {
		GET_COMPONENT(InputFieldComponent, entityID, );
		comp.PlaceholderText = text ? text : "";
	}
	static int Funke_InputField_GetIsFocused(uint64_t entityID) {
		GET_COMPONENT(InputFieldComponent, entityID, 0);
		return comp.IsFocused ? 1 : 0;
	}
	static void Funke_InputField_SetIsFocused(uint64_t entityID, int value) {
		GET_COMPONENT(InputFieldComponent, entityID, );
		comp.IsFocused = value != 0;
	}
	static int Funke_InputField_GetSubmittedThisFrame(uint64_t entityID) {
		GET_COMPONENT(InputFieldComponent, entityID, 0);
		return comp.SubmittedThisFrame ? 1 : 0;
	}
	static int Funke_InputField_GetCharacterLimit(uint64_t entityID) {
		GET_COMPONENT(InputFieldComponent, entityID, 0);
		return comp.CharacterLimit;
	}
	static void Funke_InputField_SetCharacterLimit(uint64_t entityID, int value) {
		GET_COMPONENT(InputFieldComponent, entityID, );
		comp.CharacterLimit = value;
	}
	static int Funke_InputField_GetMultiline(uint64_t entityID) {
		GET_COMPONENT(InputFieldComponent, entityID, 0);
		return comp.Multiline ? 1 : 0;
	}
	static void Funke_InputField_SetMultiline(uint64_t entityID, int value) {
		GET_COMPONENT(InputFieldComponent, entityID, );
		comp.Multiline = value != 0;
	}

	// ── UI: Dropdown ────────────────────────────────────────────────────

	static int Funke_Dropdown_GetSelectedIndex(uint64_t entityID) {
		GET_COMPONENT(DropdownComponent, entityID, 0);
		return comp.SelectedIndex;
	}
	static void Funke_Dropdown_SetSelectedIndex(uint64_t entityID, int value) {
		GET_COMPONENT(DropdownComponent, entityID, );
		comp.SelectedIndex = value;
	}
	static int Funke_Dropdown_GetIsOpen(uint64_t entityID) {
		GET_COMPONENT(DropdownComponent, entityID, 0);
		return comp.IsOpen ? 1 : 0;
	}
	static void Funke_Dropdown_SetIsOpen(uint64_t entityID, int value) {
		GET_COMPONENT(DropdownComponent, entityID, );
		comp.IsOpen = value != 0;
	}
	static int Funke_Dropdown_GetSelectionChangedThisFrame(uint64_t entityID) {
		GET_COMPONENT(DropdownComponent, entityID, 0);
		return comp.SelectionChangedThisFrame ? 1 : 0;
	}
	// See Funke_Slider_MarkValueObserved — same role for Dropdown.SelectedIndex.
	static void Funke_Dropdown_MarkSelectedIndexObserved(uint64_t entityID) {
		GET_COMPONENT(DropdownComponent, entityID, );
		comp.LastObservedSelectedIndex = comp.SelectedIndex;
		comp.SelectionObserved = true;
	}
	static int Funke_Dropdown_GetOptionCount(uint64_t entityID) {
		GET_COMPONENT(DropdownComponent, entityID, 0);
		return static_cast<int>(comp.Options.size());
	}
	static int Funke_Dropdown_GetOptionBuffer(uint64_t entityID, int index, char* outBuffer, int capacity) {
		GET_COMPONENT(DropdownComponent, entityID, CopyStringToBuffer({}, outBuffer, capacity));
		if (index < 0 || index >= static_cast<int>(comp.Options.size())) {
			return CopyStringToBuffer({}, outBuffer, capacity);
		}
		return CopyStringToBuffer(comp.Options[index], outBuffer, capacity);
	}
	static void Funke_Dropdown_SetOption(uint64_t entityID, int index, const char* text) {
		GET_COMPONENT(DropdownComponent, entityID, );
		if (index < 0 || index >= static_cast<int>(comp.Options.size())) return;
		comp.Options[index] = text ? text : "";
	}
	static void Funke_Dropdown_AddOption(uint64_t entityID, const char* text) {
		GET_COMPONENT(DropdownComponent, entityID, );
		comp.Options.emplace_back(text ? text : "");
	}
	static void Funke_Dropdown_RemoveOption(uint64_t entityID, int index) {
		GET_COMPONENT(DropdownComponent, entityID, );
		if (index < 0 || index >= static_cast<int>(comp.Options.size())) return;
		comp.Options.erase(comp.Options.begin() + index);
	}
	static void Funke_Dropdown_ClearOptions(uint64_t entityID) {
		GET_COMPONENT(DropdownComponent, entityID, );
		comp.Options.clear();
	}

	// ── UI: Scrollbar / ScrollRect / Mask / CircularSlider / Layout Groups / ContentSizeFitter / WidthConstraint ──

	#define WIDGET_FLOAT_BINDING(COMP, MEMBER, GETTER, SETTER) \
		static float GETTER(uint64_t entityID) { \
			GET_COMPONENT(COMP, entityID, 0.0f); \
			return comp.MEMBER; \
		} \
		static void SETTER(uint64_t entityID, float value) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.MEMBER = value; \
		}

	// Like WIDGET_FLOAT_BINDING but clamps the value to [MINV, MAXV] so a scripted set
	// can't push the field outside the range the inspector already enforces.
	#define WIDGET_FLOAT_BINDING_CLAMPED(COMP, MEMBER, MINV, MAXV, GETTER, SETTER) \
		static float GETTER(uint64_t entityID) { \
			GET_COMPONENT(COMP, entityID, 0.0f); \
			return comp.MEMBER; \
		} \
		static void SETTER(uint64_t entityID, float value) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.MEMBER = std::clamp(value, static_cast<float>(MINV), static_cast<float>(MAXV)); \
		}

	#define WIDGET_INT_BINDING(COMP, MEMBER, GETTER, SETTER) \
		static int GETTER(uint64_t entityID) { \
			GET_COMPONENT(COMP, entityID, 0); \
			return comp.MEMBER; \
		} \
		static void SETTER(uint64_t entityID, int value) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.MEMBER = value; \
		}

	// Like WIDGET_INT_BINDING but clamps the value to [MINV, MAXV] (same as the inspector).
	#define WIDGET_INT_BINDING_CLAMPED(COMP, MEMBER, MINV, MAXV, GETTER, SETTER) \
		static int GETTER(uint64_t entityID) { \
			GET_COMPONENT(COMP, entityID, 0); \
			return comp.MEMBER; \
		} \
		static void SETTER(uint64_t entityID, int value) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.MEMBER = std::clamp(value, static_cast<int>(MINV), static_cast<int>(MAXV)); \
		}

	#define WIDGET_BOOL_BINDING(COMP, MEMBER, GETTER, SETTER) \
		static int GETTER(uint64_t entityID) { \
			GET_COMPONENT(COMP, entityID, 0); \
			return comp.MEMBER ? 1 : 0; \
		} \
		static void SETTER(uint64_t entityID, int value) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.MEMBER = (value != 0); \
		}

	#define WIDGET_ENUM_BINDING(COMP, MEMBER, ENUM_TYPE, GETTER, SETTER) \
		static int GETTER(uint64_t entityID) { \
			GET_COMPONENT(COMP, entityID, 0); \
			return static_cast<int>(comp.MEMBER); \
		} \
		static void SETTER(uint64_t entityID, int value) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.MEMBER = static_cast<ENUM_TYPE>(value); \
		}

	#define WIDGET_VEC2_BINDING(COMP, MEMBER, GETTER, SETTER) \
		static void GETTER(uint64_t entityID, float* outX, float* outY) { \
			GET_COMPONENT(COMP, entityID, (void)(*outX = 0, *outY = 0)); \
			*outX = comp.MEMBER.x; *outY = comp.MEMBER.y; \
		} \
		static void SETTER(uint64_t entityID, float x, float y) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.MEMBER = Vec2{ x, y }; \
		}

	#define WIDGET_COLOR_BINDING(COMP, MEMBER, GETTER, SETTER) \
		static void GETTER(uint64_t entityID, float* r, float* g, float* b, float* a) { \
			GET_COMPONENT(COMP, entityID, (void)(*r = 1, *g = 1, *b = 1, *a = 1)); \
			*r = comp.MEMBER.r; *g = comp.MEMBER.g; *b = comp.MEMBER.b; *a = comp.MEMBER.a; \
		} \
		static void SETTER(uint64_t entityID, float r, float g, float b, float a) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.MEMBER = Color{ r, g, b, a }; \
		}

	#define WIDGET_SPRITE_BINDING(COMP, MEMBER, GETTER, SETTER) \
		static uint64_t GETTER(uint64_t entityID) { \
			GET_COMPONENT(COMP, entityID, 0ull); \
			return static_cast<uint64_t>(comp.MEMBER); \
		} \
		static void SETTER(uint64_t entityID, uint64_t uuid) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.MEMBER = UUID(uuid); \
		}

	#define WIDGET_TRANSITIONMODE_BINDING(COMP, GETTER, SETTER) \
		static int GETTER(uint64_t entityID) { \
			GET_COMPONENT(COMP, entityID, 0); \
			return static_cast<int>(comp.TransitionMode); \
		} \
		static void SETTER(uint64_t entityID, int mode) { \
			GET_COMPONENT(COMP, entityID, ); \
			comp.TransitionMode = static_cast<UITransitionMode>(mode); \
		}

	#define WIDGET_ENTITYREF_BINDING(COMP, MEMBER, GETTER, SETTER) \
		static uint64_t GETTER(uint64_t entityID) { \
			GET_COMPONENT(COMP, entityID, 0ull); \
			if (comp.MEMBER == entt::null) return 0ull; \
			return scene->GetEntityPersistentID(comp.MEMBER); \
		} \
		static void SETTER(uint64_t entityID, uint64_t refUuid) { \
			GET_COMPONENT(COMP, entityID, ); \
			if (refUuid == 0) { comp.MEMBER = entt::null; return; } \
			EntityHandle resolved = entt::null; \
			if (scene->TryResolveEntityRef(refUuid, resolved)) { \
				comp.MEMBER = resolved; \
			} \
		}

	// ── Scrollbar ───────────────────────────────────────────────────────
	WIDGET_FLOAT_BINDING_CLAMPED(ScrollbarComponent, Value, 0.0f, 1.0f, Funke_Scrollbar_GetValue, Funke_Scrollbar_SetValue)
	WIDGET_FLOAT_BINDING_CLAMPED(ScrollbarComponent, Size, 0.0f, 1.0f, Funke_Scrollbar_GetSize, Funke_Scrollbar_SetSize)
	WIDGET_INT_BINDING_CLAMPED(ScrollbarComponent, NumberOfSteps, 0, 11, Funke_Scrollbar_GetNumberOfSteps, Funke_Scrollbar_SetNumberOfSteps)
	WIDGET_ENUM_BINDING (ScrollbarComponent, Direction, ScrollbarDirection, Funke_Scrollbar_GetDirection, Funke_Scrollbar_SetDirection)
	WIDGET_BOOL_BINDING (ScrollbarComponent, IsReadOnly,    Funke_Scrollbar_GetIsReadOnly,    Funke_Scrollbar_SetIsReadOnly)
	WIDGET_ENTITYREF_BINDING(ScrollbarComponent, HandleEntity, Funke_Scrollbar_GetHandleEntity, Funke_Scrollbar_SetHandleEntity)

	static int  Funke_Scrollbar_GetValueChangedThisFrame(uint64_t entityID) {
		GET_COMPONENT(ScrollbarComponent, entityID, 0);
		return comp.ValueChangedThisFrame ? 1 : 0;
	}
	static void Funke_Scrollbar_MarkValueObserved(uint64_t entityID) {
		GET_COMPONENT(ScrollbarComponent, entityID, );
		comp.LastObservedValue = comp.Value;
		comp.ValueObserved = true;
	}

	WIDGET_COLOR_BINDING(ScrollbarComponent, NormalColor,   Funke_Scrollbar_GetNormalColor,   Funke_Scrollbar_SetNormalColor)
	WIDGET_COLOR_BINDING(ScrollbarComponent, HoveredColor,  Funke_Scrollbar_GetHoveredColor,  Funke_Scrollbar_SetHoveredColor)
	WIDGET_COLOR_BINDING(ScrollbarComponent, PressedColor,  Funke_Scrollbar_GetPressedColor,  Funke_Scrollbar_SetPressedColor)
	WIDGET_COLOR_BINDING(ScrollbarComponent, DisabledColor, Funke_Scrollbar_GetDisabledColor, Funke_Scrollbar_SetDisabledColor)
	WIDGET_COLOR_BINDING(ScrollbarComponent, FocusedColor,  Funke_Scrollbar_GetFocusedColor,  Funke_Scrollbar_SetFocusedColor)
	WIDGET_TRANSITIONMODE_BINDING(ScrollbarComponent, Funke_Scrollbar_GetTransitionMode, Funke_Scrollbar_SetTransitionMode)
	WIDGET_SPRITE_BINDING(ScrollbarComponent, NormalSprite,   Funke_Scrollbar_GetNormalSprite,   Funke_Scrollbar_SetNormalSprite)
	WIDGET_SPRITE_BINDING(ScrollbarComponent, HoveredSprite,  Funke_Scrollbar_GetHoveredSprite,  Funke_Scrollbar_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(ScrollbarComponent, PressedSprite,  Funke_Scrollbar_GetPressedSprite,  Funke_Scrollbar_SetPressedSprite)
	WIDGET_SPRITE_BINDING(ScrollbarComponent, DisabledSprite, Funke_Scrollbar_GetDisabledSprite, Funke_Scrollbar_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(ScrollbarComponent, FocusedSprite,  Funke_Scrollbar_GetFocusedSprite,  Funke_Scrollbar_SetFocusedSprite)

	// ── ScrollRect ──────────────────────────────────────────────────────
	WIDGET_ENTITYREF_BINDING(ScrollRectComponent, Content,             Funke_ScrollRect_GetContent,             Funke_ScrollRect_SetContent)
	WIDGET_ENTITYREF_BINDING(ScrollRectComponent, Viewport,            Funke_ScrollRect_GetViewport,            Funke_ScrollRect_SetViewport)
	WIDGET_ENTITYREF_BINDING(ScrollRectComponent, HorizontalScrollbar, Funke_ScrollRect_GetHorizontalScrollbar, Funke_ScrollRect_SetHorizontalScrollbar)
	WIDGET_ENTITYREF_BINDING(ScrollRectComponent, VerticalScrollbar,   Funke_ScrollRect_GetVerticalScrollbar,   Funke_ScrollRect_SetVerticalScrollbar)
	WIDGET_BOOL_BINDING (ScrollRectComponent, Horizontal,           Funke_ScrollRect_GetHorizontal,           Funke_ScrollRect_SetHorizontal)
	WIDGET_BOOL_BINDING (ScrollRectComponent, Vertical,             Funke_ScrollRect_GetVertical,             Funke_ScrollRect_SetVertical)
	WIDGET_BOOL_BINDING (ScrollRectComponent, Inertia,              Funke_ScrollRect_GetInertia,              Funke_ScrollRect_SetInertia)
	WIDGET_ENUM_BINDING (ScrollRectComponent, MovementType, ScrollRectMovementType, Funke_ScrollRect_GetMovementType, Funke_ScrollRect_SetMovementType)
	WIDGET_ENUM_BINDING (ScrollRectComponent, HorizontalScrollbarVisibility, ScrollbarVisibility, Funke_ScrollRect_GetHorizontalScrollbarVisibility, Funke_ScrollRect_SetHorizontalScrollbarVisibility)
	WIDGET_ENUM_BINDING (ScrollRectComponent, VerticalScrollbarVisibility,   ScrollbarVisibility, Funke_ScrollRect_GetVerticalScrollbarVisibility,   Funke_ScrollRect_SetVerticalScrollbarVisibility)
	WIDGET_FLOAT_BINDING_CLAMPED(ScrollRectComponent, Elasticity,       0.0f, 1.0f, Funke_ScrollRect_GetElasticity,       Funke_ScrollRect_SetElasticity)
	WIDGET_FLOAT_BINDING_CLAMPED(ScrollRectComponent, DecelerationRate, 0.0f, 1.0f, Funke_ScrollRect_GetDecelerationRate, Funke_ScrollRect_SetDecelerationRate)
	WIDGET_FLOAT_BINDING(ScrollRectComponent, ScrollSensitivity,           Funke_ScrollRect_GetScrollSensitivity,           Funke_ScrollRect_SetScrollSensitivity)
	WIDGET_FLOAT_BINDING(ScrollRectComponent, HorizontalScrollbarSpacing,  Funke_ScrollRect_GetHorizontalScrollbarSpacing,  Funke_ScrollRect_SetHorizontalScrollbarSpacing)
	WIDGET_FLOAT_BINDING(ScrollRectComponent, VerticalScrollbarSpacing,    Funke_ScrollRect_GetVerticalScrollbarSpacing,    Funke_ScrollRect_SetVerticalScrollbarSpacing)
	WIDGET_VEC2_BINDING (ScrollRectComponent, NormalizedPosition,          Funke_ScrollRect_GetNormalizedPosition,          Funke_ScrollRect_SetNormalizedPosition)

	static int  Funke_ScrollRect_GetValueChangedThisFrame(uint64_t entityID) {
		GET_COMPONENT(ScrollRectComponent, entityID, 0);
		return comp.ValueChangedThisFrame ? 1 : 0;
	}
	static void Funke_ScrollRect_MarkValueObserved(uint64_t entityID) {
		GET_COMPONENT(ScrollRectComponent, entityID, );
		comp.LastObservedNormalizedPosition = comp.NormalizedPosition;
		comp.ValueObserved = true;
	}

	// ── Mask ────────────────────────────────────────────────────────────
	WIDGET_BOOL_BINDING(MaskComponent, ShowMaskGraphic, Funke_Mask_GetShowMaskGraphic, Funke_Mask_SetShowMaskGraphic)

	// ── CircularSlider ──────────────────────────────────────────────────
	static float Funke_CircularSlider_GetValue(uint64_t entityID) {
		GET_COMPONENT(CircularSliderComponent, entityID, 0.0f);
		return comp.Value;
	}
	static void Funke_CircularSlider_SetValue(uint64_t entityID, float value) {
		GET_COMPONENT(CircularSliderComponent, entityID, );
		comp.Value = std::clamp(value, std::min(comp.MinValue, comp.MaxValue), std::max(comp.MinValue, comp.MaxValue));
	}
	WIDGET_FLOAT_BINDING(CircularSliderComponent, MinValue,          Funke_CircularSlider_GetMinValue,          Funke_CircularSlider_SetMinValue)
	WIDGET_FLOAT_BINDING(CircularSliderComponent, MaxValue,          Funke_CircularSlider_GetMaxValue,          Funke_CircularSlider_SetMaxValue)
	WIDGET_BOOL_BINDING (CircularSliderComponent, WholeNumbers,      Funke_CircularSlider_GetWholeNumbers,      Funke_CircularSlider_SetWholeNumbers)
	WIDGET_BOOL_BINDING (CircularSliderComponent, IsReadOnly,        Funke_CircularSlider_GetIsReadOnly,        Funke_CircularSlider_SetIsReadOnly)
	WIDGET_FLOAT_BINDING(CircularSliderComponent, StartAngleDegrees, Funke_CircularSlider_GetStartAngleDegrees, Funke_CircularSlider_SetStartAngleDegrees)
	WIDGET_FLOAT_BINDING_CLAMPED(CircularSliderComponent, SweepDegrees, 1.0f, 360.0f, Funke_CircularSlider_GetSweepDegrees, Funke_CircularSlider_SetSweepDegrees)
	WIDGET_BOOL_BINDING (CircularSliderComponent, Clockwise,         Funke_CircularSlider_GetClockwise,         Funke_CircularSlider_SetClockwise)
	WIDGET_FLOAT_BINDING_CLAMPED(CircularSliderComponent, RingThickness, 0.5f, 1024.0f, Funke_CircularSlider_GetRingThickness, Funke_CircularSlider_SetRingThickness)
	WIDGET_INT_BINDING_CLAMPED(CircularSliderComponent, RingSegments, 8, 256, Funke_CircularSlider_GetRingSegments, Funke_CircularSlider_SetRingSegments)
	WIDGET_COLOR_BINDING(CircularSliderComponent, BackgroundColor,   Funke_CircularSlider_GetBackgroundColor,   Funke_CircularSlider_SetBackgroundColor)
	WIDGET_COLOR_BINDING(CircularSliderComponent, FillColor,         Funke_CircularSlider_GetFillColor,         Funke_CircularSlider_SetFillColor)
	WIDGET_ENTITYREF_BINDING(CircularSliderComponent, HandleEntity,  Funke_CircularSlider_GetHandleEntity,      Funke_CircularSlider_SetHandleEntity)

	static int  Funke_CircularSlider_GetValueChangedThisFrame(uint64_t entityID) {
		GET_COMPONENT(CircularSliderComponent, entityID, 0);
		return comp.ValueChangedThisFrame ? 1 : 0;
	}
	static void Funke_CircularSlider_MarkValueObserved(uint64_t entityID) {
		GET_COMPONENT(CircularSliderComponent, entityID, );
		comp.LastObservedValue = comp.Value;
		comp.ValueObserved = true;
	}

	WIDGET_COLOR_BINDING(CircularSliderComponent, NormalColor,   Funke_CircularSlider_GetNormalColor,   Funke_CircularSlider_SetNormalColor)
	WIDGET_COLOR_BINDING(CircularSliderComponent, HoveredColor,  Funke_CircularSlider_GetHoveredColor,  Funke_CircularSlider_SetHoveredColor)
	WIDGET_COLOR_BINDING(CircularSliderComponent, PressedColor,  Funke_CircularSlider_GetPressedColor,  Funke_CircularSlider_SetPressedColor)
	WIDGET_COLOR_BINDING(CircularSliderComponent, DisabledColor, Funke_CircularSlider_GetDisabledColor, Funke_CircularSlider_SetDisabledColor)
	WIDGET_COLOR_BINDING(CircularSliderComponent, FocusedColor,  Funke_CircularSlider_GetFocusedColor,  Funke_CircularSlider_SetFocusedColor)
	WIDGET_TRANSITIONMODE_BINDING(CircularSliderComponent, Funke_CircularSlider_GetTransitionMode, Funke_CircularSlider_SetTransitionMode)
	WIDGET_SPRITE_BINDING(CircularSliderComponent, NormalSprite,   Funke_CircularSlider_GetNormalSprite,   Funke_CircularSlider_SetNormalSprite)
	WIDGET_SPRITE_BINDING(CircularSliderComponent, HoveredSprite,  Funke_CircularSlider_GetHoveredSprite,  Funke_CircularSlider_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(CircularSliderComponent, PressedSprite,  Funke_CircularSlider_GetPressedSprite,  Funke_CircularSlider_SetPressedSprite)
	WIDGET_SPRITE_BINDING(CircularSliderComponent, DisabledSprite, Funke_CircularSlider_GetDisabledSprite, Funke_CircularSlider_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(CircularSliderComponent, FocusedSprite,  Funke_CircularSlider_GetFocusedSprite,  Funke_CircularSlider_SetFocusedSprite)

	// ── HorizontalLayoutGroup ───────────────────────────────────────────
	WIDGET_FLOAT_BINDING(HorizontalLayoutGroupComponent, PaddingLeft,             Funke_HorizontalLayoutGroup_GetPaddingLeft,             Funke_HorizontalLayoutGroup_SetPaddingLeft)
	WIDGET_FLOAT_BINDING(HorizontalLayoutGroupComponent, PaddingRight,            Funke_HorizontalLayoutGroup_GetPaddingRight,            Funke_HorizontalLayoutGroup_SetPaddingRight)
	WIDGET_FLOAT_BINDING(HorizontalLayoutGroupComponent, PaddingTop,              Funke_HorizontalLayoutGroup_GetPaddingTop,              Funke_HorizontalLayoutGroup_SetPaddingTop)
	WIDGET_FLOAT_BINDING(HorizontalLayoutGroupComponent, PaddingBottom,           Funke_HorizontalLayoutGroup_GetPaddingBottom,           Funke_HorizontalLayoutGroup_SetPaddingBottom)
	WIDGET_FLOAT_BINDING(HorizontalLayoutGroupComponent, Spacing,                 Funke_HorizontalLayoutGroup_GetSpacing,                 Funke_HorizontalLayoutGroup_SetSpacing)
	WIDGET_ENUM_BINDING (HorizontalLayoutGroupComponent, ChildAlignment, UIAlignment, Funke_HorizontalLayoutGroup_GetChildAlignment, Funke_HorizontalLayoutGroup_SetChildAlignment)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, ReverseArrangement,      Funke_HorizontalLayoutGroup_GetReverseArrangement,      Funke_HorizontalLayoutGroup_SetReverseArrangement)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, ControlChildWidth,       Funke_HorizontalLayoutGroup_GetControlChildWidth,       Funke_HorizontalLayoutGroup_SetControlChildWidth)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, ControlChildHeight,      Funke_HorizontalLayoutGroup_GetControlChildHeight,      Funke_HorizontalLayoutGroup_SetControlChildHeight)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, UseChildScaleWidth,      Funke_HorizontalLayoutGroup_GetUseChildScaleWidth,      Funke_HorizontalLayoutGroup_SetUseChildScaleWidth)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, UseChildScaleHeight,     Funke_HorizontalLayoutGroup_GetUseChildScaleHeight,     Funke_HorizontalLayoutGroup_SetUseChildScaleHeight)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, ChildForceExpandWidth,   Funke_HorizontalLayoutGroup_GetChildForceExpandWidth,   Funke_HorizontalLayoutGroup_SetChildForceExpandWidth)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, ChildForceExpandHeight,  Funke_HorizontalLayoutGroup_GetChildForceExpandHeight,  Funke_HorizontalLayoutGroup_SetChildForceExpandHeight)

	// ── VerticalLayoutGroup ─────────────────────────────────────────────
	WIDGET_FLOAT_BINDING(VerticalLayoutGroupComponent, PaddingLeft,             Funke_VerticalLayoutGroup_GetPaddingLeft,             Funke_VerticalLayoutGroup_SetPaddingLeft)
	WIDGET_FLOAT_BINDING(VerticalLayoutGroupComponent, PaddingRight,            Funke_VerticalLayoutGroup_GetPaddingRight,            Funke_VerticalLayoutGroup_SetPaddingRight)
	WIDGET_FLOAT_BINDING(VerticalLayoutGroupComponent, PaddingTop,              Funke_VerticalLayoutGroup_GetPaddingTop,              Funke_VerticalLayoutGroup_SetPaddingTop)
	WIDGET_FLOAT_BINDING(VerticalLayoutGroupComponent, PaddingBottom,           Funke_VerticalLayoutGroup_GetPaddingBottom,           Funke_VerticalLayoutGroup_SetPaddingBottom)
	WIDGET_FLOAT_BINDING(VerticalLayoutGroupComponent, Spacing,                 Funke_VerticalLayoutGroup_GetSpacing,                 Funke_VerticalLayoutGroup_SetSpacing)
	WIDGET_ENUM_BINDING (VerticalLayoutGroupComponent, ChildAlignment, UIAlignment, Funke_VerticalLayoutGroup_GetChildAlignment, Funke_VerticalLayoutGroup_SetChildAlignment)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, ReverseArrangement,      Funke_VerticalLayoutGroup_GetReverseArrangement,      Funke_VerticalLayoutGroup_SetReverseArrangement)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, ControlChildWidth,       Funke_VerticalLayoutGroup_GetControlChildWidth,       Funke_VerticalLayoutGroup_SetControlChildWidth)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, ControlChildHeight,      Funke_VerticalLayoutGroup_GetControlChildHeight,      Funke_VerticalLayoutGroup_SetControlChildHeight)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, UseChildScaleWidth,      Funke_VerticalLayoutGroup_GetUseChildScaleWidth,      Funke_VerticalLayoutGroup_SetUseChildScaleWidth)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, UseChildScaleHeight,     Funke_VerticalLayoutGroup_GetUseChildScaleHeight,     Funke_VerticalLayoutGroup_SetUseChildScaleHeight)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, ChildForceExpandWidth,   Funke_VerticalLayoutGroup_GetChildForceExpandWidth,   Funke_VerticalLayoutGroup_SetChildForceExpandWidth)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, ChildForceExpandHeight,  Funke_VerticalLayoutGroup_GetChildForceExpandHeight,  Funke_VerticalLayoutGroup_SetChildForceExpandHeight)

	// ── GridLayoutGroup ─────────────────────────────────────────────────
	WIDGET_FLOAT_BINDING(GridLayoutGroupComponent, PaddingLeft,     Funke_GridLayoutGroup_GetPaddingLeft,     Funke_GridLayoutGroup_SetPaddingLeft)
	WIDGET_FLOAT_BINDING(GridLayoutGroupComponent, PaddingRight,    Funke_GridLayoutGroup_GetPaddingRight,    Funke_GridLayoutGroup_SetPaddingRight)
	WIDGET_FLOAT_BINDING(GridLayoutGroupComponent, PaddingTop,      Funke_GridLayoutGroup_GetPaddingTop,      Funke_GridLayoutGroup_SetPaddingTop)
	WIDGET_FLOAT_BINDING(GridLayoutGroupComponent, PaddingBottom,   Funke_GridLayoutGroup_GetPaddingBottom,   Funke_GridLayoutGroup_SetPaddingBottom)
	WIDGET_VEC2_BINDING (GridLayoutGroupComponent, CellSize,        Funke_GridLayoutGroup_GetCellSize,        Funke_GridLayoutGroup_SetCellSize)
	WIDGET_VEC2_BINDING (GridLayoutGroupComponent, Spacing,         Funke_GridLayoutGroup_GetSpacing,         Funke_GridLayoutGroup_SetSpacing)
	WIDGET_ENUM_BINDING (GridLayoutGroupComponent, StartCorner,    GridLayoutStartCorner, Funke_GridLayoutGroup_GetStartCorner,    Funke_GridLayoutGroup_SetStartCorner)
	WIDGET_ENUM_BINDING (GridLayoutGroupComponent, StartAxis,      GridLayoutStartAxis,   Funke_GridLayoutGroup_GetStartAxis,      Funke_GridLayoutGroup_SetStartAxis)
	WIDGET_ENUM_BINDING (GridLayoutGroupComponent, ChildAlignment, UIAlignment,           Funke_GridLayoutGroup_GetChildAlignment, Funke_GridLayoutGroup_SetChildAlignment)
	WIDGET_ENUM_BINDING (GridLayoutGroupComponent, Constraint,     GridLayoutConstraint,  Funke_GridLayoutGroup_GetConstraint,     Funke_GridLayoutGroup_SetConstraint)
	WIDGET_INT_BINDING  (GridLayoutGroupComponent, ConstraintCount, Funke_GridLayoutGroup_GetConstraintCount, Funke_GridLayoutGroup_SetConstraintCount)
	WIDGET_BOOL_BINDING (GridLayoutGroupComponent, Reverse,         Funke_GridLayoutGroup_GetReverse,         Funke_GridLayoutGroup_SetReverse)

	// ── ContentSizeFitter ───────────────────────────────────────────────
	WIDGET_BOOL_BINDING (ContentSizeFitterComponent, HorizontalFit,  Funke_ContentSizeFitter_GetHorizontalFit,  Funke_ContentSizeFitter_SetHorizontalFit)
	WIDGET_BOOL_BINDING (ContentSizeFitterComponent, VerticalFit,    Funke_ContentSizeFitter_GetVerticalFit,    Funke_ContentSizeFitter_SetVerticalFit)
	WIDGET_FLOAT_BINDING(ContentSizeFitterComponent, PaddingLeft,    Funke_ContentSizeFitter_GetPaddingLeft,    Funke_ContentSizeFitter_SetPaddingLeft)
	WIDGET_FLOAT_BINDING(ContentSizeFitterComponent, PaddingRight,   Funke_ContentSizeFitter_GetPaddingRight,   Funke_ContentSizeFitter_SetPaddingRight)
	WIDGET_FLOAT_BINDING(ContentSizeFitterComponent, PaddingTop,     Funke_ContentSizeFitter_GetPaddingTop,     Funke_ContentSizeFitter_SetPaddingTop)
	WIDGET_FLOAT_BINDING(ContentSizeFitterComponent, PaddingBottom,  Funke_ContentSizeFitter_GetPaddingBottom,  Funke_ContentSizeFitter_SetPaddingBottom)

	// ── WidthConstraint ─────────────────────────────────────────────────
	WIDGET_FLOAT_BINDING(WidthConstraintComponent, MinWidth, Funke_WidthConstraint_GetMinWidth, Funke_WidthConstraint_SetMinWidth)
	WIDGET_FLOAT_BINDING(WidthConstraintComponent, MaxWidth, Funke_WidthConstraint_GetMaxWidth, Funke_WidthConstraint_SetMaxWidth)

	#undef WIDGET_FLOAT_BINDING
	#undef WIDGET_INT_BINDING
	#undef WIDGET_BOOL_BINDING
	#undef WIDGET_ENUM_BINDING
	#undef WIDGET_VEC2_BINDING
	#undef WIDGET_COLOR_BINDING
	#undef WIDGET_SPRITE_BINDING
	#undef WIDGET_TRANSITIONMODE_BINDING
	#undef WIDGET_ENTITYREF_BINDING

	#undef GET_COMPONENT

	// ── Registration ────────────────────────────────────────────────────

	void ScriptBindings::PopulateNativeBindings(NativeBindings& b)
	{
		PopulateNonComponentBindings(b);
		PopulatePostProcessingBindings(b);
		ScriptEditorGui::PopulateBindings(b);
		// ECB slots — appended at the end of NativeBindings for binary
		// compat. Wired here so the managed bindings copy picks them up
		// in the same initialization step as the rest of the surface.
		PopulateEcbBindings(b);
		// JobSystem slots — same append-for-binary-compat pattern. Wires
		// the managed Job.Schedule path into the native work-stealing
		// pool so C# work shares cores cleanly with native systems.
		PopulateJobsBindings(b);
		PopulateMaterialBindings(b);
		PopulateAnimationBindings(b);
		PopulateLocalizationBindings(b);
		PopulateModuleBindings(b);

		b.Entity_Clone = FNK_GUARDED_BINDING(Funke_Entity_Clone);
		b.Entity_InstantiatePrefab = FNK_GUARDED_BINDING(Funke_Entity_InstantiatePrefab);
		b.Entity_CreateScriptInstances = FNK_GUARDED_BINDING(Funke_Entity_CreateScriptInstances);
		b.Entity_GetOrigin = FNK_GUARDED_BINDING(Funke_Entity_GetOrigin);
		b.Entity_GetRuntimeID = FNK_GUARDED_BINDING(Funke_Entity_GetRuntimeID);
		b.Entity_GetSceneGUID = FNK_GUARDED_BINDING(Funke_Entity_GetSceneGUID);
		b.Entity_GetPrefabGUID = FNK_GUARDED_BINDING(Funke_Entity_GetPrefabGUID);
		b.Entity_HasComponent = FNK_GUARDED_BINDING(Funke_Entity_HasComponent);
		b.Entity_AddComponent = FNK_GUARDED_BINDING(Funke_Entity_AddComponent);
		b.Entity_RemoveComponent = FNK_GUARDED_BINDING(Funke_Entity_RemoveComponent);
		b.Entity_AddScript = FNK_GUARDED_BINDING(Funke_Entity_AddScript);
		b.Entity_HasScript = FNK_GUARDED_BINDING(Funke_Entity_HasScript);
		b.Entity_RemoveScript = FNK_GUARDED_BINDING(Funke_Entity_RemoveScript);
		// Unbuffered string slots removed: they returned a thread-local pointer that raced across calls. Use *Buffer variants only.
		b.Entity_GetManagedComponentFieldsBuffer = FNK_GUARDED_BINDING(Funke_Entity_GetManagedComponentFieldsBuffer);
		b.Entity_GetIsStatic = FNK_GUARDED_BINDING(Funke_Entity_GetIsStatic);
		b.Entity_SetIsStatic = FNK_GUARDED_BINDING(Funke_Entity_SetIsStatic);
		b.Entity_GetIsEnabled            = FNK_GUARDED_BINDING(Funke_Entity_GetIsEnabled);
		b.Entity_GetIsEnabledInHierarchy = FNK_GUARDED_BINDING(Funke_Entity_GetIsEnabledInHierarchy);
		b.Entity_SetIsEnabled            = FNK_GUARDED_BINDING(Funke_Entity_SetIsEnabled);

		b.NameComponent_GetNameBuffer = FNK_GUARDED_BINDING(Funke_NameComponent_GetNameBuffer);
		b.NameComponent_SetName = FNK_GUARDED_BINDING(Funke_NameComponent_SetName);

		b.Entity_GetParent = FNK_GUARDED_BINDING(Funke_Entity_GetParent);
		b.Entity_SetParent = FNK_GUARDED_BINDING(Funke_Entity_SetParent);
		b.Entity_GetChildCount = FNK_GUARDED_BINDING(Funke_Entity_GetChildCount);
		b.Entity_GetChildAt = FNK_GUARDED_BINDING(Funke_Entity_GetChildAt);
		b.Entity_GetChildren = FNK_GUARDED_BINDING(Funke_Entity_GetChildren);


		b.TextRenderer_GetTextBuffer = FNK_GUARDED_BINDING(Funke_TextRenderer_GetTextBuffer);
		b.TextRenderer_SetText = FNK_GUARDED_BINDING(Funke_TextRenderer_SetText);
		b.TextRenderer_GetFont = FNK_GUARDED_BINDING(Funke_TextRenderer_GetFont);
		b.TextRenderer_SetFont = FNK_GUARDED_BINDING(Funke_TextRenderer_SetFont);
		b.TextRenderer_GetFontSize = FNK_GUARDED_BINDING(Funke_TextRenderer_GetFontSize);
		b.TextRenderer_SetFontSize = FNK_GUARDED_BINDING(Funke_TextRenderer_SetFontSize);
		b.TextRenderer_GetColor = FNK_GUARDED_BINDING(Funke_TextRenderer_GetColor);
		b.TextRenderer_SetColor = FNK_GUARDED_BINDING(Funke_TextRenderer_SetColor);
		b.TextRenderer_GetLetterSpacing = FNK_GUARDED_BINDING(Funke_TextRenderer_GetLetterSpacing);
		b.TextRenderer_SetLetterSpacing = FNK_GUARDED_BINDING(Funke_TextRenderer_SetLetterSpacing);
		b.TextRenderer_GetLineSpacing = FNK_GUARDED_BINDING(Funke_TextRenderer_GetLineSpacing);
		b.TextRenderer_SetLineSpacing = FNK_GUARDED_BINDING(Funke_TextRenderer_SetLineSpacing);
		b.TextRenderer_GetEffects = FNK_GUARDED_BINDING(Funke_TextRenderer_GetEffects);
		b.TextRenderer_SetEffects = FNK_GUARDED_BINDING(Funke_TextRenderer_SetEffects);
		b.TextRenderer_GetHAlign = FNK_GUARDED_BINDING(Funke_TextRenderer_GetHAlign);
		b.TextRenderer_SetHAlign = FNK_GUARDED_BINDING(Funke_TextRenderer_SetHAlign);
		b.TextRenderer_GetVAlign = FNK_GUARDED_BINDING(Funke_TextRenderer_GetVAlign);
		b.TextRenderer_SetVAlign = FNK_GUARDED_BINDING(Funke_TextRenderer_SetVAlign);
		b.TextRenderer_GetWrapMode = FNK_GUARDED_BINDING(Funke_TextRenderer_GetWrapMode);
		b.TextRenderer_SetWrapMode = FNK_GUARDED_BINDING(Funke_TextRenderer_SetWrapMode);
		// WrapWidth get/set slots removed; the C# struct doesn't carry
		// them anymore.
		b.TextRenderer_GetSortingOrder = FNK_GUARDED_BINDING(Funke_TextRenderer_GetSortingOrder);
		b.TextRenderer_SetSortingOrder = FNK_GUARDED_BINDING(Funke_TextRenderer_SetSortingOrder);
		b.TextRenderer_GetSortingLayer = FNK_GUARDED_BINDING(Funke_TextRenderer_GetSortingLayer);
		b.TextRenderer_SetSortingLayer = FNK_GUARDED_BINDING(Funke_TextRenderer_SetSortingLayer);






		b.AudioSource_Play = FNK_GUARDED_BINDING(Funke_AudioSource_Play);
		b.AudioSource_Pause = FNK_GUARDED_BINDING(Funke_AudioSource_Pause);
		b.AudioSource_Stop = FNK_GUARDED_BINDING(Funke_AudioSource_Stop);
		b.AudioSource_Resume = FNK_GUARDED_BINDING(Funke_AudioSource_Resume);
		b.AudioSource_GetVolume = FNK_GUARDED_BINDING(Funke_AudioSource_GetVolume);
		b.AudioSource_SetVolume = FNK_GUARDED_BINDING(Funke_AudioSource_SetVolume);
		b.AudioSource_GetPitch = FNK_GUARDED_BINDING(Funke_AudioSource_GetPitch);
		b.AudioSource_SetPitch = FNK_GUARDED_BINDING(Funke_AudioSource_SetPitch);
		b.AudioSource_GetLoop = FNK_GUARDED_BINDING(Funke_AudioSource_GetLoop);
		b.AudioSource_SetLoop = FNK_GUARDED_BINDING(Funke_AudioSource_SetLoop);
		b.AudioSource_IsPlaying = FNK_GUARDED_BINDING(Funke_AudioSource_IsPlaying);
		b.AudioSource_IsPaused = FNK_GUARDED_BINDING(Funke_AudioSource_IsPaused);
		b.AudioSource_GetSpatial = FNK_GUARDED_BINDING(Funke_AudioSource_GetSpatial);
		b.AudioSource_SetSpatial = FNK_GUARDED_BINDING(Funke_AudioSource_SetSpatial);
		b.AudioSource_GetAudio = FNK_GUARDED_BINDING(Funke_AudioSource_GetAudio);
		b.AudioSource_SetAudio = FNK_GUARDED_BINDING(Funke_AudioSource_SetAudio);


		b.Scene_GetActiveSceneNameBuffer = FNK_GUARDED_BINDING(Funke_Scene_GetActiveSceneNameBuffer);
		b.Scene_GetEntityCount = FNK_GUARDED_BINDING(Funke_Scene_GetEntityCount);
		b.Scene_GetEntityCountByName = FNK_GUARDED_BINDING(Funke_Scene_GetEntityCountByName);
		b.Scene_LoadAdditive = FNK_GUARDED_BINDING(Funke_Scene_LoadAdditive);
		b.Scene_Load = FNK_GUARDED_BINDING(Funke_Scene_Load);
		b.Scene_Unload = FNK_GUARDED_BINDING(Funke_Scene_Unload);
		b.Scene_SetActive = FNK_GUARDED_BINDING(Funke_Scene_SetActive);
		b.Scene_Reload = FNK_GUARDED_BINDING(Funke_Scene_Reload);
		b.Scene_SetSceneSystemEnabled = FNK_GUARDED_BINDING(Funke_Scene_SetSceneSystemEnabled);
		b.Scene_IsSceneSystemEnabled = FNK_GUARDED_BINDING(Funke_Scene_IsSceneSystemEnabled);
		b.Scene_SetGlobalSystemEnabled = FNK_GUARDED_BINDING(Funke_Scene_SetGlobalSystemEnabled);
		b.Scene_DoesSceneExist = FNK_GUARDED_BINDING(Funke_Scene_DoesSceneExist);
		b.Scene_GetLoadedCount = FNK_GUARDED_BINDING(Funke_Scene_GetLoadedCount);
		b.Scene_GetLoadedSceneNameAtBuffer = FNK_GUARDED_BINDING(Funke_Scene_GetLoadedSceneNameAtBuffer);
		b.Scene_GetEntityNameByUUIDBuffer = FNK_GUARDED_BINDING(Funke_Scene_GetEntityNameByUUIDBuffer);
		b.Scene_QueryEntities = FNK_GUARDED_BINDING(Funke_Scene_QueryEntities);
		b.Scene_QueryEntitiesFiltered = FNK_GUARDED_BINDING(Funke_Scene_QueryEntitiesFiltered);
		b.Scene_QueryEntitiesInScene = FNK_GUARDED_BINDING(Funke_Scene_QueryEntitiesInScene);
		b.Scene_QueryEntitiesFilteredInScene = FNK_GUARDED_BINDING(Funke_Scene_QueryEntitiesFilteredInScene);

		b.Asset_IsValid = FNK_GUARDED_BINDING(Funke_Asset_IsValid);
		b.Asset_GetOrCreateUUIDFromPath = FNK_GUARDED_BINDING(Funke_Asset_GetOrCreateUUIDFromPath);
		b.Asset_GetPathBuffer = FNK_GUARDED_BINDING(Funke_Asset_GetPathBuffer);
		b.Asset_GetDisplayNameBuffer = FNK_GUARDED_BINDING(Funke_Asset_GetDisplayNameBuffer);
		b.Asset_GetKind = FNK_GUARDED_BINDING(Funke_Asset_GetKind);
		b.Asset_FindAllBuffer = FNK_GUARDED_BINDING(Funke_Asset_FindAllBuffer);
		b.DataAsset_EnsureLoaded = FNK_GUARDED_BINDING(Funke_DataAsset_EnsureLoaded);
		b.Texture_LoadAsset = FNK_GUARDED_BINDING(Funke_Texture_LoadAsset);
		b.Texture_GetWidth = FNK_GUARDED_BINDING(Funke_Texture_GetWidth);
		b.Texture_GetHeight = FNK_GUARDED_BINDING(Funke_Texture_GetHeight);
		b.Texture_GetDefaultAssetUUID = FNK_GUARDED_BINDING(Funke_Texture_GetDefaultAssetUUID);
		b.Texture_CreateRuntime = FNK_GUARDED_BINDING(Funke_Texture_CreateRuntime);
		b.Texture_UpdateRuntime = FNK_GUARDED_BINDING(Funke_Texture_UpdateRuntime);
		b.Texture_DestroyRuntime = FNK_GUARDED_BINDING(Funke_Texture_DestroyRuntime);
		b.Texture_IsRuntime = FNK_GUARDED_BINDING(Funke_Texture_IsRuntime);
		b.Audio_LoadAsset = FNK_GUARDED_BINDING(Funke_Audio_LoadAsset);
		b.Audio_PlayOneShotAsset = FNK_GUARDED_BINDING(Funke_Audio_PlayOneShotAsset);
		b.Audio_PlayOneShotAt = FNK_GUARDED_BINDING(Funke_Audio_PlayOneShotAt);
		b.Font_LoadAsset = FNK_GUARDED_BINDING(Funke_Font_LoadAsset);


		b.Gizmo_DrawLine = FNK_GUARDED_BINDING(Funke_Gizmo_DrawLine);
		b.Gizmo_DrawWireSquare = FNK_GUARDED_BINDING(Funke_Gizmo_DrawWireSquare);
		b.Gizmo_DrawWireCircle = FNK_GUARDED_BINDING(Funke_Gizmo_DrawWireCircle);
		b.Gizmo_DrawSquare = FNK_GUARDED_BINDING(Funke_Gizmo_DrawSquare);
		b.Gizmo_DrawCircle = FNK_GUARDED_BINDING(Funke_Gizmo_DrawCircle);
		b.Gizmo_DrawText = FNK_GUARDED_BINDING(Funke_Gizmo_DrawText);
		b.Gizmo_SetColor = FNK_GUARDED_BINDING(Funke_Gizmo_SetColor);
		b.Gizmo_GetColor = FNK_GUARDED_BINDING(Funke_Gizmo_GetColor);
		b.Gizmo_GetLineWidth = FNK_GUARDED_BINDING(Funke_Gizmo_GetLineWidth);
		b.Gizmo_SetLineWidth = FNK_GUARDED_BINDING(Funke_Gizmo_SetLineWidth);
		b.Gizmo_SetMaxVertices = FNK_GUARDED_BINDING(Funke_Gizmo_SetMaxVertices);
		b.Gizmo_GetMaxVertices = FNK_GUARDED_BINDING(Funke_Gizmo_GetMaxVertices);
		b.Gizmo_GetRegisteredVertices = FNK_GUARDED_BINDING(Funke_Gizmo_GetRegisteredVertices);
		b.Gizmo_GetEnabled = FNK_GUARDED_BINDING(Funke_Gizmo_GetEnabled);
		b.Gizmo_SetEnabled = FNK_GUARDED_BINDING(Funke_Gizmo_SetEnabled);




		b.UI_GetHoveredEntity = FNK_GUARDED_BINDING(Funke_UI_GetHoveredEntity);
		b.RectTransform_GetScreenPosition = FNK_GUARDED_BINDING(Funke_RectTransform_GetScreenPosition);
		b.RectTransform_SetScreenPosition = FNK_GUARDED_BINDING(Funke_RectTransform_SetScreenPosition);

		// ── UI ──────────────────────────────────────────────────────
		b.RectTransform_GetAnchorMin        = FNK_GUARDED_BINDING(Funke_RectTransform_GetAnchorMin);
		b.RectTransform_SetAnchorMin        = FNK_GUARDED_BINDING(Funke_RectTransform_SetAnchorMin);
		b.RectTransform_GetAnchorMax        = FNK_GUARDED_BINDING(Funke_RectTransform_GetAnchorMax);
		b.RectTransform_SetAnchorMax        = FNK_GUARDED_BINDING(Funke_RectTransform_SetAnchorMax);
		b.RectTransform_GetPivot            = FNK_GUARDED_BINDING(Funke_RectTransform_GetPivot);
		b.RectTransform_SetPivot            = FNK_GUARDED_BINDING(Funke_RectTransform_SetPivot);
		b.RectTransform_GetAnchoredPosition = FNK_GUARDED_BINDING(Funke_RectTransform_GetAnchoredPosition);
		b.RectTransform_SetAnchoredPosition = FNK_GUARDED_BINDING(Funke_RectTransform_SetAnchoredPosition);
		b.RectTransform_GetSizeDelta        = FNK_GUARDED_BINDING(Funke_RectTransform_GetSizeDelta);
		b.RectTransform_SetSizeDelta        = FNK_GUARDED_BINDING(Funke_RectTransform_SetSizeDelta);
		b.RectTransform_GetRotation         = FNK_GUARDED_BINDING(Funke_RectTransform_GetRotation);
		b.RectTransform_SetRotation         = FNK_GUARDED_BINDING(Funke_RectTransform_SetRotation);
		b.RectTransform_GetScale            = FNK_GUARDED_BINDING(Funke_RectTransform_GetScale);
		b.RectTransform_SetScale            = FNK_GUARDED_BINDING(Funke_RectTransform_SetScale);
		b.RectTransform_GetLocalRotation    = FNK_GUARDED_BINDING(Funke_RectTransform_GetLocalRotation);
		b.RectTransform_SetLocalRotation    = FNK_GUARDED_BINDING(Funke_RectTransform_SetLocalRotation);
		b.RectTransform_GetLocalScale       = FNK_GUARDED_BINDING(Funke_RectTransform_GetLocalScale);
		b.RectTransform_SetLocalScale       = FNK_GUARDED_BINDING(Funke_RectTransform_SetLocalScale);
		b.RectTransform_GetResolvedSize     = FNK_GUARDED_BINDING(Funke_RectTransform_GetResolvedSize);

		b.Image_GetColor        = FNK_GUARDED_BINDING(Funke_Image_GetColor);
		b.Image_SetColor        = FNK_GUARDED_BINDING(Funke_Image_SetColor);
		b.Image_GetTexture      = FNK_GUARDED_BINDING(Funke_Image_GetTexture);
		b.Image_SetTexture      = FNK_GUARDED_BINDING(Funke_Image_SetTexture);
		b.Image_GetSpriteNameBuffer = FNK_GUARDED_BINDING(Funke_Image_GetSpriteNameBuffer);
		b.Image_SetSpriteName       = FNK_GUARDED_BINDING(Funke_Image_SetSpriteName);
		b.Image_GetSortingOrder = FNK_GUARDED_BINDING(Funke_Image_GetSortingOrder);
		b.Image_SetSortingOrder = FNK_GUARDED_BINDING(Funke_Image_SetSortingOrder);
		b.Image_GetSortingLayer = FNK_GUARDED_BINDING(Funke_Image_GetSortingLayer);
		b.Image_SetSortingLayer = FNK_GUARDED_BINDING(Funke_Image_SetSortingLayer);
		b.Image_GetSlicing      = FNK_GUARDED_BINDING(Funke_Image_GetSlicing);
		b.Image_SetSlicing      = FNK_GUARDED_BINDING(Funke_Image_SetSlicing);
		b.Image_GetMaterial     = FNK_GUARDED_BINDING(Funke_Image_GetMaterial);
		b.Image_SetMaterial     = FNK_GUARDED_BINDING(Funke_Image_SetMaterial);

		b.Interactable_GetInteractable = FNK_GUARDED_BINDING(Funke_Interactable_GetInteractable);
		b.Interactable_SetInteractable = FNK_GUARDED_BINDING(Funke_Interactable_SetInteractable);
		b.Interactable_GetIsHovered    = FNK_GUARDED_BINDING(Funke_Interactable_GetIsHovered);
		b.Interactable_GetIsClicked    = FNK_GUARDED_BINDING(Funke_Interactable_GetIsClicked);
		b.Interactable_GetIsPressed    = FNK_GUARDED_BINDING(Funke_Interactable_GetIsPressed);
		b.Interactable_GetIsMouseDown  = FNK_GUARDED_BINDING(Funke_Interactable_GetIsMouseDown);
		b.Interactable_GetIsMouseUp    = FNK_GUARDED_BINDING(Funke_Interactable_GetIsMouseUp);
		b.Interactable_GetFocusable    = FNK_GUARDED_BINDING(Funke_Interactable_GetFocusable);
		b.Interactable_SetFocusable    = FNK_GUARDED_BINDING(Funke_Interactable_SetFocusable);
		b.Interactable_GetIsFocused    = FNK_GUARDED_BINDING(Funke_Interactable_GetIsFocused);
		b.Interactable_SetIsFocused    = FNK_GUARDED_BINDING(Funke_Interactable_SetIsFocused);

		b.Button_GetNormalColor   = FNK_GUARDED_BINDING(Funke_Button_GetNormalColor);
		b.Button_SetNormalColor   = FNK_GUARDED_BINDING(Funke_Button_SetNormalColor);
		b.Button_GetHoveredColor  = FNK_GUARDED_BINDING(Funke_Button_GetHoveredColor);
		b.Button_SetHoveredColor  = FNK_GUARDED_BINDING(Funke_Button_SetHoveredColor);
		b.Button_GetPressedColor  = FNK_GUARDED_BINDING(Funke_Button_GetPressedColor);
		b.Button_SetPressedColor  = FNK_GUARDED_BINDING(Funke_Button_SetPressedColor);
		b.Button_GetDisabledColor = FNK_GUARDED_BINDING(Funke_Button_GetDisabledColor);
		b.Button_SetDisabledColor = FNK_GUARDED_BINDING(Funke_Button_SetDisabledColor);

		b.Slider_GetValue                 = FNK_GUARDED_BINDING(Funke_Slider_GetValue);
		b.Slider_SetValue                 = FNK_GUARDED_BINDING(Funke_Slider_SetValue);
		b.Slider_GetMinValue              = FNK_GUARDED_BINDING(Funke_Slider_GetMinValue);
		b.Slider_SetMinValue              = FNK_GUARDED_BINDING(Funke_Slider_SetMinValue);
		b.Slider_GetMaxValue              = FNK_GUARDED_BINDING(Funke_Slider_GetMaxValue);
		b.Slider_SetMaxValue              = FNK_GUARDED_BINDING(Funke_Slider_SetMaxValue);
		b.Slider_GetWholeNumbers          = FNK_GUARDED_BINDING(Funke_Slider_GetWholeNumbers);
		b.Slider_SetWholeNumbers          = FNK_GUARDED_BINDING(Funke_Slider_SetWholeNumbers);
		b.Slider_GetValueChangedThisFrame = FNK_GUARDED_BINDING(Funke_Slider_GetValueChangedThisFrame);
		b.Slider_MarkValueObserved        = FNK_GUARDED_BINDING(Funke_Slider_MarkValueObserved);
		b.Slider_GetNormalColor   = FNK_GUARDED_BINDING(Funke_Slider_GetNormalColor);
		b.Slider_SetNormalColor   = FNK_GUARDED_BINDING(Funke_Slider_SetNormalColor);
		b.Slider_GetHoveredColor  = FNK_GUARDED_BINDING(Funke_Slider_GetHoveredColor);
		b.Slider_SetHoveredColor  = FNK_GUARDED_BINDING(Funke_Slider_SetHoveredColor);
		b.Slider_GetPressedColor  = FNK_GUARDED_BINDING(Funke_Slider_GetPressedColor);
		b.Slider_SetPressedColor  = FNK_GUARDED_BINDING(Funke_Slider_SetPressedColor);
		b.Slider_GetDisabledColor = FNK_GUARDED_BINDING(Funke_Slider_GetDisabledColor);
		b.Slider_SetDisabledColor = FNK_GUARDED_BINDING(Funke_Slider_SetDisabledColor);

		b.Toggle_GetIsOn                  = FNK_GUARDED_BINDING(Funke_Toggle_GetIsOn);
		b.Toggle_SetIsOn                  = FNK_GUARDED_BINDING(Funke_Toggle_SetIsOn);
		b.Toggle_GetValueChangedThisFrame = FNK_GUARDED_BINDING(Funke_Toggle_GetValueChangedThisFrame);
		b.Toggle_MarkIsOnObserved         = FNK_GUARDED_BINDING(Funke_Toggle_MarkIsOnObserved);
		b.Toggle_GetNormalColor   = FNK_GUARDED_BINDING(Funke_Toggle_GetNormalColor);
		b.Toggle_SetNormalColor   = FNK_GUARDED_BINDING(Funke_Toggle_SetNormalColor);
		b.Toggle_GetHoveredColor  = FNK_GUARDED_BINDING(Funke_Toggle_GetHoveredColor);
		b.Toggle_SetHoveredColor  = FNK_GUARDED_BINDING(Funke_Toggle_SetHoveredColor);
		b.Toggle_GetPressedColor  = FNK_GUARDED_BINDING(Funke_Toggle_GetPressedColor);
		b.Toggle_SetPressedColor  = FNK_GUARDED_BINDING(Funke_Toggle_SetPressedColor);
		b.Toggle_GetDisabledColor = FNK_GUARDED_BINDING(Funke_Toggle_GetDisabledColor);
		b.Toggle_SetDisabledColor = FNK_GUARDED_BINDING(Funke_Toggle_SetDisabledColor);

		b.InputField_GetTextBuffer            = FNK_GUARDED_BINDING(Funke_InputField_GetTextBuffer);
		b.InputField_SetText                  = FNK_GUARDED_BINDING(Funke_InputField_SetText);
		b.InputField_GetPlaceholderTextBuffer = FNK_GUARDED_BINDING(Funke_InputField_GetPlaceholderTextBuffer);
		b.InputField_SetPlaceholderText       = FNK_GUARDED_BINDING(Funke_InputField_SetPlaceholderText);
		b.InputField_GetIsFocused             = FNK_GUARDED_BINDING(Funke_InputField_GetIsFocused);
		b.InputField_SetIsFocused             = FNK_GUARDED_BINDING(Funke_InputField_SetIsFocused);
		b.InputField_GetSubmittedThisFrame    = FNK_GUARDED_BINDING(Funke_InputField_GetSubmittedThisFrame);
		b.InputField_GetCharacterLimit        = FNK_GUARDED_BINDING(Funke_InputField_GetCharacterLimit);
		b.InputField_SetCharacterLimit        = FNK_GUARDED_BINDING(Funke_InputField_SetCharacterLimit);
		b.InputField_GetMultiline             = FNK_GUARDED_BINDING(Funke_InputField_GetMultiline);
		b.InputField_SetMultiline             = FNK_GUARDED_BINDING(Funke_InputField_SetMultiline);
		b.InputField_GetVerticalScrollbarEntity = FNK_GUARDED_BINDING(Funke_InputField_GetVerticalScrollbarEntity);
		b.InputField_SetVerticalScrollbarEntity = FNK_GUARDED_BINDING(Funke_InputField_SetVerticalScrollbarEntity);
		b.InputField_GetNormalColor   = FNK_GUARDED_BINDING(Funke_InputField_GetNormalColor);
		b.InputField_SetNormalColor   = FNK_GUARDED_BINDING(Funke_InputField_SetNormalColor);
		b.InputField_GetHoveredColor  = FNK_GUARDED_BINDING(Funke_InputField_GetHoveredColor);
		b.InputField_SetHoveredColor  = FNK_GUARDED_BINDING(Funke_InputField_SetHoveredColor);
		b.InputField_GetPressedColor  = FNK_GUARDED_BINDING(Funke_InputField_GetPressedColor);
		b.InputField_SetPressedColor  = FNK_GUARDED_BINDING(Funke_InputField_SetPressedColor);
		b.InputField_GetDisabledColor = FNK_GUARDED_BINDING(Funke_InputField_GetDisabledColor);
		b.InputField_SetDisabledColor = FNK_GUARDED_BINDING(Funke_InputField_SetDisabledColor);

		b.Dropdown_GetSelectedIndex             = FNK_GUARDED_BINDING(Funke_Dropdown_GetSelectedIndex);
		b.Dropdown_SetSelectedIndex             = FNK_GUARDED_BINDING(Funke_Dropdown_SetSelectedIndex);
		b.Dropdown_GetIsOpen                    = FNK_GUARDED_BINDING(Funke_Dropdown_GetIsOpen);
		b.Dropdown_SetIsOpen                    = FNK_GUARDED_BINDING(Funke_Dropdown_SetIsOpen);
		b.Dropdown_GetSelectionChangedThisFrame = FNK_GUARDED_BINDING(Funke_Dropdown_GetSelectionChangedThisFrame);
		b.Dropdown_MarkSelectedIndexObserved    = FNK_GUARDED_BINDING(Funke_Dropdown_MarkSelectedIndexObserved);
		b.Dropdown_GetOptionCount               = FNK_GUARDED_BINDING(Funke_Dropdown_GetOptionCount);
		b.Dropdown_GetOptionBuffer              = FNK_GUARDED_BINDING(Funke_Dropdown_GetOptionBuffer);
		b.Dropdown_SetOption                    = FNK_GUARDED_BINDING(Funke_Dropdown_SetOption);
		b.Dropdown_AddOption                    = FNK_GUARDED_BINDING(Funke_Dropdown_AddOption);
		b.Dropdown_RemoveOption                 = FNK_GUARDED_BINDING(Funke_Dropdown_RemoveOption);
		b.Dropdown_ClearOptions                 = FNK_GUARDED_BINDING(Funke_Dropdown_ClearOptions);
		b.Dropdown_GetNormalColor   = FNK_GUARDED_BINDING(Funke_Dropdown_GetNormalColor);
		b.Dropdown_SetNormalColor   = FNK_GUARDED_BINDING(Funke_Dropdown_SetNormalColor);
		b.Dropdown_GetHoveredColor  = FNK_GUARDED_BINDING(Funke_Dropdown_GetHoveredColor);
		b.Dropdown_SetHoveredColor  = FNK_GUARDED_BINDING(Funke_Dropdown_SetHoveredColor);
		b.Dropdown_GetPressedColor  = FNK_GUARDED_BINDING(Funke_Dropdown_GetPressedColor);
		b.Dropdown_SetPressedColor  = FNK_GUARDED_BINDING(Funke_Dropdown_SetPressedColor);
		b.Dropdown_GetDisabledColor = FNK_GUARDED_BINDING(Funke_Dropdown_GetDisabledColor);
		b.Dropdown_SetDisabledColor = FNK_GUARDED_BINDING(Funke_Dropdown_SetDisabledColor);

		b.Button_GetFocusedColor    = FNK_GUARDED_BINDING(Funke_Button_GetFocusedColor);
		b.Button_SetFocusedColor    = FNK_GUARDED_BINDING(Funke_Button_SetFocusedColor);
		b.Toggle_GetFocusedColor    = FNK_GUARDED_BINDING(Funke_Toggle_GetFocusedColor);
		b.Toggle_SetFocusedColor    = FNK_GUARDED_BINDING(Funke_Toggle_SetFocusedColor);
		b.Slider_GetFocusedColor    = FNK_GUARDED_BINDING(Funke_Slider_GetFocusedColor);
		b.Slider_SetFocusedColor    = FNK_GUARDED_BINDING(Funke_Slider_SetFocusedColor);
		b.InputField_GetFocusedColor= FNK_GUARDED_BINDING(Funke_InputField_GetFocusedColor);
		b.InputField_SetFocusedColor= FNK_GUARDED_BINDING(Funke_InputField_SetFocusedColor);
		b.Dropdown_GetFocusedColor  = FNK_GUARDED_BINDING(Funke_Dropdown_GetFocusedColor);
		b.Dropdown_SetFocusedColor  = FNK_GUARDED_BINDING(Funke_Dropdown_SetFocusedColor);

		b.Button_GetTransitionMode     = FNK_GUARDED_BINDING(Funke_Button_GetTransitionMode);
		b.Button_SetTransitionMode     = FNK_GUARDED_BINDING(Funke_Button_SetTransitionMode);
		b.Toggle_GetTransitionMode     = FNK_GUARDED_BINDING(Funke_Toggle_GetTransitionMode);
		b.Toggle_SetTransitionMode     = FNK_GUARDED_BINDING(Funke_Toggle_SetTransitionMode);
		b.Slider_GetTransitionMode     = FNK_GUARDED_BINDING(Funke_Slider_GetTransitionMode);
		b.Slider_SetTransitionMode     = FNK_GUARDED_BINDING(Funke_Slider_SetTransitionMode);
		b.InputField_GetTransitionMode = FNK_GUARDED_BINDING(Funke_InputField_GetTransitionMode);
		b.InputField_SetTransitionMode = FNK_GUARDED_BINDING(Funke_InputField_SetTransitionMode);
		b.Dropdown_GetTransitionMode   = FNK_GUARDED_BINDING(Funke_Dropdown_GetTransitionMode);
		b.Dropdown_SetTransitionMode   = FNK_GUARDED_BINDING(Funke_Dropdown_SetTransitionMode);

		b.Button_GetNormalSprite   = FNK_GUARDED_BINDING(Funke_Button_GetNormalSprite);
		b.Button_SetNormalSprite   = FNK_GUARDED_BINDING(Funke_Button_SetNormalSprite);
		b.Button_GetHoveredSprite  = FNK_GUARDED_BINDING(Funke_Button_GetHoveredSprite);
		b.Button_SetHoveredSprite  = FNK_GUARDED_BINDING(Funke_Button_SetHoveredSprite);
		b.Button_GetPressedSprite  = FNK_GUARDED_BINDING(Funke_Button_GetPressedSprite);
		b.Button_SetPressedSprite  = FNK_GUARDED_BINDING(Funke_Button_SetPressedSprite);
		b.Button_GetDisabledSprite = FNK_GUARDED_BINDING(Funke_Button_GetDisabledSprite);
		b.Button_SetDisabledSprite = FNK_GUARDED_BINDING(Funke_Button_SetDisabledSprite);
		b.Button_GetFocusedSprite  = FNK_GUARDED_BINDING(Funke_Button_GetFocusedSprite);
		b.Button_SetFocusedSprite  = FNK_GUARDED_BINDING(Funke_Button_SetFocusedSprite);

		b.Toggle_GetNormalSprite   = FNK_GUARDED_BINDING(Funke_Toggle_GetNormalSprite);
		b.Toggle_SetNormalSprite   = FNK_GUARDED_BINDING(Funke_Toggle_SetNormalSprite);
		b.Toggle_GetHoveredSprite  = FNK_GUARDED_BINDING(Funke_Toggle_GetHoveredSprite);
		b.Toggle_SetHoveredSprite  = FNK_GUARDED_BINDING(Funke_Toggle_SetHoveredSprite);
		b.Toggle_GetPressedSprite  = FNK_GUARDED_BINDING(Funke_Toggle_GetPressedSprite);
		b.Toggle_SetPressedSprite  = FNK_GUARDED_BINDING(Funke_Toggle_SetPressedSprite);
		b.Toggle_GetDisabledSprite = FNK_GUARDED_BINDING(Funke_Toggle_GetDisabledSprite);
		b.Toggle_SetDisabledSprite = FNK_GUARDED_BINDING(Funke_Toggle_SetDisabledSprite);
		b.Toggle_GetFocusedSprite  = FNK_GUARDED_BINDING(Funke_Toggle_GetFocusedSprite);
		b.Toggle_SetFocusedSprite  = FNK_GUARDED_BINDING(Funke_Toggle_SetFocusedSprite);

		b.Slider_GetNormalSprite   = FNK_GUARDED_BINDING(Funke_Slider_GetNormalSprite);
		b.Slider_SetNormalSprite   = FNK_GUARDED_BINDING(Funke_Slider_SetNormalSprite);
		b.Slider_GetHoveredSprite  = FNK_GUARDED_BINDING(Funke_Slider_GetHoveredSprite);
		b.Slider_SetHoveredSprite  = FNK_GUARDED_BINDING(Funke_Slider_SetHoveredSprite);
		b.Slider_GetPressedSprite  = FNK_GUARDED_BINDING(Funke_Slider_GetPressedSprite);
		b.Slider_SetPressedSprite  = FNK_GUARDED_BINDING(Funke_Slider_SetPressedSprite);
		b.Slider_GetDisabledSprite = FNK_GUARDED_BINDING(Funke_Slider_GetDisabledSprite);
		b.Slider_SetDisabledSprite = FNK_GUARDED_BINDING(Funke_Slider_SetDisabledSprite);
		b.Slider_GetFocusedSprite  = FNK_GUARDED_BINDING(Funke_Slider_GetFocusedSprite);
		b.Slider_SetFocusedSprite  = FNK_GUARDED_BINDING(Funke_Slider_SetFocusedSprite);

		b.InputField_GetNormalSprite   = FNK_GUARDED_BINDING(Funke_InputField_GetNormalSprite);
		b.InputField_SetNormalSprite   = FNK_GUARDED_BINDING(Funke_InputField_SetNormalSprite);
		b.InputField_GetHoveredSprite  = FNK_GUARDED_BINDING(Funke_InputField_GetHoveredSprite);
		b.InputField_SetHoveredSprite  = FNK_GUARDED_BINDING(Funke_InputField_SetHoveredSprite);
		b.InputField_GetPressedSprite  = FNK_GUARDED_BINDING(Funke_InputField_GetPressedSprite);
		b.InputField_SetPressedSprite  = FNK_GUARDED_BINDING(Funke_InputField_SetPressedSprite);
		b.InputField_GetDisabledSprite = FNK_GUARDED_BINDING(Funke_InputField_GetDisabledSprite);
		b.InputField_SetDisabledSprite = FNK_GUARDED_BINDING(Funke_InputField_SetDisabledSprite);
		b.InputField_GetFocusedSprite  = FNK_GUARDED_BINDING(Funke_InputField_GetFocusedSprite);
		b.InputField_SetFocusedSprite  = FNK_GUARDED_BINDING(Funke_InputField_SetFocusedSprite);

		b.Dropdown_GetNormalSprite   = FNK_GUARDED_BINDING(Funke_Dropdown_GetNormalSprite);
		b.Dropdown_SetNormalSprite   = FNK_GUARDED_BINDING(Funke_Dropdown_SetNormalSprite);
		b.Dropdown_GetHoveredSprite  = FNK_GUARDED_BINDING(Funke_Dropdown_GetHoveredSprite);
		b.Dropdown_SetHoveredSprite  = FNK_GUARDED_BINDING(Funke_Dropdown_SetHoveredSprite);
		b.Dropdown_GetPressedSprite  = FNK_GUARDED_BINDING(Funke_Dropdown_GetPressedSprite);
		b.Dropdown_SetPressedSprite  = FNK_GUARDED_BINDING(Funke_Dropdown_SetPressedSprite);
		b.Dropdown_GetDisabledSprite = FNK_GUARDED_BINDING(Funke_Dropdown_GetDisabledSprite);
		b.Dropdown_SetDisabledSprite = FNK_GUARDED_BINDING(Funke_Dropdown_SetDisabledSprite);
		b.Dropdown_GetFocusedSprite  = FNK_GUARDED_BINDING(Funke_Dropdown_GetFocusedSprite);
		b.Dropdown_SetFocusedSprite  = FNK_GUARDED_BINDING(Funke_Dropdown_SetFocusedSprite);

		b.Toggle_GetIsReadOnly   = FNK_GUARDED_BINDING(Funke_Toggle_GetIsReadOnly);
		b.Toggle_SetIsReadOnly   = FNK_GUARDED_BINDING(Funke_Toggle_SetIsReadOnly);
		b.Slider_GetIsReadOnly   = FNK_GUARDED_BINDING(Funke_Slider_GetIsReadOnly);
		b.Slider_SetIsReadOnly   = FNK_GUARDED_BINDING(Funke_Slider_SetIsReadOnly);
		b.Dropdown_GetIsReadOnly = FNK_GUARDED_BINDING(Funke_Dropdown_GetIsReadOnly);
		b.Dropdown_SetIsReadOnly = FNK_GUARDED_BINDING(Funke_Dropdown_SetIsReadOnly);

		b.Button_GetTargetGraphic     = FNK_GUARDED_BINDING(Funke_Button_GetTargetGraphic);
		b.Button_SetTargetGraphic     = FNK_GUARDED_BINDING(Funke_Button_SetTargetGraphic);
		b.Slider_GetFillEntity        = FNK_GUARDED_BINDING(Funke_Slider_GetFillEntity);
		b.Slider_SetFillEntity        = FNK_GUARDED_BINDING(Funke_Slider_SetFillEntity);
		b.Slider_GetHandleEntity      = FNK_GUARDED_BINDING(Funke_Slider_GetHandleEntity);
		b.Slider_SetHandleEntity      = FNK_GUARDED_BINDING(Funke_Slider_SetHandleEntity);
		b.Slider_GetBackgroundEntity  = FNK_GUARDED_BINDING(Funke_Slider_GetBackgroundEntity);
		b.Slider_SetBackgroundEntity  = FNK_GUARDED_BINDING(Funke_Slider_SetBackgroundEntity);
		b.Toggle_GetCheckmarkEntity   = FNK_GUARDED_BINDING(Funke_Toggle_GetCheckmarkEntity);
		b.Toggle_SetCheckmarkEntity   = FNK_GUARDED_BINDING(Funke_Toggle_SetCheckmarkEntity);
		b.InputField_GetTextEntity    = FNK_GUARDED_BINDING(Funke_InputField_GetTextEntity);
		b.InputField_SetTextEntity    = FNK_GUARDED_BINDING(Funke_InputField_SetTextEntity);
		b.Dropdown_GetLabelEntity     = FNK_GUARDED_BINDING(Funke_Dropdown_GetLabelEntity);
		b.Dropdown_SetLabelEntity     = FNK_GUARDED_BINDING(Funke_Dropdown_SetLabelEntity);

		b.Dropdown_GetOptionNormalColor   = FNK_GUARDED_BINDING(Funke_Dropdown_GetOptionNormalColor);
		b.Dropdown_SetOptionNormalColor   = FNK_GUARDED_BINDING(Funke_Dropdown_SetOptionNormalColor);
		b.Dropdown_GetOptionHoverColor    = FNK_GUARDED_BINDING(Funke_Dropdown_GetOptionHoverColor);
		b.Dropdown_SetOptionHoverColor    = FNK_GUARDED_BINDING(Funke_Dropdown_SetOptionHoverColor);
		b.Dropdown_GetOptionPressedColor  = FNK_GUARDED_BINDING(Funke_Dropdown_GetOptionPressedColor);
		b.Dropdown_SetOptionPressedColor  = FNK_GUARDED_BINDING(Funke_Dropdown_SetOptionPressedColor);
		b.Dropdown_GetOptionSelectedColor = FNK_GUARDED_BINDING(Funke_Dropdown_GetOptionSelectedColor);
		b.Dropdown_SetOptionSelectedColor = FNK_GUARDED_BINDING(Funke_Dropdown_SetOptionSelectedColor);
		b.Dropdown_GetPopupBackgroundColor = FNK_GUARDED_BINDING(Funke_Dropdown_GetPopupBackgroundColor);
		b.Dropdown_SetPopupBackgroundColor = FNK_GUARDED_BINDING(Funke_Dropdown_SetPopupBackgroundColor);
		b.Dropdown_GetOptionTextColor      = FNK_GUARDED_BINDING(Funke_Dropdown_GetOptionTextColor);
		b.Dropdown_SetOptionTextColor      = FNK_GUARDED_BINDING(Funke_Dropdown_SetOptionTextColor);

		b.Entity_GetComponentPtr   = FNK_GUARDED_BINDING(Funke_Entity_GetComponentPtr);
		b.Entity_GetComponentPtrReadOnly = FNK_GUARDED_BINDING(Funke_Entity_GetComponentPtrReadOnly);
		b.Entity_GetComponentSize  = FNK_GUARDED_BINDING(Funke_Entity_GetComponentSize);
		b.Scene_OpenQueryView      = FNK_GUARDED_BINDING(Funke_Scene_OpenQueryView);

		// ── UI (appended for binary compat — order MUST match NativeBindings in ScriptGlue.hpp and the C# mirror) ──

		// Scrollbar
		b.Scrollbar_GetValue                 = FNK_GUARDED_BINDING(Funke_Scrollbar_GetValue);
		b.Scrollbar_SetValue                 = FNK_GUARDED_BINDING(Funke_Scrollbar_SetValue);
		b.Scrollbar_GetSize                  = FNK_GUARDED_BINDING(Funke_Scrollbar_GetSize);
		b.Scrollbar_SetSize                  = FNK_GUARDED_BINDING(Funke_Scrollbar_SetSize);
		b.Scrollbar_GetNumberOfSteps         = FNK_GUARDED_BINDING(Funke_Scrollbar_GetNumberOfSteps);
		b.Scrollbar_SetNumberOfSteps         = FNK_GUARDED_BINDING(Funke_Scrollbar_SetNumberOfSteps);
		b.Scrollbar_GetDirection             = FNK_GUARDED_BINDING(Funke_Scrollbar_GetDirection);
		b.Scrollbar_SetDirection             = FNK_GUARDED_BINDING(Funke_Scrollbar_SetDirection);
		b.Scrollbar_GetIsReadOnly            = FNK_GUARDED_BINDING(Funke_Scrollbar_GetIsReadOnly);
		b.Scrollbar_SetIsReadOnly            = FNK_GUARDED_BINDING(Funke_Scrollbar_SetIsReadOnly);
		b.Scrollbar_GetHandleEntity          = FNK_GUARDED_BINDING(Funke_Scrollbar_GetHandleEntity);
		b.Scrollbar_SetHandleEntity          = FNK_GUARDED_BINDING(Funke_Scrollbar_SetHandleEntity);
		b.Scrollbar_GetValueChangedThisFrame = FNK_GUARDED_BINDING(Funke_Scrollbar_GetValueChangedThisFrame);
		b.Scrollbar_MarkValueObserved        = FNK_GUARDED_BINDING(Funke_Scrollbar_MarkValueObserved);
		b.Scrollbar_GetNormalColor   = FNK_GUARDED_BINDING(Funke_Scrollbar_GetNormalColor);
		b.Scrollbar_SetNormalColor   = FNK_GUARDED_BINDING(Funke_Scrollbar_SetNormalColor);
		b.Scrollbar_GetHoveredColor  = FNK_GUARDED_BINDING(Funke_Scrollbar_GetHoveredColor);
		b.Scrollbar_SetHoveredColor  = FNK_GUARDED_BINDING(Funke_Scrollbar_SetHoveredColor);
		b.Scrollbar_GetPressedColor  = FNK_GUARDED_BINDING(Funke_Scrollbar_GetPressedColor);
		b.Scrollbar_SetPressedColor  = FNK_GUARDED_BINDING(Funke_Scrollbar_SetPressedColor);
		b.Scrollbar_GetDisabledColor = FNK_GUARDED_BINDING(Funke_Scrollbar_GetDisabledColor);
		b.Scrollbar_SetDisabledColor = FNK_GUARDED_BINDING(Funke_Scrollbar_SetDisabledColor);
		b.Scrollbar_GetFocusedColor  = FNK_GUARDED_BINDING(Funke_Scrollbar_GetFocusedColor);
		b.Scrollbar_SetFocusedColor  = FNK_GUARDED_BINDING(Funke_Scrollbar_SetFocusedColor);
		b.Scrollbar_GetTransitionMode = FNK_GUARDED_BINDING(Funke_Scrollbar_GetTransitionMode);
		b.Scrollbar_SetTransitionMode = FNK_GUARDED_BINDING(Funke_Scrollbar_SetTransitionMode);
		b.Scrollbar_GetNormalSprite   = FNK_GUARDED_BINDING(Funke_Scrollbar_GetNormalSprite);
		b.Scrollbar_SetNormalSprite   = FNK_GUARDED_BINDING(Funke_Scrollbar_SetNormalSprite);
		b.Scrollbar_GetHoveredSprite  = FNK_GUARDED_BINDING(Funke_Scrollbar_GetHoveredSprite);
		b.Scrollbar_SetHoveredSprite  = FNK_GUARDED_BINDING(Funke_Scrollbar_SetHoveredSprite);
		b.Scrollbar_GetPressedSprite  = FNK_GUARDED_BINDING(Funke_Scrollbar_GetPressedSprite);
		b.Scrollbar_SetPressedSprite  = FNK_GUARDED_BINDING(Funke_Scrollbar_SetPressedSprite);
		b.Scrollbar_GetDisabledSprite = FNK_GUARDED_BINDING(Funke_Scrollbar_GetDisabledSprite);
		b.Scrollbar_SetDisabledSprite = FNK_GUARDED_BINDING(Funke_Scrollbar_SetDisabledSprite);
		b.Scrollbar_GetFocusedSprite  = FNK_GUARDED_BINDING(Funke_Scrollbar_GetFocusedSprite);
		b.Scrollbar_SetFocusedSprite  = FNK_GUARDED_BINDING(Funke_Scrollbar_SetFocusedSprite);

		// ScrollRect
		b.ScrollRect_GetContent                       = FNK_GUARDED_BINDING(Funke_ScrollRect_GetContent);
		b.ScrollRect_SetContent                       = FNK_GUARDED_BINDING(Funke_ScrollRect_SetContent);
		b.ScrollRect_GetViewport                      = FNK_GUARDED_BINDING(Funke_ScrollRect_GetViewport);
		b.ScrollRect_SetViewport                      = FNK_GUARDED_BINDING(Funke_ScrollRect_SetViewport);
		b.ScrollRect_GetHorizontal                    = FNK_GUARDED_BINDING(Funke_ScrollRect_GetHorizontal);
		b.ScrollRect_SetHorizontal                    = FNK_GUARDED_BINDING(Funke_ScrollRect_SetHorizontal);
		b.ScrollRect_GetVertical                      = FNK_GUARDED_BINDING(Funke_ScrollRect_GetVertical);
		b.ScrollRect_SetVertical                      = FNK_GUARDED_BINDING(Funke_ScrollRect_SetVertical);
		b.ScrollRect_GetMovementType                  = FNK_GUARDED_BINDING(Funke_ScrollRect_GetMovementType);
		b.ScrollRect_SetMovementType                  = FNK_GUARDED_BINDING(Funke_ScrollRect_SetMovementType);
		b.ScrollRect_GetElasticity                    = FNK_GUARDED_BINDING(Funke_ScrollRect_GetElasticity);
		b.ScrollRect_SetElasticity                    = FNK_GUARDED_BINDING(Funke_ScrollRect_SetElasticity);
		b.ScrollRect_GetInertia                       = FNK_GUARDED_BINDING(Funke_ScrollRect_GetInertia);
		b.ScrollRect_SetInertia                       = FNK_GUARDED_BINDING(Funke_ScrollRect_SetInertia);
		b.ScrollRect_GetDecelerationRate              = FNK_GUARDED_BINDING(Funke_ScrollRect_GetDecelerationRate);
		b.ScrollRect_SetDecelerationRate              = FNK_GUARDED_BINDING(Funke_ScrollRect_SetDecelerationRate);
		b.ScrollRect_GetScrollSensitivity             = FNK_GUARDED_BINDING(Funke_ScrollRect_GetScrollSensitivity);
		b.ScrollRect_SetScrollSensitivity             = FNK_GUARDED_BINDING(Funke_ScrollRect_SetScrollSensitivity);
		b.ScrollRect_GetHorizontalScrollbar           = FNK_GUARDED_BINDING(Funke_ScrollRect_GetHorizontalScrollbar);
		b.ScrollRect_SetHorizontalScrollbar           = FNK_GUARDED_BINDING(Funke_ScrollRect_SetHorizontalScrollbar);
		b.ScrollRect_GetVerticalScrollbar             = FNK_GUARDED_BINDING(Funke_ScrollRect_GetVerticalScrollbar);
		b.ScrollRect_SetVerticalScrollbar             = FNK_GUARDED_BINDING(Funke_ScrollRect_SetVerticalScrollbar);
		b.ScrollRect_GetHorizontalScrollbarVisibility = FNK_GUARDED_BINDING(Funke_ScrollRect_GetHorizontalScrollbarVisibility);
		b.ScrollRect_SetHorizontalScrollbarVisibility = FNK_GUARDED_BINDING(Funke_ScrollRect_SetHorizontalScrollbarVisibility);
		b.ScrollRect_GetVerticalScrollbarVisibility   = FNK_GUARDED_BINDING(Funke_ScrollRect_GetVerticalScrollbarVisibility);
		b.ScrollRect_SetVerticalScrollbarVisibility   = FNK_GUARDED_BINDING(Funke_ScrollRect_SetVerticalScrollbarVisibility);
		b.ScrollRect_GetHorizontalScrollbarSpacing    = FNK_GUARDED_BINDING(Funke_ScrollRect_GetHorizontalScrollbarSpacing);
		b.ScrollRect_SetHorizontalScrollbarSpacing    = FNK_GUARDED_BINDING(Funke_ScrollRect_SetHorizontalScrollbarSpacing);
		b.ScrollRect_GetVerticalScrollbarSpacing      = FNK_GUARDED_BINDING(Funke_ScrollRect_GetVerticalScrollbarSpacing);
		b.ScrollRect_SetVerticalScrollbarSpacing      = FNK_GUARDED_BINDING(Funke_ScrollRect_SetVerticalScrollbarSpacing);
		b.ScrollRect_GetNormalizedPosition            = FNK_GUARDED_BINDING(Funke_ScrollRect_GetNormalizedPosition);
		b.ScrollRect_SetNormalizedPosition            = FNK_GUARDED_BINDING(Funke_ScrollRect_SetNormalizedPosition);
		b.ScrollRect_GetValueChangedThisFrame         = FNK_GUARDED_BINDING(Funke_ScrollRect_GetValueChangedThisFrame);
		b.ScrollRect_MarkValueObserved                = FNK_GUARDED_BINDING(Funke_ScrollRect_MarkValueObserved);

		// Mask
		b.Mask_GetShowMaskGraphic = FNK_GUARDED_BINDING(Funke_Mask_GetShowMaskGraphic);
		b.Mask_SetShowMaskGraphic = FNK_GUARDED_BINDING(Funke_Mask_SetShowMaskGraphic);

		// CircularSlider
		b.CircularSlider_GetValue                 = FNK_GUARDED_BINDING(Funke_CircularSlider_GetValue);
		b.CircularSlider_SetValue                 = FNK_GUARDED_BINDING(Funke_CircularSlider_SetValue);
		b.CircularSlider_GetMinValue              = FNK_GUARDED_BINDING(Funke_CircularSlider_GetMinValue);
		b.CircularSlider_SetMinValue              = FNK_GUARDED_BINDING(Funke_CircularSlider_SetMinValue);
		b.CircularSlider_GetMaxValue              = FNK_GUARDED_BINDING(Funke_CircularSlider_GetMaxValue);
		b.CircularSlider_SetMaxValue              = FNK_GUARDED_BINDING(Funke_CircularSlider_SetMaxValue);
		b.CircularSlider_GetWholeNumbers          = FNK_GUARDED_BINDING(Funke_CircularSlider_GetWholeNumbers);
		b.CircularSlider_SetWholeNumbers          = FNK_GUARDED_BINDING(Funke_CircularSlider_SetWholeNumbers);
		b.CircularSlider_GetIsReadOnly            = FNK_GUARDED_BINDING(Funke_CircularSlider_GetIsReadOnly);
		b.CircularSlider_SetIsReadOnly            = FNK_GUARDED_BINDING(Funke_CircularSlider_SetIsReadOnly);
		b.CircularSlider_GetStartAngleDegrees     = FNK_GUARDED_BINDING(Funke_CircularSlider_GetStartAngleDegrees);
		b.CircularSlider_SetStartAngleDegrees     = FNK_GUARDED_BINDING(Funke_CircularSlider_SetStartAngleDegrees);
		b.CircularSlider_GetSweepDegrees          = FNK_GUARDED_BINDING(Funke_CircularSlider_GetSweepDegrees);
		b.CircularSlider_SetSweepDegrees          = FNK_GUARDED_BINDING(Funke_CircularSlider_SetSweepDegrees);
		b.CircularSlider_GetClockwise             = FNK_GUARDED_BINDING(Funke_CircularSlider_GetClockwise);
		b.CircularSlider_SetClockwise             = FNK_GUARDED_BINDING(Funke_CircularSlider_SetClockwise);
		b.CircularSlider_GetRingThickness         = FNK_GUARDED_BINDING(Funke_CircularSlider_GetRingThickness);
		b.CircularSlider_SetRingThickness         = FNK_GUARDED_BINDING(Funke_CircularSlider_SetRingThickness);
		b.CircularSlider_GetRingSegments          = FNK_GUARDED_BINDING(Funke_CircularSlider_GetRingSegments);
		b.CircularSlider_SetRingSegments          = FNK_GUARDED_BINDING(Funke_CircularSlider_SetRingSegments);
		b.CircularSlider_GetBackgroundColor       = FNK_GUARDED_BINDING(Funke_CircularSlider_GetBackgroundColor);
		b.CircularSlider_SetBackgroundColor       = FNK_GUARDED_BINDING(Funke_CircularSlider_SetBackgroundColor);
		b.CircularSlider_GetFillColor             = FNK_GUARDED_BINDING(Funke_CircularSlider_GetFillColor);
		b.CircularSlider_SetFillColor             = FNK_GUARDED_BINDING(Funke_CircularSlider_SetFillColor);
		b.CircularSlider_GetHandleEntity          = FNK_GUARDED_BINDING(Funke_CircularSlider_GetHandleEntity);
		b.CircularSlider_SetHandleEntity          = FNK_GUARDED_BINDING(Funke_CircularSlider_SetHandleEntity);
		b.CircularSlider_GetValueChangedThisFrame = FNK_GUARDED_BINDING(Funke_CircularSlider_GetValueChangedThisFrame);
		b.CircularSlider_MarkValueObserved        = FNK_GUARDED_BINDING(Funke_CircularSlider_MarkValueObserved);
		b.CircularSlider_GetNormalColor   = FNK_GUARDED_BINDING(Funke_CircularSlider_GetNormalColor);
		b.CircularSlider_SetNormalColor   = FNK_GUARDED_BINDING(Funke_CircularSlider_SetNormalColor);
		b.CircularSlider_GetHoveredColor  = FNK_GUARDED_BINDING(Funke_CircularSlider_GetHoveredColor);
		b.CircularSlider_SetHoveredColor  = FNK_GUARDED_BINDING(Funke_CircularSlider_SetHoveredColor);
		b.CircularSlider_GetPressedColor  = FNK_GUARDED_BINDING(Funke_CircularSlider_GetPressedColor);
		b.CircularSlider_SetPressedColor  = FNK_GUARDED_BINDING(Funke_CircularSlider_SetPressedColor);
		b.CircularSlider_GetDisabledColor = FNK_GUARDED_BINDING(Funke_CircularSlider_GetDisabledColor);
		b.CircularSlider_SetDisabledColor = FNK_GUARDED_BINDING(Funke_CircularSlider_SetDisabledColor);
		b.CircularSlider_GetFocusedColor  = FNK_GUARDED_BINDING(Funke_CircularSlider_GetFocusedColor);
		b.CircularSlider_SetFocusedColor  = FNK_GUARDED_BINDING(Funke_CircularSlider_SetFocusedColor);
		b.CircularSlider_GetTransitionMode = FNK_GUARDED_BINDING(Funke_CircularSlider_GetTransitionMode);
		b.CircularSlider_SetTransitionMode = FNK_GUARDED_BINDING(Funke_CircularSlider_SetTransitionMode);
		b.CircularSlider_GetNormalSprite   = FNK_GUARDED_BINDING(Funke_CircularSlider_GetNormalSprite);
		b.CircularSlider_SetNormalSprite   = FNK_GUARDED_BINDING(Funke_CircularSlider_SetNormalSprite);
		b.CircularSlider_GetHoveredSprite  = FNK_GUARDED_BINDING(Funke_CircularSlider_GetHoveredSprite);
		b.CircularSlider_SetHoveredSprite  = FNK_GUARDED_BINDING(Funke_CircularSlider_SetHoveredSprite);
		b.CircularSlider_GetPressedSprite  = FNK_GUARDED_BINDING(Funke_CircularSlider_GetPressedSprite);
		b.CircularSlider_SetPressedSprite  = FNK_GUARDED_BINDING(Funke_CircularSlider_SetPressedSprite);
		b.CircularSlider_GetDisabledSprite = FNK_GUARDED_BINDING(Funke_CircularSlider_GetDisabledSprite);
		b.CircularSlider_SetDisabledSprite = FNK_GUARDED_BINDING(Funke_CircularSlider_SetDisabledSprite);
		b.CircularSlider_GetFocusedSprite  = FNK_GUARDED_BINDING(Funke_CircularSlider_GetFocusedSprite);
		b.CircularSlider_SetFocusedSprite  = FNK_GUARDED_BINDING(Funke_CircularSlider_SetFocusedSprite);

		// HorizontalLayoutGroup
		b.HorizontalLayoutGroup_GetPaddingLeft            = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_GetPaddingLeft);
		b.HorizontalLayoutGroup_SetPaddingLeft            = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_SetPaddingLeft);
		b.HorizontalLayoutGroup_GetPaddingRight           = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_GetPaddingRight);
		b.HorizontalLayoutGroup_SetPaddingRight           = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_SetPaddingRight);
		b.HorizontalLayoutGroup_GetPaddingTop             = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_GetPaddingTop);
		b.HorizontalLayoutGroup_SetPaddingTop             = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_SetPaddingTop);
		b.HorizontalLayoutGroup_GetPaddingBottom          = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_GetPaddingBottom);
		b.HorizontalLayoutGroup_SetPaddingBottom          = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_SetPaddingBottom);
		b.HorizontalLayoutGroup_GetSpacing                = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_GetSpacing);
		b.HorizontalLayoutGroup_SetSpacing                = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_SetSpacing);
		b.HorizontalLayoutGroup_GetChildAlignment         = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_GetChildAlignment);
		b.HorizontalLayoutGroup_SetChildAlignment         = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_SetChildAlignment);
		b.HorizontalLayoutGroup_GetReverseArrangement     = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_GetReverseArrangement);
		b.HorizontalLayoutGroup_SetReverseArrangement     = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_SetReverseArrangement);
		b.HorizontalLayoutGroup_GetControlChildWidth      = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_GetControlChildWidth);
		b.HorizontalLayoutGroup_SetControlChildWidth      = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_SetControlChildWidth);
		b.HorizontalLayoutGroup_GetControlChildHeight     = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_GetControlChildHeight);
		b.HorizontalLayoutGroup_SetControlChildHeight     = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_SetControlChildHeight);
		b.HorizontalLayoutGroup_GetUseChildScaleWidth     = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_GetUseChildScaleWidth);
		b.HorizontalLayoutGroup_SetUseChildScaleWidth     = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_SetUseChildScaleWidth);
		b.HorizontalLayoutGroup_GetUseChildScaleHeight    = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_GetUseChildScaleHeight);
		b.HorizontalLayoutGroup_SetUseChildScaleHeight    = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_SetUseChildScaleHeight);
		b.HorizontalLayoutGroup_GetChildForceExpandWidth  = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_GetChildForceExpandWidth);
		b.HorizontalLayoutGroup_SetChildForceExpandWidth  = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_SetChildForceExpandWidth);
		b.HorizontalLayoutGroup_GetChildForceExpandHeight = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_GetChildForceExpandHeight);
		b.HorizontalLayoutGroup_SetChildForceExpandHeight = FNK_GUARDED_BINDING(Funke_HorizontalLayoutGroup_SetChildForceExpandHeight);

		// VerticalLayoutGroup
		b.VerticalLayoutGroup_GetPaddingLeft            = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_GetPaddingLeft);
		b.VerticalLayoutGroup_SetPaddingLeft            = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_SetPaddingLeft);
		b.VerticalLayoutGroup_GetPaddingRight           = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_GetPaddingRight);
		b.VerticalLayoutGroup_SetPaddingRight           = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_SetPaddingRight);
		b.VerticalLayoutGroup_GetPaddingTop             = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_GetPaddingTop);
		b.VerticalLayoutGroup_SetPaddingTop             = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_SetPaddingTop);
		b.VerticalLayoutGroup_GetPaddingBottom          = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_GetPaddingBottom);
		b.VerticalLayoutGroup_SetPaddingBottom          = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_SetPaddingBottom);
		b.VerticalLayoutGroup_GetSpacing                = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_GetSpacing);
		b.VerticalLayoutGroup_SetSpacing                = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_SetSpacing);
		b.VerticalLayoutGroup_GetChildAlignment         = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_GetChildAlignment);
		b.VerticalLayoutGroup_SetChildAlignment         = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_SetChildAlignment);
		b.VerticalLayoutGroup_GetReverseArrangement     = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_GetReverseArrangement);
		b.VerticalLayoutGroup_SetReverseArrangement     = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_SetReverseArrangement);
		b.VerticalLayoutGroup_GetControlChildWidth      = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_GetControlChildWidth);
		b.VerticalLayoutGroup_SetControlChildWidth      = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_SetControlChildWidth);
		b.VerticalLayoutGroup_GetControlChildHeight     = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_GetControlChildHeight);
		b.VerticalLayoutGroup_SetControlChildHeight     = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_SetControlChildHeight);
		b.VerticalLayoutGroup_GetUseChildScaleWidth     = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_GetUseChildScaleWidth);
		b.VerticalLayoutGroup_SetUseChildScaleWidth     = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_SetUseChildScaleWidth);
		b.VerticalLayoutGroup_GetUseChildScaleHeight    = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_GetUseChildScaleHeight);
		b.VerticalLayoutGroup_SetUseChildScaleHeight    = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_SetUseChildScaleHeight);
		b.VerticalLayoutGroup_GetChildForceExpandWidth  = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_GetChildForceExpandWidth);
		b.VerticalLayoutGroup_SetChildForceExpandWidth  = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_SetChildForceExpandWidth);
		b.VerticalLayoutGroup_GetChildForceExpandHeight = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_GetChildForceExpandHeight);
		b.VerticalLayoutGroup_SetChildForceExpandHeight = FNK_GUARDED_BINDING(Funke_VerticalLayoutGroup_SetChildForceExpandHeight);

		// GridLayoutGroup
		b.GridLayoutGroup_GetPaddingLeft     = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_GetPaddingLeft);
		b.GridLayoutGroup_SetPaddingLeft     = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_SetPaddingLeft);
		b.GridLayoutGroup_GetPaddingRight    = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_GetPaddingRight);
		b.GridLayoutGroup_SetPaddingRight    = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_SetPaddingRight);
		b.GridLayoutGroup_GetPaddingTop      = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_GetPaddingTop);
		b.GridLayoutGroup_SetPaddingTop      = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_SetPaddingTop);
		b.GridLayoutGroup_GetPaddingBottom   = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_GetPaddingBottom);
		b.GridLayoutGroup_SetPaddingBottom   = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_SetPaddingBottom);
		b.GridLayoutGroup_GetCellSize        = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_GetCellSize);
		b.GridLayoutGroup_SetCellSize        = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_SetCellSize);
		b.GridLayoutGroup_GetSpacing         = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_GetSpacing);
		b.GridLayoutGroup_SetSpacing         = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_SetSpacing);
		b.GridLayoutGroup_GetStartCorner     = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_GetStartCorner);
		b.GridLayoutGroup_SetStartCorner     = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_SetStartCorner);
		b.GridLayoutGroup_GetStartAxis       = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_GetStartAxis);
		b.GridLayoutGroup_SetStartAxis       = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_SetStartAxis);
		b.GridLayoutGroup_GetChildAlignment  = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_GetChildAlignment);
		b.GridLayoutGroup_SetChildAlignment  = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_SetChildAlignment);
		b.GridLayoutGroup_GetConstraint      = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_GetConstraint);
		b.GridLayoutGroup_SetConstraint      = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_SetConstraint);
		b.GridLayoutGroup_GetConstraintCount = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_GetConstraintCount);
		b.GridLayoutGroup_SetConstraintCount = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_SetConstraintCount);
		b.GridLayoutGroup_GetReverse         = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_GetReverse);
		b.GridLayoutGroup_SetReverse         = FNK_GUARDED_BINDING(Funke_GridLayoutGroup_SetReverse);

		// ContentSizeFitter
		b.ContentSizeFitter_GetHorizontalFit  = FNK_GUARDED_BINDING(Funke_ContentSizeFitter_GetHorizontalFit);
		b.ContentSizeFitter_SetHorizontalFit  = FNK_GUARDED_BINDING(Funke_ContentSizeFitter_SetHorizontalFit);
		b.ContentSizeFitter_GetVerticalFit    = FNK_GUARDED_BINDING(Funke_ContentSizeFitter_GetVerticalFit);
		b.ContentSizeFitter_SetVerticalFit    = FNK_GUARDED_BINDING(Funke_ContentSizeFitter_SetVerticalFit);
		b.ContentSizeFitter_GetPaddingLeft    = FNK_GUARDED_BINDING(Funke_ContentSizeFitter_GetPaddingLeft);
		b.ContentSizeFitter_SetPaddingLeft    = FNK_GUARDED_BINDING(Funke_ContentSizeFitter_SetPaddingLeft);
		b.ContentSizeFitter_GetPaddingRight   = FNK_GUARDED_BINDING(Funke_ContentSizeFitter_GetPaddingRight);
		b.ContentSizeFitter_SetPaddingRight   = FNK_GUARDED_BINDING(Funke_ContentSizeFitter_SetPaddingRight);
		b.ContentSizeFitter_GetPaddingTop     = FNK_GUARDED_BINDING(Funke_ContentSizeFitter_GetPaddingTop);
		b.ContentSizeFitter_SetPaddingTop     = FNK_GUARDED_BINDING(Funke_ContentSizeFitter_SetPaddingTop);
		b.ContentSizeFitter_GetPaddingBottom  = FNK_GUARDED_BINDING(Funke_ContentSizeFitter_GetPaddingBottom);
		b.ContentSizeFitter_SetPaddingBottom  = FNK_GUARDED_BINDING(Funke_ContentSizeFitter_SetPaddingBottom);

		// WidthConstraint
		b.WidthConstraint_GetMinWidth = FNK_GUARDED_BINDING(Funke_WidthConstraint_GetMinWidth);
		b.WidthConstraint_SetMinWidth = FNK_GUARDED_BINDING(Funke_WidthConstraint_SetMinWidth);
		b.WidthConstraint_GetMaxWidth = FNK_GUARDED_BINDING(Funke_WidthConstraint_GetMaxWidth);
		b.WidthConstraint_SetMaxWidth = FNK_GUARDED_BINDING(Funke_WidthConstraint_SetMaxWidth);

		// ── SpriteRenderer filter (appended for binary compat) ──

		// ── SpriteRenderer slice name (appended for binary compat) ──

		// ── Dynamic component registration (appended for binary compat) ──
		b.Component_RegisterDynamic       = FNK_GUARDED_BINDING(Funke_Component_RegisterDynamic);
		b.Component_UnregisterAllDynamic  = FNK_GUARDED_BINDING(Funke_Component_UnregisterAllDynamic);

		// ── Scene load-by-GUID (appended for binary compat) ──
		b.Scene_LoadByGuid           = FNK_GUARDED_BINDING(Funke_Scene_LoadByGuid);
		b.Scene_LoadAdditiveByGuid   = FNK_GUARDED_BINDING(Funke_Scene_LoadAdditiveByGuid);
		b.Scene_UnloadByGuid         = FNK_GUARDED_BINDING(Funke_Scene_UnloadByGuid);
		b.Scene_SetActiveByGuid      = FNK_GUARDED_BINDING(Funke_Scene_SetActiveByGuid);
		b.Scene_ReloadByGuid         = FNK_GUARDED_BINDING(Funke_Scene_ReloadByGuid);
		b.Scene_DoesSceneExistByGuid = FNK_GUARDED_BINDING(Funke_Scene_DoesSceneExistByGuid);
		b.Scene_GetActiveSceneGuid   = FNK_GUARDED_BINDING(Funke_Scene_GetActiveSceneGuid);

		// Rigidbody2D motion locks (appended for binary compat).


		b.Scene_OpenQueryViewWithEntities = FNK_GUARDED_BINDING(Funke_Scene_OpenQueryViewWithEntities);
	}

} // namespace Funke
