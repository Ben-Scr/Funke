#include "pch.hpp"
#include "Scripting/ScriptBindingGuard.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptEngine.hpp"
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
#include "Systems/UIEventSystem.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Project/ProjectManager.hpp"
#include "Project/IndexProject.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#if INDEX_WITH_2D
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

namespace Index {
	bool ScriptBindings::IsScriptInputEnabled()
	{
		return Application::s_Instance && Application::s_Instance->m_IsScriptInputEnabled;
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
	#define IDX_BINDING_TRY try
	#define IDX_BINDING_CATCH(default_return)                                                  \
		catch (const std::exception& _idx_ex) {                                                \
			IDX_CORE_ERROR_TAG("ScriptBinding", "exception in {}: {}", __func__, _idx_ex.what()); \
			return default_return;                                                             \
		}                                                                                      \
		catch (...) {                                                                          \
			IDX_CORE_ERROR_TAG("ScriptBinding", "unknown exception in {}", __func__);          \
			return default_return;                                                             \
		}
	// Void-return variant (no return statement in catch).
	#define IDX_BINDING_CATCH_VOID                                                             \
		catch (const std::exception& _idx_ex) {                                                \
			IDX_CORE_ERROR_TAG("ScriptBinding", "exception in {}: {}", __func__, _idx_ex.what()); \
		}                                                                                      \
		catch (...) {                                                                          \
			IDX_CORE_ERROR_TAG("ScriptBinding", "unknown exception in {}", __func__);          \
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

#if INDEX_WITH_2D
	bool SetParentKeepingWorld2D(Scene& scene, EntityHandle handle, Entity newParent);
#endif
	void PopulateNonComponentBindings(NativeBindings& b);
	void PopulateEcbBindings(NativeBindings& b);
	void PopulateJobsBindings(NativeBindings& b);

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

	static int Index_Entity_GetManagedComponentFieldsBuffer(uint64_t entityID, const char* componentName, char* outBuffer, int capacity)
	{
		IDX_BINDING_TRY {
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
		} IDX_BINDING_CATCH(CopyCStringToBuffer("{}", outBuffer, capacity));
	}

	static int Index_Entity_GetIsStatic(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return scene->HasComponent<StaticTag>(handle) ? 1 : 0;
	}

	static void Index_Entity_SetIsStatic(uint64_t entityID, int isStatic)
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
	static int Index_Entity_GetIsEnabled(uint64_t entityID)
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
	static int Index_Entity_GetIsEnabledInHierarchy(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return scene->HasComponent<DisabledTag>(handle) ? 0 : 1;
	}

	static void Index_Entity_SetIsEnabled(uint64_t entityID, int isEnabled)
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

	int Index_Entity_HasComponent(uint64_t entityID, const char* componentName)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info || !info->has) return HasManagedComponent(*scene, handle, componentName ? componentName : "") ? 1 : 0;
		return info->has(scene->GetEntity(handle)) ? 1 : 0;
	}

	int Index_Entity_AddComponent(uint64_t entityID, const char* componentName)
	{
		IDX_BINDING_TRY {
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
		} IDX_BINDING_CATCH(0)
	}

	int Index_Entity_RemoveComponent(uint64_t entityID, const char* componentName)
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

	int Index_Entity_AddScript(uint64_t entityID, const char* className)
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

	int Index_Entity_HasScript(uint64_t entityID, const char* className)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		if (!className || !*className) return 0;
		if (!scene->HasComponent<ScriptComponent>(handle)) return 0;
		return scene->GetComponent<ScriptComponent>(handle).HasScript(className) ? 1 : 0;
	}

	int Index_Entity_RemoveScript(uint64_t entityID, const char* className)
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
#if INDEX_WITH_2D
		static const std::type_index kTransform2DType(typeid(Transform2DComponent));
		return info && info->typeId == kTransform2DType;
#else
		(void)info;
		return false;
#endif
	}

	// Validity contract: pointer is invalidated by any structural change to the pool; callers must refetch each frame.
	static void* Index_Entity_GetComponentPtr(uint64_t entityID, const char* componentName)
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
	static void* Index_Entity_GetComponentPtrReadOnly(uint64_t entityID, const char* componentName)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return nullptr;

		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info || !info->getRaw) return nullptr;
		return info->getRaw(scene->GetEntity(handle));
	}

	static int Index_Entity_GetComponentSize(const char* componentName)
	{
		const ComponentInfo* info = FindComponentByName(componentName);
		if (!info) return 0;
		return static_cast<int>(info->rawSize);
	}

	uint64_t Index_Entity_Clone(uint64_t sourceEntityID)
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

	uint64_t Index_Entity_InstantiatePrefab(uint64_t prefabGuid)
	{
		Scene* targetScene = GetScene();
		if (!targetScene) return 0;

		const EntityHandle instance = SceneSerializer::InstantiatePrefab(*targetScene, prefabGuid);
		return instance != entt::null ? GetEntityScriptId(*targetScene, instance) : 0;
	}

	int Index_Entity_GetOrigin(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return static_cast<int>(EntityOrigin::Runtime);
		return static_cast<int>(scene->GetEntityOrigin(handle));
	}

	uint64_t Index_Entity_GetRuntimeID(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return scene->GetRuntimeID(handle);
	}

	uint64_t Index_Entity_GetSceneGUID(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return static_cast<uint64_t>(scene->GetSceneEntityGUID(handle));
	}

	uint64_t Index_Entity_GetPrefabGUID(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return static_cast<uint64_t>(scene->GetPrefabGUID(handle));
	}

	// ── Scene ───────────────────────────────────────────────────────────

	static int Index_Scene_GetActiveSceneNameBuffer(char* outBuffer, int capacity)
	{
		IDX_BINDING_TRY {
			Scene* scene = GetScene();
			if (!scene) return CopyCStringToBuffer("", outBuffer, capacity);
			return CopyCStringToBuffer(scene->GetName().c_str(), outBuffer, capacity);
		} IDX_BINDING_CATCH(CopyCStringToBuffer("", outBuffer, capacity));
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

	static int Index_Scene_GetEntityCount() {
		Scene* scene = GetScene();
		if (!scene) return 0;
		return CountSceneEntities(*scene);
	}

	static int Index_Scene_GetEntityCountByName(const char* sceneName) {
		if (!sceneName || sceneName[0] == '\0') return 0;
		auto scene = SceneManager::GetLoadedScene(sceneName).lock();
		if (!scene || !scene->IsLoaded()) return 0;
		return CountSceneEntities(*scene);
	}

	static int Index_Scene_GetEntityNameByUUIDBuffer(uint64_t uuid, char* outBuffer, int capacity)
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
		// Reject empty names before project lookup or registration.
		if (!sceneName || sceneName[0] == '\0') {
			IDX_CORE_ERROR_TAG("ScriptBindings", "LoadScene called with an empty name");
			return 0;
		}

		std::string name(sceneName);
		IndexProject* project = ProjectManager::GetCurrentProject();
		const bool hasDefinition = SceneManager::HasSceneDefinition(name);

		// GetSceneFilePath also rejects traversal/control-byte names via IsValidSceneName.
		std::string scenePath;
		if (project) {
			scenePath = project->GetSceneFilePath(name);
		}
		const bool fileResolves = !scenePath.empty() && File::Exists(scenePath);

		if (!hasDefinition && !fileResolves) {
			IDX_CORE_ERROR_TAG("ScriptBindings",
				"LoadScene: no scene file resolves for '{}' and no definition is registered", name);
			return 0;
		}

		if (!hasDefinition) {
			auto& definition = SceneManager::RegisterScene(name);
			definition.OnLoadChecked([scenePath, name](Scene& scene) {
				if (File::Exists(scenePath)) {
					return SceneSerializer::LoadFromFile(scene, scenePath);
				}
				else {
					IDX_CORE_ERROR_TAG("ScriptBindings",
						"LoadScene: '{}' no longer exists at '{}'.",
						name, scenePath);
					return false;
				}
			});
		}

		// LoadScene on a scene that is already loaded restarts it from disk (a single load would otherwise be a no-op).
		const bool restart = !additive && SceneManager::IsSceneLoaded(name);
		const SceneLoadResult loadResult = restart
			? SceneManager::TryRestartScene(name)
			: additive
				? SceneManager::TryLoadSceneAdditive(name)
				: SceneManager::TryLoadScene(name);
		auto scenePtr = loadResult.LoadedScene.lock();
		if (!loadResult || !scenePtr) {
			IDX_CORE_ERROR_TAG("ScriptBindings", "Failed to {} scene{} '{}': {}",
				restart ? "restart" : "load", additive ? " additively" : "", name, loadResult.Message);
			return 0;
		}

		IDX_INFO_TAG("ScriptBindings", "{} scene{}: {}", restart ? "Restarted" : "Loaded", additive ? " additively" : "", name);
		return 1;
	}

	static int Index_Scene_LoadAdditive(const char* sceneName) {
		return LoadSceneByName(sceneName, true);
	}

	static int Index_Scene_Load(const char* sceneName) {
		IDX_BINDING_TRY {
			return LoadSceneByName(sceneName, false);
		} IDX_BINDING_CATCH(0)
	}

	static void Index_Scene_Unload(const char* sceneName) {
		SceneManager::UnloadScene(sceneName);
	}

	static int Index_Scene_SetActive(const char* sceneName) {
		return SceneManager::SetActiveScene(sceneName) ? 1 : 0;
	}

	static int Index_Scene_Reload(const char* sceneName) {
		auto result = SceneManager::ReloadScene(sceneName);
		return result.lock() ? 1 : 0;
	}

	static int Index_Scene_SetSceneSystemEnabled(const char* sceneName, const char* className, int enabled) {
		if (!sceneName || !className || sceneName[0] == '\0' || className[0] == '\0') return 0;
		auto scene = SceneManager::GetLoadedScene(sceneName).lock();
		return scene && scene->SetSceneSystemEnabled(className, enabled != 0) ? 1 : 0;
	}

	static int Index_Scene_IsSceneSystemEnabled(const char* sceneName, const char* className) {
		if (!sceneName || !className || sceneName[0] == '\0' || className[0] == '\0') return 0;
		auto scene = SceneManager::GetLoadedScene(sceneName).lock();
		return scene && scene->IsSceneSystemEnabled(className) ? 1 : 0;
	}

	static void Index_Scene_SetGlobalSystemEnabled(const char* className, int enabled) {
		ScriptEngine::SetGlobalSystemEnabled(className ? className : "", enabled != 0);
	}

	static int Index_Scene_DoesSceneExist(const char* sceneName) {
		if (!sceneName || sceneName[0] == '\0') return 0;
		return SceneManager::HasSceneDefinition(sceneName) ? 1 : 0;
	}

	static int Index_Scene_GetLoadedCount() {
		return static_cast<int>(SceneManager::GetLoadedSceneCount());
	}

	static int Index_Scene_GetLoadedSceneNameAtBuffer(int index, char* outBuffer, int capacity)
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

	static int Index_Scene_LoadByGuid(uint64_t sceneGuid) {
		const std::string sceneName = ResolveSceneNameForGuid(sceneGuid);
		if (sceneName.empty()) {
			IDX_CORE_ERROR_TAG("ScriptBindings",
				"Scene_LoadByGuid: no Scene asset registered for GUID {}.", sceneGuid);
			return 0;
		}
		return LoadSceneByName(sceneName.c_str(), false);
	}

	static int Index_Scene_LoadAdditiveByGuid(uint64_t sceneGuid) {
		const std::string sceneName = ResolveSceneNameForGuid(sceneGuid);
		if (sceneName.empty()) {
			IDX_CORE_ERROR_TAG("ScriptBindings",
				"Scene_LoadAdditiveByGuid: no Scene asset registered for GUID {}.", sceneGuid);
			return 0;
		}
		return LoadSceneByName(sceneName.c_str(), true);
	}

	static void Index_Scene_UnloadByGuid(uint64_t sceneGuid) {
		const std::string sceneName = ResolveSceneNameForGuid(sceneGuid);
		if (sceneName.empty()) {
			IDX_CORE_WARN_TAG("ScriptBindings",
				"Scene_UnloadByGuid: no Scene asset registered for GUID {}.", sceneGuid);
			return;
		}
		SceneManager::UnloadScene(sceneName);
	}

	static int Index_Scene_SetActiveByGuid(uint64_t sceneGuid) {
		const std::string sceneName = ResolveSceneNameForGuid(sceneGuid);
		if (sceneName.empty()) return 0;
		return SceneManager::SetActiveScene(sceneName) ? 1 : 0;
	}

	static int Index_Scene_ReloadByGuid(uint64_t sceneGuid) {
		const std::string sceneName = ResolveSceneNameForGuid(sceneGuid);
		if (sceneName.empty()) return 0;
		auto result = SceneManager::ReloadScene(sceneName);
		return result.lock() ? 1 : 0;
	}

	static int Index_Scene_DoesSceneExistByGuid(uint64_t sceneGuid) {
		return ResolveSceneNameForGuid(sceneGuid).empty() ? 0 : 1;
	}

	static uint64_t Index_Scene_GetActiveSceneGuid() {
		Scene* scene = GetScene();
		if (!scene) return 0;
		const IndexProject* project = ProjectManager::GetCurrentProject();
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

	static int Index_Scene_QueryEntities(const char* componentNames, uint64_t* outEntityIDs, int maxOut) {
		IDX_BINDING_TRY {
			return QueryEntitiesInScene(GetScene(), componentNames, outEntityIDs, maxOut);
		} IDX_BINDING_CATCH(0);
	}

	static int Index_Scene_QueryEntitiesInScene(const char* sceneName, const char* componentNames, uint64_t* outEntityIDs, int maxOut) {
		IDX_BINDING_TRY {
			return QueryEntitiesInScene(ResolveLoadedSceneForQuery(sceneName), componentNames, outEntityIDs, maxOut);
		} IDX_BINDING_CATCH(0);
	}

	static int Index_Asset_IsValid(uint64_t assetId)
	{
		return AssetRegistry::Exists(assetId) ? 1 : 0;
	}

	static uint64_t Index_Asset_GetOrCreateUUIDFromPath(const char* path)
	{
		if (!path || path[0] == '\0') {
			return 0;
		}

		return AssetRegistry::GetOrCreateAssetUUID(path);
	}

	static int Index_Asset_GetPathBuffer(uint64_t assetId, char* outBuffer, int capacity)
	{
		IDX_BINDING_TRY {
			const std::string path = AssetRegistry::ResolvePath(assetId);
			return CopyCStringToBuffer(path.c_str(), outBuffer, capacity);
		} IDX_BINDING_CATCH(CopyCStringToBuffer("", outBuffer, capacity));
	}

	static int Index_Asset_GetDisplayNameBuffer(uint64_t assetId, char* outBuffer, int capacity)
	{
		IDX_BINDING_TRY {
			const std::string name = AssetRegistry::GetDisplayName(assetId);
			return CopyCStringToBuffer(name.c_str(), outBuffer, capacity);
		} IDX_BINDING_CATCH(CopyCStringToBuffer("", outBuffer, capacity));
	}

	static int Index_Asset_GetKind(uint64_t assetId)
	{
		return static_cast<int>(AssetRegistry::GetKind(assetId));
	}

	static int Index_DataAsset_EnsureLoaded(uint64_t guid)
	{
		return DataAssetManager::Load(guid) ? 1 : 0;
	}

	static int Index_Asset_FindAllBuffer(const char* pathPrefix, int kind, char* outBuffer, int capacity)
	{
		IDX_BINDING_TRY {
			Json::Value ids = Json::Value::MakeArray();
			for (const AssetRegistry::Record& record : AssetRegistry::FindAll(
					 static_cast<AssetKind>(kind),
					 pathPrefix ? pathPrefix : "")) {
				ids.Append(Json::Value(std::to_string(record.Id)));
			}

			const std::string serialized = Json::Stringify(ids, false);
			return CopyCStringToBuffer(serialized.c_str(), outBuffer, capacity);
		} IDX_BINDING_CATCH(CopyCStringToBuffer("[]", outBuffer, capacity));
	}

	static int Index_Texture_LoadAsset(uint64_t assetId)
	{
		return TextureManager::LoadTextureByUUID(assetId).IsValid() ? 1 : 0;
	}

	static int Index_Texture_GetWidth(uint64_t assetId)
	{
		TextureHandle handle = TextureManager::LoadTextureByUUID(assetId);
		Texture2D* texture = TextureManager::GetTexture(handle);
		return texture ? static_cast<int>(texture->GetWidth()) : 0;
	}

	static int Index_Texture_GetHeight(uint64_t assetId)
	{
		TextureHandle handle = TextureManager::LoadTextureByUUID(assetId);
		Texture2D* texture = TextureManager::GetTexture(handle);
		return texture ? static_cast<int>(texture->GetHeight()) : 0;
	}

	// ── Runtime / procedural textures ───────────────────────────────────
	// rgba is width*height*4 RGBA8 bytes owned by the C# side. Returns the
	// synthetic UUID (0 on failure) which the script assigns to a
	// SpriteRenderer / Image like any other texture.
	static uint64_t Index_Texture_CreateRuntime(int width, int height, const uint8_t* rgba, int filter)
	{
		return TextureManager::CreateRuntimeTexture(width, height, rgba,
			static_cast<Filter>(filter), Wrap::Clamp, Wrap::Clamp);
	}
	// Re-upload the full image (byteCount must be width*height*4). Returns 1 on success.
	static int Index_Texture_UpdateRuntime(uint64_t runtimeUuid, const uint8_t* rgba, int byteCount)
	{
		if (byteCount < 0) return 0;
		return TextureManager::UpdateRuntimeTexture(runtimeUuid, rgba,
			static_cast<size_t>(byteCount)) ? 1 : 0;
	}
	static void Index_Texture_DestroyRuntime(uint64_t runtimeUuid)
	{
		TextureManager::DestroyRuntimeTexture(runtimeUuid);
	}
	static int Index_Texture_IsRuntime(uint64_t uuid)
	{
		return TextureManager::IsRuntimeTextureUUID(uuid) ? 1 : 0;
	}

	static uint64_t Index_Texture_GetDefaultAssetUUID(uint8_t which)
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

	static int Index_Audio_LoadAsset(uint64_t assetId)
	{
		return AudioManager::LoadAudioByUUID(assetId).IsValid() ? 1 : 0;
	}

	static int Index_Font_LoadAsset(uint64_t assetId)
	{
		// DO NOT bake an atlas here — atlas slots are per (uuid, pixelSize); TextRenderer bakes at FontSize on first draw.
		return assetId != 0 && AssetRegistry::IsFont(assetId) ? 1 : 0;
	}

	static void Index_Audio_PlayOneShotAsset(uint64_t assetId, float volume)
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

	static int Index_Scene_QueryEntitiesFiltered(
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
	static int Index_Scene_OpenQueryViewImpl(
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
		IDX_BINDING_TRY {
			Scene* scene = ResolveLoadedSceneForQuery(sceneName);
			if (!scene || !scene->IsLoaded()) return 0;
			auto& registry = scene->GetRegistry();

			INDEX_PROFILE_SCOPE("Scene.OpenQueryView");

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
				INDEX_PROFILE_SCOPE("Scene.OpenQueryView.Snapshot.ParallelBitmap");
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
				INDEX_PROFILE_SCOPE("Scene.OpenQueryView.Snapshot.ParallelChunked");
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
			INDEX_PROFILE_SCOPE("Scene.OpenQueryView.Snapshot.Serial");
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
		} IDX_BINDING_CATCH(0);
	}

	static int Index_Scene_OpenQueryView(
		const char* sceneName,
		const char* writeNames,
		const char* readonlyNames,
		const char* mustHaveNames,
		const char* withoutNames,
		int enableFilter,
		void** outPointers,
		int maxRows)
	{
		return Index_Scene_OpenQueryViewImpl(
			sceneName, writeNames, readonlyNames, mustHaveNames, withoutNames,
			enableFilter, outPointers, nullptr, maxRows);
	}

	static int Index_Scene_OpenQueryViewWithEntities(
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
		return Index_Scene_OpenQueryViewImpl(
			sceneName, writeNames, readonlyNames, mustHaveNames, withoutNames,
			enableFilter, outPointers, outEntityIDs, maxRows);
	}

	static int Index_Scene_QueryEntitiesFilteredInScene(
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

	static int Index_NameComponent_GetNameBuffer(uint64_t entityID, char* outBuffer, int capacity)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle) || !scene->HasComponent<NameComponent>(handle)) {
			return CopyCStringToBuffer("", outBuffer, capacity);
		}

		return CopyCStringToBuffer(scene->GetComponent<NameComponent>(handle).Name.c_str(), outBuffer, capacity);
	}

	static void Index_NameComponent_SetName(uint64_t entityID, const char* name)
	{
		IDX_BINDING_TRY {
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
		} IDX_BINDING_CATCH_VOID
	}

	// ── Entity hierarchy ──

	static uint64_t Index_Entity_GetParent(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		Entity parent = scene->GetEntity(handle).GetParent();
		if (!parent.IsValid()) return 0;
		return GetEntityScriptId(*scene, parent.GetHandle());
	}

	static int Index_Entity_SetParent(uint64_t entityID, uint64_t parentEntityID, int worldPositionStays)
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
				IDX_CORE_WARN_TAG("ScriptBinding", "Entity.SetParent across scenes is not supported");
				return 0;
			}
			newParent = scene->GetEntity(parentHandle);
		}

#if INDEX_WITH_2D
		if (worldPositionStays != 0 && SetParentKeepingWorld2D(*scene, handle, newParent)) return 1;
#else
		(void)worldPositionStays;
#endif
		entity.SetParent(newParent);
		return 1;
	}

	static int Index_Entity_GetChildCount(uint64_t entityID)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;
		return static_cast<int>(scene->GetEntity(handle).GetChildren().size());
	}

	static uint64_t Index_Entity_GetChildAt(uint64_t entityID, int index)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle)) return 0;

		const auto& children = scene->GetEntity(handle).GetChildren();
		if (index < 0 || static_cast<size_t>(index) >= children.size()) return 0;
		return GetEntityScriptId(*scene, children[static_cast<size_t>(index)]);
	}

	static int Index_Entity_GetChildren(uint64_t entityID, uint64_t* outIDs, int maxOut)
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
	static int Index_UI_GetHoveredEntity(uint64_t* outEntityID) {
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

	static uint32_t Index_Component_RegisterDynamic(
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

	static void Index_Component_UnregisterAllDynamic()
	{
		auto& registry = SceneManager::GetComponentRegistry();
		registry.UnregisterAllDynamic();
	}

	// ── TextRenderer ────────────────────────────────────────────────────

	static int Index_TextRenderer_GetTextBuffer(uint64_t entityID, char* outBuffer, int capacity)
	{
		Scene* scene = nullptr;
		EntityHandle handle = entt::null;
		if (!ResolveEntityReference(entityID, scene, handle) || !scene->HasComponent<TextRendererComponent>(handle)) {
			return CopyCStringToBuffer("", outBuffer, capacity);
		}
		return CopyCStringToBuffer(scene->GetComponent<TextRendererComponent>(handle).Text.c_str(), outBuffer, capacity);
	}

	static void Index_TextRenderer_SetText(uint64_t entityID, const char* text)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.Text = text ? text : "";
	}

	static uint64_t Index_TextRenderer_GetFont(uint64_t entityID)
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

	static void Index_TextRenderer_SetFont(uint64_t entityID, uint64_t assetId)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );

		comp.FontAssetId = UUID(assetId);
		// Force the renderer to re-resolve next frame at the current FontSize.
		comp.ResolvedFont = FontHandle{};
	}

	static float Index_TextRenderer_GetFontSize(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 32.0f);
		return comp.FontSize;
	}

	static void Index_TextRenderer_SetFontSize(uint64_t entityID, float size)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.FontSize = size;
		// A new pixel size needs a different atlas slot — invalidate the cache.
		comp.ResolvedFont = FontHandle{};
	}

	static void Index_TextRenderer_GetColor(uint64_t entityID, float* r, float* g, float* b, float* a)
	{
		GET_COMPONENT(TextRendererComponent, entityID, (void)(*r = 1, *g = 1, *b = 1, *a = 1));
		*r = comp.Color.r; *g = comp.Color.g; *b = comp.Color.b; *a = comp.Color.a;
	}

	static void Index_TextRenderer_SetColor(uint64_t entityID, float r, float g, float b, float a)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.Color = { r, g, b, a };
	}

	static float Index_TextRenderer_GetLetterSpacing(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0.0f);
		return comp.LetterSpacing;
	}

	static void Index_TextRenderer_SetLetterSpacing(uint64_t entityID, float spacing)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.LetterSpacing = spacing;
	}

	static float Index_TextRenderer_GetLineSpacing(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0.0f);
		return comp.LineSpacing;
	}

	static void Index_TextRenderer_SetLineSpacing(uint64_t entityID, float spacing)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.LineSpacing = spacing;
	}

	static int Index_TextRenderer_GetHAlign(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);
		return static_cast<int>(comp.HAlign);
	}

	static void Index_TextRenderer_SetHAlign(uint64_t entityID, int align)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.HAlign = static_cast<TextAlignment>(align);
	}

	static int Index_TextRenderer_GetVAlign(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);
		return static_cast<int>(comp.VAlign);
	}

	static void Index_TextRenderer_SetVAlign(uint64_t entityID, int align)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.VAlign = static_cast<TextVerticalAlignment>(align);
	}

	static int Index_TextRenderer_GetWrapMode(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);
		return static_cast<int>(comp.WrapMode);
	}

	static void Index_TextRenderer_SetWrapMode(uint64_t entityID, int mode)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.WrapMode = static_cast<TextWrapMode>(mode);
	}

	// WrapWidth get/set thunks removed alongside the field. The C#
	// TextRenderer.WrapWidth property is gone too; use Margin to
	// inset wrapped text.

	static int Index_TextRenderer_GetSortingOrder(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);
		return comp.SortingOrder;
	}

	static void Index_TextRenderer_SetSortingOrder(uint64_t entityID, int order)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.SortingOrder = static_cast<int16_t>(order);
	}

	static int Index_TextRenderer_GetSortingLayer(uint64_t entityID)
	{
		GET_COMPONENT(TextRendererComponent, entityID, 0);
		return comp.SortingLayer;
	}

	static void Index_TextRenderer_SetSortingLayer(uint64_t entityID, int layer)
	{
		GET_COMPONENT(TextRendererComponent, entityID, );
		comp.SortingLayer = static_cast<uint8_t>(layer);
	}

	// ── AudioSource ─────────────────────────────────────────────────────

	static void Index_AudioSource_Play(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::PlayAudioSource(comp); }
	static void Index_AudioSource_Pause(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::PauseAudioSource(comp); }
	static void Index_AudioSource_Stop(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::StopAudioSource(comp); }
	static void Index_AudioSource_Resume(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, ); AudioManager::ResumeAudioSource(comp); }

	static float Index_AudioSource_GetVolume(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 1.0f); return comp.GetVolume(); }
	static void  Index_AudioSource_SetVolume(uint64_t entityID, float volume) { GET_COMPONENT(AudioSourceComponent, entityID, ); comp.SetVolume(std::clamp(volume, 0.0f, 1.0f)); }
	static float Index_AudioSource_GetPitch(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 1.0f); return comp.GetPitch(); }
	static void  Index_AudioSource_SetPitch(uint64_t entityID, float pitch) { GET_COMPONENT(AudioSourceComponent, entityID, ); comp.SetPitch(std::clamp(pitch, 0.1f, 3.0f)); }
	static int   Index_AudioSource_GetLoop(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 0); return comp.IsLooping() ? 1 : 0; }
	static void  Index_AudioSource_SetLoop(uint64_t entityID, int loop) { GET_COMPONENT(AudioSourceComponent, entityID, ); comp.SetLoop(loop != 0); }
	static int   Index_AudioSource_IsPlaying(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 0); return comp.IsPlaying() ? 1 : 0; }
	static int   Index_AudioSource_IsPaused(uint64_t entityID) { GET_COMPONENT(AudioSourceComponent, entityID, 0); return comp.IsPaused() ? 1 : 0; }

	static uint64_t Index_AudioSource_GetAudio(uint64_t entityID)
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

	static void Index_AudioSource_SetAudio(uint64_t entityID, uint64_t assetId)
	{
		GET_COMPONENT(AudioSourceComponent, entityID, );

		if (assetId == 0) {
			comp.SetAudioHandle(AudioHandle(), UUID(0));
			return;
		}

		comp.SetAudioHandle(AudioManager::LoadAudioByUUID(assetId), UUID(assetId));
	}

	// ── Gizmos ──────────────────────────────────────────────────────────

	static void Index_Gizmo_DrawLine(float x1, float y1, float x2, float y2) { Gizmo::DrawLine({ x1, y1 }, { x2, y2 }); }
	static void Index_Gizmo_DrawWireSquare(float cx, float cy, float sx, float sy, float degrees) { Gizmo::DrawWireSquare({ cx, cy }, { sx, sy }, degrees); }
	static void Index_Gizmo_DrawWireCircle(float cx, float cy, float radius, int segments) { Gizmo::DrawWireCircle({ cx, cy }, radius, segments); }
	static void Index_Gizmo_DrawSquare(float cx, float cy, float sx, float sy, float degrees) { Gizmo::DrawSquare({ cx, cy }, { sx, sy }, degrees); }
	static void Index_Gizmo_DrawCircle(float cx, float cy, float radius, int segments) { Gizmo::DrawCircle({ cx, cy }, radius, segments); }
	static void Index_Gizmo_DrawText(const char* text, float x, float y, float rotation, float size) { Gizmo::DrawText(text ? text : "", { x, y }, rotation, size); }
	static void Index_Gizmo_SetColor(float r, float g, float b, float a) { Gizmo::SetColor(Color(r, g, b, a)); }
	static void Index_Gizmo_GetColor(float* r, float* g, float* b, float* a) { Color c = Gizmo::GetColor(); *r = c.r; *g = c.g; *b = c.b; *a = c.a; }
	static float Index_Gizmo_GetLineWidth() { return Gizmo::GetLineWidth(); }
	static void Index_Gizmo_SetLineWidth(float width) { Gizmo::SetLineWidth(width); }
	static void Index_Gizmo_SetMaxVertices(int maxVertices) { Gizmo::SetMaxVertices(static_cast<size_t>(maxVertices < 0 ? 0 : maxVertices)); }
	static int Index_Gizmo_GetMaxVertices() { return static_cast<int>(Gizmo::GetMaxVertices()); }
	static int Index_Gizmo_GetRegisteredVertices() { return static_cast<int>(Gizmo::GetRegisteredVertices()); }
		static int Index_Gizmo_GetEnabled() { return Gizmo::IsEnabled() ? 1 : 0; }
		static void Index_Gizmo_SetEnabled(int enabled) { Gizmo::SetEnabled(enabled != 0); }

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
		const IndexProject* project = ProjectManager::GetCurrentProject();
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

	static void Index_RectTransform_GetScreenPosition(uint64_t entityID, float* outX, float* outY) {
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

	static void Index_RectTransform_SetScreenPosition(uint64_t entityID, float x, float y) {
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

	static void Index_RectTransform_GetAnchorMin(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 0.5f, *outY = 0.5f));
		*outX = comp.AnchorMin.x; *outY = comp.AnchorMin.y;
	}
	static void Index_RectTransform_SetAnchorMin(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.AnchorMin = { x, y };
	}
	static void Index_RectTransform_GetAnchorMax(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 0.5f, *outY = 0.5f));
		*outX = comp.AnchorMax.x; *outY = comp.AnchorMax.y;
	}
	static void Index_RectTransform_SetAnchorMax(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.AnchorMax = { x, y };
	}
	static void Index_RectTransform_GetPivot(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 0.5f, *outY = 0.5f));
		*outX = comp.Pivot.x; *outY = comp.Pivot.y;
	}
	static void Index_RectTransform_SetPivot(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.Pivot = { x, y };
	}
	static void Index_RectTransform_GetAnchoredPosition(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 0.0f, *outY = 0.0f));
		*outX = comp.AnchoredPosition.x; *outY = comp.AnchoredPosition.y;
	}
	static void Index_RectTransform_SetAnchoredPosition(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.AnchoredPosition = { x, y };
	}
	static void Index_RectTransform_GetSizeDelta(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 100.0f, *outY = 100.0f));
		*outX = comp.SizeDelta.x; *outY = comp.SizeDelta.y;
	}
	static void Index_RectTransform_SetSizeDelta(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.SizeDelta = { x, y };
	}
	// Get* returns world value (set by UILayoutSystem); Set* writes Local* — world writes would be overwritten on next layout pass.
	static float Index_RectTransform_GetRotation(uint64_t entityID) {
		GET_COMPONENT(RectTransform2DComponent, entityID, 0.0f);
		return comp.Rotation;
	}
	static void Index_RectTransform_SetRotation(uint64_t entityID, float rotation) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.LocalRotation = rotation;
	}
	static void Index_RectTransform_GetScale(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 1.0f, *outY = 1.0f));
		*outX = comp.Scale.x; *outY = comp.Scale.y;
	}
	static void Index_RectTransform_SetScale(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.LocalScale = { x, y };
	}
	static float Index_RectTransform_GetLocalRotation(uint64_t entityID) {
		GET_COMPONENT(RectTransform2DComponent, entityID, 0.0f);
		return comp.LocalRotation;
	}
	static void Index_RectTransform_SetLocalRotation(uint64_t entityID, float rotation) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.LocalRotation = rotation;
	}
	static void Index_RectTransform_GetLocalScale(uint64_t entityID, float* outX, float* outY) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outX = 1.0f, *outY = 1.0f));
		*outX = comp.LocalScale.x; *outY = comp.LocalScale.y;
	}
	static void Index_RectTransform_SetLocalScale(uint64_t entityID, float x, float y) {
		GET_COMPONENT(RectTransform2DComponent, entityID, );
		comp.LocalScale = { x, y };
	}
	static void Index_RectTransform_GetResolvedSize(uint64_t entityID, float* outW, float* outH) {
		GET_COMPONENT(RectTransform2DComponent, entityID, (void)(*outW = 0.0f, *outH = 0.0f));
		const Vec2 size = comp.GetSize();
		*outW = size.x; *outH = size.y;
	}

	// ── UI: Image ───────────────────────────────────────────────────────

	static void Index_Image_GetColor(uint64_t entityID, float* r, float* g, float* b, float* a) {
		GET_COMPONENT(ImageComponent, entityID, (void)(*r = 1, *g = 1, *b = 1, *a = 1));
		*r = comp.Color.r; *g = comp.Color.g; *b = comp.Color.b; *a = comp.Color.a;
	}
	static void Index_Image_SetColor(uint64_t entityID, float r, float g, float b, float a) {
		GET_COMPONENT(ImageComponent, entityID, );
		comp.Color = Color{ r, g, b, a };
	}
	static uint64_t Index_Image_GetTexture(uint64_t entityID) {
		GET_COMPONENT(ImageComponent, entityID, 0ull);
		return static_cast<uint64_t>(comp.TextureAssetId);
	}
	static void Index_Image_SetTexture(uint64_t entityID, uint64_t assetId) {
		GET_COMPONENT(ImageComponent, entityID, );
		comp.TextureAssetId = UUID(assetId);
		comp.TextureHandle = (assetId != 0)
			? TextureManager::LoadTextureByUUID(assetId)
			: TextureHandle{};
	}
	static int Index_Image_GetSortingOrder(uint64_t entityID) {
		GET_COMPONENT(ImageComponent, entityID, 0);
		return comp.SortingOrder;
	}
	static void Index_Image_SetSortingOrder(uint64_t entityID, int order) {
		GET_COMPONENT(ImageComponent, entityID, );
		comp.SortingOrder = static_cast<int16_t>(order);
	}
	static int Index_Image_GetSortingLayer(uint64_t entityID) {
		GET_COMPONENT(ImageComponent, entityID, 0);
		return comp.SortingLayer;
	}
	static void Index_Image_SetSortingLayer(uint64_t entityID, int layer) {
		GET_COMPONENT(ImageComponent, entityID, );
		comp.SortingLayer = static_cast<uint8_t>(layer);
	}

	// ── UI: Interactable ────────────────────────────────────────────────

	static int Index_Interactable_GetInteractable(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.Interactable ? 1 : 0;
	}
	static void Index_Interactable_SetInteractable(uint64_t entityID, int value) {
		GET_COMPONENT(InteractableComponent, entityID, );
		comp.Interactable = value != 0;
	}
	static int Index_Interactable_GetIsHovered(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.IsHovered ? 1 : 0;
	}
	static int Index_Interactable_GetIsClicked(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.IsClicked ? 1 : 0;
	}
	static int Index_Interactable_GetIsPressed(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.IsPressed ? 1 : 0;
	}
	static int Index_Interactable_GetIsMouseDown(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.IsMouseDown ? 1 : 0;
	}
	static int Index_Interactable_GetIsMouseUp(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.IsMouseUp ? 1 : 0;
	}
	static int Index_Interactable_GetFocusable(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.Focusable ? 1 : 0;
	}
	static void Index_Interactable_SetFocusable(uint64_t entityID, int value) {
		GET_COMPONENT(InteractableComponent, entityID, );
		comp.Focusable = value != 0;
		// When opting out at runtime, drop any leftover focus state so
		// the next UIFocusSystem tick doesn't see a stale "I'm focused"
		// flag on a non-focusable widget.
		if (!comp.Focusable) {
			comp.IsFocused = false;
		}
	}
	static int Index_Interactable_GetIsFocused(uint64_t entityID) {
		GET_COMPONENT(InteractableComponent, entityID, 0);
		return comp.IsFocused ? 1 : 0;
	}
	static void Index_Interactable_SetIsFocused(uint64_t entityID, int value) {
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

	WIDGET_COLOR_BINDING(ButtonComponent, NormalColor,   Index_Button_GetNormalColor,   Index_Button_SetNormalColor)
	WIDGET_COLOR_BINDING(ButtonComponent, HoveredColor,  Index_Button_GetHoveredColor,  Index_Button_SetHoveredColor)
	WIDGET_COLOR_BINDING(ButtonComponent, PressedColor,  Index_Button_GetPressedColor,  Index_Button_SetPressedColor)
	WIDGET_COLOR_BINDING(ButtonComponent, DisabledColor, Index_Button_GetDisabledColor, Index_Button_SetDisabledColor)

	WIDGET_COLOR_BINDING(SliderComponent, NormalColor,   Index_Slider_GetNormalColor,   Index_Slider_SetNormalColor)
	WIDGET_COLOR_BINDING(SliderComponent, HoveredColor,  Index_Slider_GetHoveredColor,  Index_Slider_SetHoveredColor)
	WIDGET_COLOR_BINDING(SliderComponent, PressedColor,  Index_Slider_GetPressedColor,  Index_Slider_SetPressedColor)
	WIDGET_COLOR_BINDING(SliderComponent, DisabledColor, Index_Slider_GetDisabledColor, Index_Slider_SetDisabledColor)

	WIDGET_COLOR_BINDING(ToggleComponent, NormalColor,   Index_Toggle_GetNormalColor,   Index_Toggle_SetNormalColor)
	WIDGET_COLOR_BINDING(ToggleComponent, HoveredColor,  Index_Toggle_GetHoveredColor,  Index_Toggle_SetHoveredColor)
	WIDGET_COLOR_BINDING(ToggleComponent, PressedColor,  Index_Toggle_GetPressedColor,  Index_Toggle_SetPressedColor)
	WIDGET_COLOR_BINDING(ToggleComponent, DisabledColor, Index_Toggle_GetDisabledColor, Index_Toggle_SetDisabledColor)

	WIDGET_COLOR_BINDING(InputFieldComponent, NormalColor,   Index_InputField_GetNormalColor,   Index_InputField_SetNormalColor)
	WIDGET_COLOR_BINDING(InputFieldComponent, HoveredColor,  Index_InputField_GetHoveredColor,  Index_InputField_SetHoveredColor)
	WIDGET_COLOR_BINDING(InputFieldComponent, PressedColor,  Index_InputField_GetPressedColor,  Index_InputField_SetPressedColor)
	WIDGET_COLOR_BINDING(InputFieldComponent, DisabledColor, Index_InputField_GetDisabledColor, Index_InputField_SetDisabledColor)

	WIDGET_COLOR_BINDING(DropdownComponent, NormalColor,   Index_Dropdown_GetNormalColor,   Index_Dropdown_SetNormalColor)
	WIDGET_COLOR_BINDING(DropdownComponent, HoveredColor,  Index_Dropdown_GetHoveredColor,  Index_Dropdown_SetHoveredColor)
	WIDGET_COLOR_BINDING(DropdownComponent, PressedColor,  Index_Dropdown_GetPressedColor,  Index_Dropdown_SetPressedColor)
	WIDGET_COLOR_BINDING(DropdownComponent, DisabledColor, Index_Dropdown_GetDisabledColor, Index_Dropdown_SetDisabledColor)

	// Optional focus-state tint — alpha == 0 sentinel = "skip", so
	// scripts can read / write a fully-zeroed color to opt out of any
	// visual focus indicator without affecting navigation.
	WIDGET_COLOR_BINDING(ButtonComponent,    FocusedColor, Index_Button_GetFocusedColor,    Index_Button_SetFocusedColor)
	WIDGET_COLOR_BINDING(ToggleComponent,    FocusedColor, Index_Toggle_GetFocusedColor,    Index_Toggle_SetFocusedColor)
	WIDGET_COLOR_BINDING(SliderComponent,    FocusedColor, Index_Slider_GetFocusedColor,    Index_Slider_SetFocusedColor)
	WIDGET_COLOR_BINDING(InputFieldComponent,FocusedColor, Index_InputField_GetFocusedColor,Index_InputField_SetFocusedColor)
	WIDGET_COLOR_BINDING(DropdownComponent,  FocusedColor, Index_Dropdown_GetFocusedColor,  Index_Dropdown_SetFocusedColor)
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

	WIDGET_TRANSITIONMODE_BINDING(ButtonComponent,    Index_Button_GetTransitionMode,    Index_Button_SetTransitionMode)
	WIDGET_TRANSITIONMODE_BINDING(ToggleComponent,    Index_Toggle_GetTransitionMode,    Index_Toggle_SetTransitionMode)
	WIDGET_TRANSITIONMODE_BINDING(SliderComponent,    Index_Slider_GetTransitionMode,    Index_Slider_SetTransitionMode)
	WIDGET_TRANSITIONMODE_BINDING(InputFieldComponent,Index_InputField_GetTransitionMode,Index_InputField_SetTransitionMode)
	WIDGET_TRANSITIONMODE_BINDING(DropdownComponent,  Index_Dropdown_GetTransitionMode,  Index_Dropdown_SetTransitionMode)

	WIDGET_SPRITE_BINDING(ButtonComponent, NormalSprite,   Index_Button_GetNormalSprite,   Index_Button_SetNormalSprite)
	WIDGET_SPRITE_BINDING(ButtonComponent, HoveredSprite,  Index_Button_GetHoveredSprite,  Index_Button_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(ButtonComponent, PressedSprite,  Index_Button_GetPressedSprite,  Index_Button_SetPressedSprite)
	WIDGET_SPRITE_BINDING(ButtonComponent, DisabledSprite, Index_Button_GetDisabledSprite, Index_Button_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(ButtonComponent, FocusedSprite,  Index_Button_GetFocusedSprite,  Index_Button_SetFocusedSprite)

	WIDGET_SPRITE_BINDING(ToggleComponent, NormalSprite,   Index_Toggle_GetNormalSprite,   Index_Toggle_SetNormalSprite)
	WIDGET_SPRITE_BINDING(ToggleComponent, HoveredSprite,  Index_Toggle_GetHoveredSprite,  Index_Toggle_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(ToggleComponent, PressedSprite,  Index_Toggle_GetPressedSprite,  Index_Toggle_SetPressedSprite)
	WIDGET_SPRITE_BINDING(ToggleComponent, DisabledSprite, Index_Toggle_GetDisabledSprite, Index_Toggle_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(ToggleComponent, FocusedSprite,  Index_Toggle_GetFocusedSprite,  Index_Toggle_SetFocusedSprite)

	WIDGET_SPRITE_BINDING(SliderComponent, NormalSprite,   Index_Slider_GetNormalSprite,   Index_Slider_SetNormalSprite)
	WIDGET_SPRITE_BINDING(SliderComponent, HoveredSprite,  Index_Slider_GetHoveredSprite,  Index_Slider_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(SliderComponent, PressedSprite,  Index_Slider_GetPressedSprite,  Index_Slider_SetPressedSprite)
	WIDGET_SPRITE_BINDING(SliderComponent, DisabledSprite, Index_Slider_GetDisabledSprite, Index_Slider_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(SliderComponent, FocusedSprite,  Index_Slider_GetFocusedSprite,  Index_Slider_SetFocusedSprite)

	WIDGET_SPRITE_BINDING(InputFieldComponent, NormalSprite,   Index_InputField_GetNormalSprite,   Index_InputField_SetNormalSprite)
	WIDGET_SPRITE_BINDING(InputFieldComponent, HoveredSprite,  Index_InputField_GetHoveredSprite,  Index_InputField_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(InputFieldComponent, PressedSprite,  Index_InputField_GetPressedSprite,  Index_InputField_SetPressedSprite)
	WIDGET_SPRITE_BINDING(InputFieldComponent, DisabledSprite, Index_InputField_GetDisabledSprite, Index_InputField_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(InputFieldComponent, FocusedSprite,  Index_InputField_GetFocusedSprite,  Index_InputField_SetFocusedSprite)

	WIDGET_SPRITE_BINDING(DropdownComponent, NormalSprite,   Index_Dropdown_GetNormalSprite,   Index_Dropdown_SetNormalSprite)
	WIDGET_SPRITE_BINDING(DropdownComponent, HoveredSprite,  Index_Dropdown_GetHoveredSprite,  Index_Dropdown_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(DropdownComponent, PressedSprite,  Index_Dropdown_GetPressedSprite,  Index_Dropdown_SetPressedSprite)
	WIDGET_SPRITE_BINDING(DropdownComponent, DisabledSprite, Index_Dropdown_GetDisabledSprite, Index_Dropdown_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(DropdownComponent, FocusedSprite,  Index_Dropdown_GetFocusedSprite,  Index_Dropdown_SetFocusedSprite)
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

	WIDGET_BOOL_BINDING(ToggleComponent,   IsReadOnly, Index_Toggle_GetIsReadOnly,   Index_Toggle_SetIsReadOnly)
	WIDGET_BOOL_BINDING(SliderComponent,   IsReadOnly, Index_Slider_GetIsReadOnly,   Index_Slider_SetIsReadOnly)
	WIDGET_BOOL_BINDING(DropdownComponent, IsReadOnly, Index_Dropdown_GetIsReadOnly, Index_Dropdown_SetIsReadOnly)

	WIDGET_ENTITYREF_BINDING(ButtonComponent,     TargetGraphic,    Index_Button_GetTargetGraphic,        Index_Button_SetTargetGraphic)
	WIDGET_ENTITYREF_BINDING(SliderComponent,     FillEntity,       Index_Slider_GetFillEntity,           Index_Slider_SetFillEntity)
	WIDGET_ENTITYREF_BINDING(SliderComponent,     HandleEntity,     Index_Slider_GetHandleEntity,         Index_Slider_SetHandleEntity)
	WIDGET_ENTITYREF_BINDING(SliderComponent,     BackgroundEntity, Index_Slider_GetBackgroundEntity,     Index_Slider_SetBackgroundEntity)
	WIDGET_ENTITYREF_BINDING(ToggleComponent,     CheckmarkEntity,  Index_Toggle_GetCheckmarkEntity,      Index_Toggle_SetCheckmarkEntity)
	WIDGET_ENTITYREF_BINDING(InputFieldComponent, TextEntity,       Index_InputField_GetTextEntity,       Index_InputField_SetTextEntity)
	WIDGET_ENTITYREF_BINDING(InputFieldComponent, VerticalScrollbarEntity, Index_InputField_GetVerticalScrollbarEntity, Index_InputField_SetVerticalScrollbarEntity)
	WIDGET_ENTITYREF_BINDING(DropdownComponent,   LabelEntity,      Index_Dropdown_GetLabelEntity,        Index_Dropdown_SetLabelEntity)

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

	WIDGET_COLOR_BINDING(DropdownComponent, OptionNormalColor,    Index_Dropdown_GetOptionNormalColor,    Index_Dropdown_SetOptionNormalColor)
	WIDGET_COLOR_BINDING(DropdownComponent, OptionHoverColor,     Index_Dropdown_GetOptionHoverColor,     Index_Dropdown_SetOptionHoverColor)
	WIDGET_COLOR_BINDING(DropdownComponent, OptionPressedColor,   Index_Dropdown_GetOptionPressedColor,   Index_Dropdown_SetOptionPressedColor)
	WIDGET_COLOR_BINDING(DropdownComponent, OptionSelectedColor,  Index_Dropdown_GetOptionSelectedColor,  Index_Dropdown_SetOptionSelectedColor)
	WIDGET_COLOR_BINDING(DropdownComponent, PopupBackgroundColor, Index_Dropdown_GetPopupBackgroundColor, Index_Dropdown_SetPopupBackgroundColor)
	WIDGET_COLOR_BINDING(DropdownComponent, OptionTextColor,      Index_Dropdown_GetOptionTextColor,      Index_Dropdown_SetOptionTextColor)
	#undef WIDGET_COLOR_BINDING
	#undef WIDGET_BOOL_BINDING
	#undef WIDGET_ENTITYREF_BINDING

	// ── UI: Slider ──────────────────────────────────────────────────────

	static float Index_Slider_GetValue(uint64_t entityID) {
		GET_COMPONENT(SliderComponent, entityID, 0.0f);
		return comp.Value;
	}
	static void Index_Slider_SetValue(uint64_t entityID, float value) {
		GET_COMPONENT(SliderComponent, entityID, );
		comp.Value = std::clamp(value, std::min(comp.MinValue, comp.MaxValue), std::max(comp.MinValue, comp.MaxValue));
	}
	static float Index_Slider_GetMinValue(uint64_t entityID) {
		GET_COMPONENT(SliderComponent, entityID, 0.0f);
		return comp.MinValue;
	}
	static void Index_Slider_SetMinValue(uint64_t entityID, float value) {
		GET_COMPONENT(SliderComponent, entityID, );
		comp.MinValue = value;
	}
	static float Index_Slider_GetMaxValue(uint64_t entityID) {
		GET_COMPONENT(SliderComponent, entityID, 1.0f);
		return comp.MaxValue;
	}
	static void Index_Slider_SetMaxValue(uint64_t entityID, float value) {
		GET_COMPONENT(SliderComponent, entityID, );
		comp.MaxValue = value;
	}
	static int Index_Slider_GetWholeNumbers(uint64_t entityID) {
		GET_COMPONENT(SliderComponent, entityID, 0);
		return comp.WholeNumbers ? 1 : 0;
	}
	static void Index_Slider_SetWholeNumbers(uint64_t entityID, int value) {
		GET_COMPONENT(SliderComponent, entityID, );
		comp.WholeNumbers = value != 0;
	}
	static int Index_Slider_GetValueChangedThisFrame(uint64_t entityID) {
		GET_COMPONENT(SliderComponent, entityID, 0);
		return comp.ValueChangedThisFrame ? 1 : 0;
	}
	// Prevents UIEventSystem from double-firing OnValueChanged when C# SetValue fires immediately before the tick diff.
	static void Index_Slider_MarkValueObserved(uint64_t entityID) {
		GET_COMPONENT(SliderComponent, entityID, );
		comp.LastObservedValue = comp.Value;
		comp.ValueObserved = true;
	}

	// ── UI: Toggle ──────────────────────────────────────────────────────

	static int Index_Toggle_GetIsOn(uint64_t entityID) {
		GET_COMPONENT(ToggleComponent, entityID, 0);
		return comp.IsOn ? 1 : 0;
	}
	static void Index_Toggle_SetIsOn(uint64_t entityID, int value) {
		GET_COMPONENT(ToggleComponent, entityID, );
		comp.IsOn = value != 0;
	}
	static int Index_Toggle_GetValueChangedThisFrame(uint64_t entityID) {
		GET_COMPONENT(ToggleComponent, entityID, 0);
		return comp.ValueChangedThisFrame ? 1 : 0;
	}
	// See Index_Slider_MarkValueObserved — same role for Toggle.IsOn.
	static void Index_Toggle_MarkIsOnObserved(uint64_t entityID) {
		GET_COMPONENT(ToggleComponent, entityID, );
		comp.LastObservedIsOn = comp.IsOn;
		comp.ValueObserved = true;
	}

	// ── UI: InputField ──────────────────────────────────────────────────

	static int Index_InputField_GetTextBuffer(uint64_t entityID, char* outBuffer, int capacity) {
		GET_COMPONENT(InputFieldComponent, entityID, CopyStringToBuffer({}, outBuffer, capacity));
		return CopyStringToBuffer(comp.Text, outBuffer, capacity);
	}
	static void Index_InputField_SetText(uint64_t entityID, const char* text) {
		GET_COMPONENT(InputFieldComponent, entityID, );
		comp.Text = text ? text : "";
	}
	static int Index_InputField_GetPlaceholderTextBuffer(uint64_t entityID, char* outBuffer, int capacity) {
		GET_COMPONENT(InputFieldComponent, entityID, CopyStringToBuffer({}, outBuffer, capacity));
		return CopyStringToBuffer(comp.PlaceholderText, outBuffer, capacity);
	}
	static void Index_InputField_SetPlaceholderText(uint64_t entityID, const char* text) {
		GET_COMPONENT(InputFieldComponent, entityID, );
		comp.PlaceholderText = text ? text : "";
	}
	static int Index_InputField_GetIsFocused(uint64_t entityID) {
		GET_COMPONENT(InputFieldComponent, entityID, 0);
		return comp.IsFocused ? 1 : 0;
	}
	static void Index_InputField_SetIsFocused(uint64_t entityID, int value) {
		GET_COMPONENT(InputFieldComponent, entityID, );
		comp.IsFocused = value != 0;
	}
	static int Index_InputField_GetSubmittedThisFrame(uint64_t entityID) {
		GET_COMPONENT(InputFieldComponent, entityID, 0);
		return comp.SubmittedThisFrame ? 1 : 0;
	}
	static int Index_InputField_GetCharacterLimit(uint64_t entityID) {
		GET_COMPONENT(InputFieldComponent, entityID, 0);
		return comp.CharacterLimit;
	}
	static void Index_InputField_SetCharacterLimit(uint64_t entityID, int value) {
		GET_COMPONENT(InputFieldComponent, entityID, );
		comp.CharacterLimit = value;
	}
	static int Index_InputField_GetMultiline(uint64_t entityID) {
		GET_COMPONENT(InputFieldComponent, entityID, 0);
		return comp.Multiline ? 1 : 0;
	}
	static void Index_InputField_SetMultiline(uint64_t entityID, int value) {
		GET_COMPONENT(InputFieldComponent, entityID, );
		comp.Multiline = value != 0;
	}

	// ── UI: Dropdown ────────────────────────────────────────────────────

	static int Index_Dropdown_GetSelectedIndex(uint64_t entityID) {
		GET_COMPONENT(DropdownComponent, entityID, 0);
		return comp.SelectedIndex;
	}
	static void Index_Dropdown_SetSelectedIndex(uint64_t entityID, int value) {
		GET_COMPONENT(DropdownComponent, entityID, );
		comp.SelectedIndex = value;
	}
	static int Index_Dropdown_GetIsOpen(uint64_t entityID) {
		GET_COMPONENT(DropdownComponent, entityID, 0);
		return comp.IsOpen ? 1 : 0;
	}
	static void Index_Dropdown_SetIsOpen(uint64_t entityID, int value) {
		GET_COMPONENT(DropdownComponent, entityID, );
		comp.IsOpen = value != 0;
	}
	static int Index_Dropdown_GetSelectionChangedThisFrame(uint64_t entityID) {
		GET_COMPONENT(DropdownComponent, entityID, 0);
		return comp.SelectionChangedThisFrame ? 1 : 0;
	}
	// See Index_Slider_MarkValueObserved — same role for Dropdown.SelectedIndex.
	static void Index_Dropdown_MarkSelectedIndexObserved(uint64_t entityID) {
		GET_COMPONENT(DropdownComponent, entityID, );
		comp.LastObservedSelectedIndex = comp.SelectedIndex;
		comp.SelectionObserved = true;
	}
	static int Index_Dropdown_GetOptionCount(uint64_t entityID) {
		GET_COMPONENT(DropdownComponent, entityID, 0);
		return static_cast<int>(comp.Options.size());
	}
	static int Index_Dropdown_GetOptionBuffer(uint64_t entityID, int index, char* outBuffer, int capacity) {
		GET_COMPONENT(DropdownComponent, entityID, CopyStringToBuffer({}, outBuffer, capacity));
		if (index < 0 || index >= static_cast<int>(comp.Options.size())) {
			return CopyStringToBuffer({}, outBuffer, capacity);
		}
		return CopyStringToBuffer(comp.Options[index], outBuffer, capacity);
	}
	static void Index_Dropdown_SetOption(uint64_t entityID, int index, const char* text) {
		GET_COMPONENT(DropdownComponent, entityID, );
		if (index < 0 || index >= static_cast<int>(comp.Options.size())) return;
		comp.Options[index] = text ? text : "";
	}
	static void Index_Dropdown_AddOption(uint64_t entityID, const char* text) {
		GET_COMPONENT(DropdownComponent, entityID, );
		comp.Options.emplace_back(text ? text : "");
	}
	static void Index_Dropdown_RemoveOption(uint64_t entityID, int index) {
		GET_COMPONENT(DropdownComponent, entityID, );
		if (index < 0 || index >= static_cast<int>(comp.Options.size())) return;
		comp.Options.erase(comp.Options.begin() + index);
	}
	static void Index_Dropdown_ClearOptions(uint64_t entityID) {
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
	WIDGET_FLOAT_BINDING_CLAMPED(ScrollbarComponent, Value, 0.0f, 1.0f, Index_Scrollbar_GetValue, Index_Scrollbar_SetValue)
	WIDGET_FLOAT_BINDING_CLAMPED(ScrollbarComponent, Size, 0.0f, 1.0f, Index_Scrollbar_GetSize, Index_Scrollbar_SetSize)
	WIDGET_INT_BINDING_CLAMPED(ScrollbarComponent, NumberOfSteps, 0, 11, Index_Scrollbar_GetNumberOfSteps, Index_Scrollbar_SetNumberOfSteps)
	WIDGET_ENUM_BINDING (ScrollbarComponent, Direction, ScrollbarDirection, Index_Scrollbar_GetDirection, Index_Scrollbar_SetDirection)
	WIDGET_BOOL_BINDING (ScrollbarComponent, IsReadOnly,    Index_Scrollbar_GetIsReadOnly,    Index_Scrollbar_SetIsReadOnly)
	WIDGET_ENTITYREF_BINDING(ScrollbarComponent, HandleEntity, Index_Scrollbar_GetHandleEntity, Index_Scrollbar_SetHandleEntity)

	static int  Index_Scrollbar_GetValueChangedThisFrame(uint64_t entityID) {
		GET_COMPONENT(ScrollbarComponent, entityID, 0);
		return comp.ValueChangedThisFrame ? 1 : 0;
	}
	static void Index_Scrollbar_MarkValueObserved(uint64_t entityID) {
		GET_COMPONENT(ScrollbarComponent, entityID, );
		comp.LastObservedValue = comp.Value;
		comp.ValueObserved = true;
	}

	WIDGET_COLOR_BINDING(ScrollbarComponent, NormalColor,   Index_Scrollbar_GetNormalColor,   Index_Scrollbar_SetNormalColor)
	WIDGET_COLOR_BINDING(ScrollbarComponent, HoveredColor,  Index_Scrollbar_GetHoveredColor,  Index_Scrollbar_SetHoveredColor)
	WIDGET_COLOR_BINDING(ScrollbarComponent, PressedColor,  Index_Scrollbar_GetPressedColor,  Index_Scrollbar_SetPressedColor)
	WIDGET_COLOR_BINDING(ScrollbarComponent, DisabledColor, Index_Scrollbar_GetDisabledColor, Index_Scrollbar_SetDisabledColor)
	WIDGET_COLOR_BINDING(ScrollbarComponent, FocusedColor,  Index_Scrollbar_GetFocusedColor,  Index_Scrollbar_SetFocusedColor)
	WIDGET_TRANSITIONMODE_BINDING(ScrollbarComponent, Index_Scrollbar_GetTransitionMode, Index_Scrollbar_SetTransitionMode)
	WIDGET_SPRITE_BINDING(ScrollbarComponent, NormalSprite,   Index_Scrollbar_GetNormalSprite,   Index_Scrollbar_SetNormalSprite)
	WIDGET_SPRITE_BINDING(ScrollbarComponent, HoveredSprite,  Index_Scrollbar_GetHoveredSprite,  Index_Scrollbar_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(ScrollbarComponent, PressedSprite,  Index_Scrollbar_GetPressedSprite,  Index_Scrollbar_SetPressedSprite)
	WIDGET_SPRITE_BINDING(ScrollbarComponent, DisabledSprite, Index_Scrollbar_GetDisabledSprite, Index_Scrollbar_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(ScrollbarComponent, FocusedSprite,  Index_Scrollbar_GetFocusedSprite,  Index_Scrollbar_SetFocusedSprite)

	// ── ScrollRect ──────────────────────────────────────────────────────
	WIDGET_ENTITYREF_BINDING(ScrollRectComponent, Content,             Index_ScrollRect_GetContent,             Index_ScrollRect_SetContent)
	WIDGET_ENTITYREF_BINDING(ScrollRectComponent, Viewport,            Index_ScrollRect_GetViewport,            Index_ScrollRect_SetViewport)
	WIDGET_ENTITYREF_BINDING(ScrollRectComponent, HorizontalScrollbar, Index_ScrollRect_GetHorizontalScrollbar, Index_ScrollRect_SetHorizontalScrollbar)
	WIDGET_ENTITYREF_BINDING(ScrollRectComponent, VerticalScrollbar,   Index_ScrollRect_GetVerticalScrollbar,   Index_ScrollRect_SetVerticalScrollbar)
	WIDGET_BOOL_BINDING (ScrollRectComponent, Horizontal,           Index_ScrollRect_GetHorizontal,           Index_ScrollRect_SetHorizontal)
	WIDGET_BOOL_BINDING (ScrollRectComponent, Vertical,             Index_ScrollRect_GetVertical,             Index_ScrollRect_SetVertical)
	WIDGET_BOOL_BINDING (ScrollRectComponent, Inertia,              Index_ScrollRect_GetInertia,              Index_ScrollRect_SetInertia)
	WIDGET_ENUM_BINDING (ScrollRectComponent, MovementType, ScrollRectMovementType, Index_ScrollRect_GetMovementType, Index_ScrollRect_SetMovementType)
	WIDGET_ENUM_BINDING (ScrollRectComponent, HorizontalScrollbarVisibility, ScrollbarVisibility, Index_ScrollRect_GetHorizontalScrollbarVisibility, Index_ScrollRect_SetHorizontalScrollbarVisibility)
	WIDGET_ENUM_BINDING (ScrollRectComponent, VerticalScrollbarVisibility,   ScrollbarVisibility, Index_ScrollRect_GetVerticalScrollbarVisibility,   Index_ScrollRect_SetVerticalScrollbarVisibility)
	WIDGET_FLOAT_BINDING_CLAMPED(ScrollRectComponent, Elasticity,       0.0f, 1.0f, Index_ScrollRect_GetElasticity,       Index_ScrollRect_SetElasticity)
	WIDGET_FLOAT_BINDING_CLAMPED(ScrollRectComponent, DecelerationRate, 0.0f, 1.0f, Index_ScrollRect_GetDecelerationRate, Index_ScrollRect_SetDecelerationRate)
	WIDGET_FLOAT_BINDING(ScrollRectComponent, ScrollSensitivity,           Index_ScrollRect_GetScrollSensitivity,           Index_ScrollRect_SetScrollSensitivity)
	WIDGET_FLOAT_BINDING(ScrollRectComponent, HorizontalScrollbarSpacing,  Index_ScrollRect_GetHorizontalScrollbarSpacing,  Index_ScrollRect_SetHorizontalScrollbarSpacing)
	WIDGET_FLOAT_BINDING(ScrollRectComponent, VerticalScrollbarSpacing,    Index_ScrollRect_GetVerticalScrollbarSpacing,    Index_ScrollRect_SetVerticalScrollbarSpacing)
	WIDGET_VEC2_BINDING (ScrollRectComponent, NormalizedPosition,          Index_ScrollRect_GetNormalizedPosition,          Index_ScrollRect_SetNormalizedPosition)

	static int  Index_ScrollRect_GetValueChangedThisFrame(uint64_t entityID) {
		GET_COMPONENT(ScrollRectComponent, entityID, 0);
		return comp.ValueChangedThisFrame ? 1 : 0;
	}
	static void Index_ScrollRect_MarkValueObserved(uint64_t entityID) {
		GET_COMPONENT(ScrollRectComponent, entityID, );
		comp.LastObservedNormalizedPosition = comp.NormalizedPosition;
		comp.ValueObserved = true;
	}

	// ── Mask ────────────────────────────────────────────────────────────
	WIDGET_BOOL_BINDING(MaskComponent, ShowMaskGraphic, Index_Mask_GetShowMaskGraphic, Index_Mask_SetShowMaskGraphic)

	// ── CircularSlider ──────────────────────────────────────────────────
	static float Index_CircularSlider_GetValue(uint64_t entityID) {
		GET_COMPONENT(CircularSliderComponent, entityID, 0.0f);
		return comp.Value;
	}
	static void Index_CircularSlider_SetValue(uint64_t entityID, float value) {
		GET_COMPONENT(CircularSliderComponent, entityID, );
		comp.Value = std::clamp(value, std::min(comp.MinValue, comp.MaxValue), std::max(comp.MinValue, comp.MaxValue));
	}
	WIDGET_FLOAT_BINDING(CircularSliderComponent, MinValue,          Index_CircularSlider_GetMinValue,          Index_CircularSlider_SetMinValue)
	WIDGET_FLOAT_BINDING(CircularSliderComponent, MaxValue,          Index_CircularSlider_GetMaxValue,          Index_CircularSlider_SetMaxValue)
	WIDGET_BOOL_BINDING (CircularSliderComponent, WholeNumbers,      Index_CircularSlider_GetWholeNumbers,      Index_CircularSlider_SetWholeNumbers)
	WIDGET_BOOL_BINDING (CircularSliderComponent, IsReadOnly,        Index_CircularSlider_GetIsReadOnly,        Index_CircularSlider_SetIsReadOnly)
	WIDGET_FLOAT_BINDING(CircularSliderComponent, StartAngleDegrees, Index_CircularSlider_GetStartAngleDegrees, Index_CircularSlider_SetStartAngleDegrees)
	WIDGET_FLOAT_BINDING_CLAMPED(CircularSliderComponent, SweepDegrees, 1.0f, 360.0f, Index_CircularSlider_GetSweepDegrees, Index_CircularSlider_SetSweepDegrees)
	WIDGET_BOOL_BINDING (CircularSliderComponent, Clockwise,         Index_CircularSlider_GetClockwise,         Index_CircularSlider_SetClockwise)
	WIDGET_FLOAT_BINDING_CLAMPED(CircularSliderComponent, RingThickness, 0.5f, 1024.0f, Index_CircularSlider_GetRingThickness, Index_CircularSlider_SetRingThickness)
	WIDGET_INT_BINDING_CLAMPED(CircularSliderComponent, RingSegments, 8, 256, Index_CircularSlider_GetRingSegments, Index_CircularSlider_SetRingSegments)
	WIDGET_COLOR_BINDING(CircularSliderComponent, BackgroundColor,   Index_CircularSlider_GetBackgroundColor,   Index_CircularSlider_SetBackgroundColor)
	WIDGET_COLOR_BINDING(CircularSliderComponent, FillColor,         Index_CircularSlider_GetFillColor,         Index_CircularSlider_SetFillColor)
	WIDGET_ENTITYREF_BINDING(CircularSliderComponent, HandleEntity,  Index_CircularSlider_GetHandleEntity,      Index_CircularSlider_SetHandleEntity)

	static int  Index_CircularSlider_GetValueChangedThisFrame(uint64_t entityID) {
		GET_COMPONENT(CircularSliderComponent, entityID, 0);
		return comp.ValueChangedThisFrame ? 1 : 0;
	}
	static void Index_CircularSlider_MarkValueObserved(uint64_t entityID) {
		GET_COMPONENT(CircularSliderComponent, entityID, );
		comp.LastObservedValue = comp.Value;
		comp.ValueObserved = true;
	}

	WIDGET_COLOR_BINDING(CircularSliderComponent, NormalColor,   Index_CircularSlider_GetNormalColor,   Index_CircularSlider_SetNormalColor)
	WIDGET_COLOR_BINDING(CircularSliderComponent, HoveredColor,  Index_CircularSlider_GetHoveredColor,  Index_CircularSlider_SetHoveredColor)
	WIDGET_COLOR_BINDING(CircularSliderComponent, PressedColor,  Index_CircularSlider_GetPressedColor,  Index_CircularSlider_SetPressedColor)
	WIDGET_COLOR_BINDING(CircularSliderComponent, DisabledColor, Index_CircularSlider_GetDisabledColor, Index_CircularSlider_SetDisabledColor)
	WIDGET_COLOR_BINDING(CircularSliderComponent, FocusedColor,  Index_CircularSlider_GetFocusedColor,  Index_CircularSlider_SetFocusedColor)
	WIDGET_TRANSITIONMODE_BINDING(CircularSliderComponent, Index_CircularSlider_GetTransitionMode, Index_CircularSlider_SetTransitionMode)
	WIDGET_SPRITE_BINDING(CircularSliderComponent, NormalSprite,   Index_CircularSlider_GetNormalSprite,   Index_CircularSlider_SetNormalSprite)
	WIDGET_SPRITE_BINDING(CircularSliderComponent, HoveredSprite,  Index_CircularSlider_GetHoveredSprite,  Index_CircularSlider_SetHoveredSprite)
	WIDGET_SPRITE_BINDING(CircularSliderComponent, PressedSprite,  Index_CircularSlider_GetPressedSprite,  Index_CircularSlider_SetPressedSprite)
	WIDGET_SPRITE_BINDING(CircularSliderComponent, DisabledSprite, Index_CircularSlider_GetDisabledSprite, Index_CircularSlider_SetDisabledSprite)
	WIDGET_SPRITE_BINDING(CircularSliderComponent, FocusedSprite,  Index_CircularSlider_GetFocusedSprite,  Index_CircularSlider_SetFocusedSprite)

	// ── HorizontalLayoutGroup ───────────────────────────────────────────
	WIDGET_FLOAT_BINDING(HorizontalLayoutGroupComponent, PaddingLeft,             Index_HorizontalLayoutGroup_GetPaddingLeft,             Index_HorizontalLayoutGroup_SetPaddingLeft)
	WIDGET_FLOAT_BINDING(HorizontalLayoutGroupComponent, PaddingRight,            Index_HorizontalLayoutGroup_GetPaddingRight,            Index_HorizontalLayoutGroup_SetPaddingRight)
	WIDGET_FLOAT_BINDING(HorizontalLayoutGroupComponent, PaddingTop,              Index_HorizontalLayoutGroup_GetPaddingTop,              Index_HorizontalLayoutGroup_SetPaddingTop)
	WIDGET_FLOAT_BINDING(HorizontalLayoutGroupComponent, PaddingBottom,           Index_HorizontalLayoutGroup_GetPaddingBottom,           Index_HorizontalLayoutGroup_SetPaddingBottom)
	WIDGET_FLOAT_BINDING(HorizontalLayoutGroupComponent, Spacing,                 Index_HorizontalLayoutGroup_GetSpacing,                 Index_HorizontalLayoutGroup_SetSpacing)
	WIDGET_ENUM_BINDING (HorizontalLayoutGroupComponent, ChildAlignment, UIAlignment, Index_HorizontalLayoutGroup_GetChildAlignment, Index_HorizontalLayoutGroup_SetChildAlignment)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, ReverseArrangement,      Index_HorizontalLayoutGroup_GetReverseArrangement,      Index_HorizontalLayoutGroup_SetReverseArrangement)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, ControlChildWidth,       Index_HorizontalLayoutGroup_GetControlChildWidth,       Index_HorizontalLayoutGroup_SetControlChildWidth)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, ControlChildHeight,      Index_HorizontalLayoutGroup_GetControlChildHeight,      Index_HorizontalLayoutGroup_SetControlChildHeight)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, UseChildScaleWidth,      Index_HorizontalLayoutGroup_GetUseChildScaleWidth,      Index_HorizontalLayoutGroup_SetUseChildScaleWidth)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, UseChildScaleHeight,     Index_HorizontalLayoutGroup_GetUseChildScaleHeight,     Index_HorizontalLayoutGroup_SetUseChildScaleHeight)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, ChildForceExpandWidth,   Index_HorizontalLayoutGroup_GetChildForceExpandWidth,   Index_HorizontalLayoutGroup_SetChildForceExpandWidth)
	WIDGET_BOOL_BINDING (HorizontalLayoutGroupComponent, ChildForceExpandHeight,  Index_HorizontalLayoutGroup_GetChildForceExpandHeight,  Index_HorizontalLayoutGroup_SetChildForceExpandHeight)

	// ── VerticalLayoutGroup ─────────────────────────────────────────────
	WIDGET_FLOAT_BINDING(VerticalLayoutGroupComponent, PaddingLeft,             Index_VerticalLayoutGroup_GetPaddingLeft,             Index_VerticalLayoutGroup_SetPaddingLeft)
	WIDGET_FLOAT_BINDING(VerticalLayoutGroupComponent, PaddingRight,            Index_VerticalLayoutGroup_GetPaddingRight,            Index_VerticalLayoutGroup_SetPaddingRight)
	WIDGET_FLOAT_BINDING(VerticalLayoutGroupComponent, PaddingTop,              Index_VerticalLayoutGroup_GetPaddingTop,              Index_VerticalLayoutGroup_SetPaddingTop)
	WIDGET_FLOAT_BINDING(VerticalLayoutGroupComponent, PaddingBottom,           Index_VerticalLayoutGroup_GetPaddingBottom,           Index_VerticalLayoutGroup_SetPaddingBottom)
	WIDGET_FLOAT_BINDING(VerticalLayoutGroupComponent, Spacing,                 Index_VerticalLayoutGroup_GetSpacing,                 Index_VerticalLayoutGroup_SetSpacing)
	WIDGET_ENUM_BINDING (VerticalLayoutGroupComponent, ChildAlignment, UIAlignment, Index_VerticalLayoutGroup_GetChildAlignment, Index_VerticalLayoutGroup_SetChildAlignment)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, ReverseArrangement,      Index_VerticalLayoutGroup_GetReverseArrangement,      Index_VerticalLayoutGroup_SetReverseArrangement)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, ControlChildWidth,       Index_VerticalLayoutGroup_GetControlChildWidth,       Index_VerticalLayoutGroup_SetControlChildWidth)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, ControlChildHeight,      Index_VerticalLayoutGroup_GetControlChildHeight,      Index_VerticalLayoutGroup_SetControlChildHeight)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, UseChildScaleWidth,      Index_VerticalLayoutGroup_GetUseChildScaleWidth,      Index_VerticalLayoutGroup_SetUseChildScaleWidth)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, UseChildScaleHeight,     Index_VerticalLayoutGroup_GetUseChildScaleHeight,     Index_VerticalLayoutGroup_SetUseChildScaleHeight)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, ChildForceExpandWidth,   Index_VerticalLayoutGroup_GetChildForceExpandWidth,   Index_VerticalLayoutGroup_SetChildForceExpandWidth)
	WIDGET_BOOL_BINDING (VerticalLayoutGroupComponent, ChildForceExpandHeight,  Index_VerticalLayoutGroup_GetChildForceExpandHeight,  Index_VerticalLayoutGroup_SetChildForceExpandHeight)

	// ── GridLayoutGroup ─────────────────────────────────────────────────
	WIDGET_FLOAT_BINDING(GridLayoutGroupComponent, PaddingLeft,     Index_GridLayoutGroup_GetPaddingLeft,     Index_GridLayoutGroup_SetPaddingLeft)
	WIDGET_FLOAT_BINDING(GridLayoutGroupComponent, PaddingRight,    Index_GridLayoutGroup_GetPaddingRight,    Index_GridLayoutGroup_SetPaddingRight)
	WIDGET_FLOAT_BINDING(GridLayoutGroupComponent, PaddingTop,      Index_GridLayoutGroup_GetPaddingTop,      Index_GridLayoutGroup_SetPaddingTop)
	WIDGET_FLOAT_BINDING(GridLayoutGroupComponent, PaddingBottom,   Index_GridLayoutGroup_GetPaddingBottom,   Index_GridLayoutGroup_SetPaddingBottom)
	WIDGET_VEC2_BINDING (GridLayoutGroupComponent, CellSize,        Index_GridLayoutGroup_GetCellSize,        Index_GridLayoutGroup_SetCellSize)
	WIDGET_VEC2_BINDING (GridLayoutGroupComponent, Spacing,         Index_GridLayoutGroup_GetSpacing,         Index_GridLayoutGroup_SetSpacing)
	WIDGET_ENUM_BINDING (GridLayoutGroupComponent, StartCorner,    GridLayoutStartCorner, Index_GridLayoutGroup_GetStartCorner,    Index_GridLayoutGroup_SetStartCorner)
	WIDGET_ENUM_BINDING (GridLayoutGroupComponent, StartAxis,      GridLayoutStartAxis,   Index_GridLayoutGroup_GetStartAxis,      Index_GridLayoutGroup_SetStartAxis)
	WIDGET_ENUM_BINDING (GridLayoutGroupComponent, ChildAlignment, UIAlignment,           Index_GridLayoutGroup_GetChildAlignment, Index_GridLayoutGroup_SetChildAlignment)
	WIDGET_ENUM_BINDING (GridLayoutGroupComponent, Constraint,     GridLayoutConstraint,  Index_GridLayoutGroup_GetConstraint,     Index_GridLayoutGroup_SetConstraint)
	WIDGET_INT_BINDING  (GridLayoutGroupComponent, ConstraintCount, Index_GridLayoutGroup_GetConstraintCount, Index_GridLayoutGroup_SetConstraintCount)
	WIDGET_BOOL_BINDING (GridLayoutGroupComponent, Reverse,         Index_GridLayoutGroup_GetReverse,         Index_GridLayoutGroup_SetReverse)

	// ── ContentSizeFitter ───────────────────────────────────────────────
	WIDGET_BOOL_BINDING (ContentSizeFitterComponent, HorizontalFit,  Index_ContentSizeFitter_GetHorizontalFit,  Index_ContentSizeFitter_SetHorizontalFit)
	WIDGET_BOOL_BINDING (ContentSizeFitterComponent, VerticalFit,    Index_ContentSizeFitter_GetVerticalFit,    Index_ContentSizeFitter_SetVerticalFit)
	WIDGET_FLOAT_BINDING(ContentSizeFitterComponent, PaddingLeft,    Index_ContentSizeFitter_GetPaddingLeft,    Index_ContentSizeFitter_SetPaddingLeft)
	WIDGET_FLOAT_BINDING(ContentSizeFitterComponent, PaddingRight,   Index_ContentSizeFitter_GetPaddingRight,   Index_ContentSizeFitter_SetPaddingRight)
	WIDGET_FLOAT_BINDING(ContentSizeFitterComponent, PaddingTop,     Index_ContentSizeFitter_GetPaddingTop,     Index_ContentSizeFitter_SetPaddingTop)
	WIDGET_FLOAT_BINDING(ContentSizeFitterComponent, PaddingBottom,  Index_ContentSizeFitter_GetPaddingBottom,  Index_ContentSizeFitter_SetPaddingBottom)

	// ── WidthConstraint ─────────────────────────────────────────────────
	WIDGET_FLOAT_BINDING(WidthConstraintComponent, MinWidth, Index_WidthConstraint_GetMinWidth, Index_WidthConstraint_SetMinWidth)
	WIDGET_FLOAT_BINDING(WidthConstraintComponent, MaxWidth, Index_WidthConstraint_GetMaxWidth, Index_WidthConstraint_SetMaxWidth)

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
		// ECB slots — appended at the end of NativeBindings for binary
		// compat. Wired here so the managed bindings copy picks them up
		// in the same initialization step as the rest of the surface.
		PopulateEcbBindings(b);
		// JobSystem slots — same append-for-binary-compat pattern. Wires
		// the managed Job.Schedule path into the native work-stealing
		// pool so C# work shares cores cleanly with native systems.
		PopulateJobsBindings(b);
		PopulateModuleBindings(b);

		b.Entity_Clone = IDX_GUARDED_BINDING(Index_Entity_Clone);
		b.Entity_InstantiatePrefab = IDX_GUARDED_BINDING(Index_Entity_InstantiatePrefab);
		b.Entity_GetOrigin = IDX_GUARDED_BINDING(Index_Entity_GetOrigin);
		b.Entity_GetRuntimeID = IDX_GUARDED_BINDING(Index_Entity_GetRuntimeID);
		b.Entity_GetSceneGUID = IDX_GUARDED_BINDING(Index_Entity_GetSceneGUID);
		b.Entity_GetPrefabGUID = IDX_GUARDED_BINDING(Index_Entity_GetPrefabGUID);
		b.Entity_HasComponent = IDX_GUARDED_BINDING(Index_Entity_HasComponent);
		b.Entity_AddComponent = IDX_GUARDED_BINDING(Index_Entity_AddComponent);
		b.Entity_RemoveComponent = IDX_GUARDED_BINDING(Index_Entity_RemoveComponent);
		b.Entity_AddScript = IDX_GUARDED_BINDING(Index_Entity_AddScript);
		b.Entity_HasScript = IDX_GUARDED_BINDING(Index_Entity_HasScript);
		b.Entity_RemoveScript = IDX_GUARDED_BINDING(Index_Entity_RemoveScript);
		// Unbuffered string slots removed: they returned a thread-local pointer that raced across calls. Use *Buffer variants only.
		b.Entity_GetManagedComponentFieldsBuffer = IDX_GUARDED_BINDING(Index_Entity_GetManagedComponentFieldsBuffer);
		b.Entity_GetIsStatic = IDX_GUARDED_BINDING(Index_Entity_GetIsStatic);
		b.Entity_SetIsStatic = IDX_GUARDED_BINDING(Index_Entity_SetIsStatic);
		b.Entity_GetIsEnabled            = IDX_GUARDED_BINDING(Index_Entity_GetIsEnabled);
		b.Entity_GetIsEnabledInHierarchy = IDX_GUARDED_BINDING(Index_Entity_GetIsEnabledInHierarchy);
		b.Entity_SetIsEnabled            = IDX_GUARDED_BINDING(Index_Entity_SetIsEnabled);

		b.NameComponent_GetNameBuffer = IDX_GUARDED_BINDING(Index_NameComponent_GetNameBuffer);
		b.NameComponent_SetName = IDX_GUARDED_BINDING(Index_NameComponent_SetName);

		b.Entity_GetParent = IDX_GUARDED_BINDING(Index_Entity_GetParent);
		b.Entity_SetParent = IDX_GUARDED_BINDING(Index_Entity_SetParent);
		b.Entity_GetChildCount = IDX_GUARDED_BINDING(Index_Entity_GetChildCount);
		b.Entity_GetChildAt = IDX_GUARDED_BINDING(Index_Entity_GetChildAt);
		b.Entity_GetChildren = IDX_GUARDED_BINDING(Index_Entity_GetChildren);


		b.TextRenderer_GetTextBuffer = IDX_GUARDED_BINDING(Index_TextRenderer_GetTextBuffer);
		b.TextRenderer_SetText = IDX_GUARDED_BINDING(Index_TextRenderer_SetText);
		b.TextRenderer_GetFont = IDX_GUARDED_BINDING(Index_TextRenderer_GetFont);
		b.TextRenderer_SetFont = IDX_GUARDED_BINDING(Index_TextRenderer_SetFont);
		b.TextRenderer_GetFontSize = IDX_GUARDED_BINDING(Index_TextRenderer_GetFontSize);
		b.TextRenderer_SetFontSize = IDX_GUARDED_BINDING(Index_TextRenderer_SetFontSize);
		b.TextRenderer_GetColor = IDX_GUARDED_BINDING(Index_TextRenderer_GetColor);
		b.TextRenderer_SetColor = IDX_GUARDED_BINDING(Index_TextRenderer_SetColor);
		b.TextRenderer_GetLetterSpacing = IDX_GUARDED_BINDING(Index_TextRenderer_GetLetterSpacing);
		b.TextRenderer_SetLetterSpacing = IDX_GUARDED_BINDING(Index_TextRenderer_SetLetterSpacing);
		b.TextRenderer_GetLineSpacing = IDX_GUARDED_BINDING(Index_TextRenderer_GetLineSpacing);
		b.TextRenderer_SetLineSpacing = IDX_GUARDED_BINDING(Index_TextRenderer_SetLineSpacing);
		b.TextRenderer_GetHAlign = IDX_GUARDED_BINDING(Index_TextRenderer_GetHAlign);
		b.TextRenderer_SetHAlign = IDX_GUARDED_BINDING(Index_TextRenderer_SetHAlign);
		b.TextRenderer_GetVAlign = IDX_GUARDED_BINDING(Index_TextRenderer_GetVAlign);
		b.TextRenderer_SetVAlign = IDX_GUARDED_BINDING(Index_TextRenderer_SetVAlign);
		b.TextRenderer_GetWrapMode = IDX_GUARDED_BINDING(Index_TextRenderer_GetWrapMode);
		b.TextRenderer_SetWrapMode = IDX_GUARDED_BINDING(Index_TextRenderer_SetWrapMode);
		// WrapWidth get/set slots removed; the C# struct doesn't carry
		// them anymore.
		b.TextRenderer_GetSortingOrder = IDX_GUARDED_BINDING(Index_TextRenderer_GetSortingOrder);
		b.TextRenderer_SetSortingOrder = IDX_GUARDED_BINDING(Index_TextRenderer_SetSortingOrder);
		b.TextRenderer_GetSortingLayer = IDX_GUARDED_BINDING(Index_TextRenderer_GetSortingLayer);
		b.TextRenderer_SetSortingLayer = IDX_GUARDED_BINDING(Index_TextRenderer_SetSortingLayer);






		b.AudioSource_Play = IDX_GUARDED_BINDING(Index_AudioSource_Play);
		b.AudioSource_Pause = IDX_GUARDED_BINDING(Index_AudioSource_Pause);
		b.AudioSource_Stop = IDX_GUARDED_BINDING(Index_AudioSource_Stop);
		b.AudioSource_Resume = IDX_GUARDED_BINDING(Index_AudioSource_Resume);
		b.AudioSource_GetVolume = IDX_GUARDED_BINDING(Index_AudioSource_GetVolume);
		b.AudioSource_SetVolume = IDX_GUARDED_BINDING(Index_AudioSource_SetVolume);
		b.AudioSource_GetPitch = IDX_GUARDED_BINDING(Index_AudioSource_GetPitch);
		b.AudioSource_SetPitch = IDX_GUARDED_BINDING(Index_AudioSource_SetPitch);
		b.AudioSource_GetLoop = IDX_GUARDED_BINDING(Index_AudioSource_GetLoop);
		b.AudioSource_SetLoop = IDX_GUARDED_BINDING(Index_AudioSource_SetLoop);
		b.AudioSource_IsPlaying = IDX_GUARDED_BINDING(Index_AudioSource_IsPlaying);
		b.AudioSource_IsPaused = IDX_GUARDED_BINDING(Index_AudioSource_IsPaused);
		b.AudioSource_GetAudio = IDX_GUARDED_BINDING(Index_AudioSource_GetAudio);
		b.AudioSource_SetAudio = IDX_GUARDED_BINDING(Index_AudioSource_SetAudio);


		b.Scene_GetActiveSceneNameBuffer = IDX_GUARDED_BINDING(Index_Scene_GetActiveSceneNameBuffer);
		b.Scene_GetEntityCount = IDX_GUARDED_BINDING(Index_Scene_GetEntityCount);
		b.Scene_GetEntityCountByName = IDX_GUARDED_BINDING(Index_Scene_GetEntityCountByName);
		b.Scene_LoadAdditive = IDX_GUARDED_BINDING(Index_Scene_LoadAdditive);
		b.Scene_Load = IDX_GUARDED_BINDING(Index_Scene_Load);
		b.Scene_Unload = IDX_GUARDED_BINDING(Index_Scene_Unload);
		b.Scene_SetActive = IDX_GUARDED_BINDING(Index_Scene_SetActive);
		b.Scene_Reload = IDX_GUARDED_BINDING(Index_Scene_Reload);
		b.Scene_SetSceneSystemEnabled = IDX_GUARDED_BINDING(Index_Scene_SetSceneSystemEnabled);
		b.Scene_IsSceneSystemEnabled = IDX_GUARDED_BINDING(Index_Scene_IsSceneSystemEnabled);
		b.Scene_SetGlobalSystemEnabled = IDX_GUARDED_BINDING(Index_Scene_SetGlobalSystemEnabled);
		b.Scene_DoesSceneExist = IDX_GUARDED_BINDING(Index_Scene_DoesSceneExist);
		b.Scene_GetLoadedCount = IDX_GUARDED_BINDING(Index_Scene_GetLoadedCount);
		b.Scene_GetLoadedSceneNameAtBuffer = IDX_GUARDED_BINDING(Index_Scene_GetLoadedSceneNameAtBuffer);
		b.Scene_GetEntityNameByUUIDBuffer = IDX_GUARDED_BINDING(Index_Scene_GetEntityNameByUUIDBuffer);
		b.Scene_QueryEntities = IDX_GUARDED_BINDING(Index_Scene_QueryEntities);
		b.Scene_QueryEntitiesFiltered = IDX_GUARDED_BINDING(Index_Scene_QueryEntitiesFiltered);
		b.Scene_QueryEntitiesInScene = IDX_GUARDED_BINDING(Index_Scene_QueryEntitiesInScene);
		b.Scene_QueryEntitiesFilteredInScene = IDX_GUARDED_BINDING(Index_Scene_QueryEntitiesFilteredInScene);

		b.Asset_IsValid = IDX_GUARDED_BINDING(Index_Asset_IsValid);
		b.Asset_GetOrCreateUUIDFromPath = IDX_GUARDED_BINDING(Index_Asset_GetOrCreateUUIDFromPath);
		b.Asset_GetPathBuffer = IDX_GUARDED_BINDING(Index_Asset_GetPathBuffer);
		b.Asset_GetDisplayNameBuffer = IDX_GUARDED_BINDING(Index_Asset_GetDisplayNameBuffer);
		b.Asset_GetKind = IDX_GUARDED_BINDING(Index_Asset_GetKind);
		b.Asset_FindAllBuffer = IDX_GUARDED_BINDING(Index_Asset_FindAllBuffer);
		b.DataAsset_EnsureLoaded = IDX_GUARDED_BINDING(Index_DataAsset_EnsureLoaded);
		b.Texture_LoadAsset = IDX_GUARDED_BINDING(Index_Texture_LoadAsset);
		b.Texture_GetWidth = IDX_GUARDED_BINDING(Index_Texture_GetWidth);
		b.Texture_GetHeight = IDX_GUARDED_BINDING(Index_Texture_GetHeight);
		b.Texture_GetDefaultAssetUUID = IDX_GUARDED_BINDING(Index_Texture_GetDefaultAssetUUID);
		b.Texture_CreateRuntime = IDX_GUARDED_BINDING(Index_Texture_CreateRuntime);
		b.Texture_UpdateRuntime = IDX_GUARDED_BINDING(Index_Texture_UpdateRuntime);
		b.Texture_DestroyRuntime = IDX_GUARDED_BINDING(Index_Texture_DestroyRuntime);
		b.Texture_IsRuntime = IDX_GUARDED_BINDING(Index_Texture_IsRuntime);
		b.Audio_LoadAsset = IDX_GUARDED_BINDING(Index_Audio_LoadAsset);
		b.Audio_PlayOneShotAsset = IDX_GUARDED_BINDING(Index_Audio_PlayOneShotAsset);
		b.Font_LoadAsset = IDX_GUARDED_BINDING(Index_Font_LoadAsset);


		b.Gizmo_DrawLine = IDX_GUARDED_BINDING(Index_Gizmo_DrawLine);
		b.Gizmo_DrawWireSquare = IDX_GUARDED_BINDING(Index_Gizmo_DrawWireSquare);
		b.Gizmo_DrawWireCircle = IDX_GUARDED_BINDING(Index_Gizmo_DrawWireCircle);
		b.Gizmo_DrawSquare = IDX_GUARDED_BINDING(Index_Gizmo_DrawSquare);
		b.Gizmo_DrawCircle = IDX_GUARDED_BINDING(Index_Gizmo_DrawCircle);
		b.Gizmo_DrawText = IDX_GUARDED_BINDING(Index_Gizmo_DrawText);
		b.Gizmo_SetColor = IDX_GUARDED_BINDING(Index_Gizmo_SetColor);
		b.Gizmo_GetColor = IDX_GUARDED_BINDING(Index_Gizmo_GetColor);
		b.Gizmo_GetLineWidth = IDX_GUARDED_BINDING(Index_Gizmo_GetLineWidth);
		b.Gizmo_SetLineWidth = IDX_GUARDED_BINDING(Index_Gizmo_SetLineWidth);
		b.Gizmo_SetMaxVertices = IDX_GUARDED_BINDING(Index_Gizmo_SetMaxVertices);
		b.Gizmo_GetMaxVertices = IDX_GUARDED_BINDING(Index_Gizmo_GetMaxVertices);
		b.Gizmo_GetRegisteredVertices = IDX_GUARDED_BINDING(Index_Gizmo_GetRegisteredVertices);
		b.Gizmo_GetEnabled = IDX_GUARDED_BINDING(Index_Gizmo_GetEnabled);
		b.Gizmo_SetEnabled = IDX_GUARDED_BINDING(Index_Gizmo_SetEnabled);




		b.UI_GetHoveredEntity = IDX_GUARDED_BINDING(Index_UI_GetHoveredEntity);
		b.RectTransform_GetScreenPosition = IDX_GUARDED_BINDING(Index_RectTransform_GetScreenPosition);
		b.RectTransform_SetScreenPosition = IDX_GUARDED_BINDING(Index_RectTransform_SetScreenPosition);

		// ── UI ──────────────────────────────────────────────────────
		b.RectTransform_GetAnchorMin        = IDX_GUARDED_BINDING(Index_RectTransform_GetAnchorMin);
		b.RectTransform_SetAnchorMin        = IDX_GUARDED_BINDING(Index_RectTransform_SetAnchorMin);
		b.RectTransform_GetAnchorMax        = IDX_GUARDED_BINDING(Index_RectTransform_GetAnchorMax);
		b.RectTransform_SetAnchorMax        = IDX_GUARDED_BINDING(Index_RectTransform_SetAnchorMax);
		b.RectTransform_GetPivot            = IDX_GUARDED_BINDING(Index_RectTransform_GetPivot);
		b.RectTransform_SetPivot            = IDX_GUARDED_BINDING(Index_RectTransform_SetPivot);
		b.RectTransform_GetAnchoredPosition = IDX_GUARDED_BINDING(Index_RectTransform_GetAnchoredPosition);
		b.RectTransform_SetAnchoredPosition = IDX_GUARDED_BINDING(Index_RectTransform_SetAnchoredPosition);
		b.RectTransform_GetSizeDelta        = IDX_GUARDED_BINDING(Index_RectTransform_GetSizeDelta);
		b.RectTransform_SetSizeDelta        = IDX_GUARDED_BINDING(Index_RectTransform_SetSizeDelta);
		b.RectTransform_GetRotation         = IDX_GUARDED_BINDING(Index_RectTransform_GetRotation);
		b.RectTransform_SetRotation         = IDX_GUARDED_BINDING(Index_RectTransform_SetRotation);
		b.RectTransform_GetScale            = IDX_GUARDED_BINDING(Index_RectTransform_GetScale);
		b.RectTransform_SetScale            = IDX_GUARDED_BINDING(Index_RectTransform_SetScale);
		b.RectTransform_GetLocalRotation    = IDX_GUARDED_BINDING(Index_RectTransform_GetLocalRotation);
		b.RectTransform_SetLocalRotation    = IDX_GUARDED_BINDING(Index_RectTransform_SetLocalRotation);
		b.RectTransform_GetLocalScale       = IDX_GUARDED_BINDING(Index_RectTransform_GetLocalScale);
		b.RectTransform_SetLocalScale       = IDX_GUARDED_BINDING(Index_RectTransform_SetLocalScale);
		b.RectTransform_GetResolvedSize     = IDX_GUARDED_BINDING(Index_RectTransform_GetResolvedSize);

		b.Image_GetColor        = IDX_GUARDED_BINDING(Index_Image_GetColor);
		b.Image_SetColor        = IDX_GUARDED_BINDING(Index_Image_SetColor);
		b.Image_GetTexture      = IDX_GUARDED_BINDING(Index_Image_GetTexture);
		b.Image_SetTexture      = IDX_GUARDED_BINDING(Index_Image_SetTexture);
		b.Image_GetSortingOrder = IDX_GUARDED_BINDING(Index_Image_GetSortingOrder);
		b.Image_SetSortingOrder = IDX_GUARDED_BINDING(Index_Image_SetSortingOrder);
		b.Image_GetSortingLayer = IDX_GUARDED_BINDING(Index_Image_GetSortingLayer);
		b.Image_SetSortingLayer = IDX_GUARDED_BINDING(Index_Image_SetSortingLayer);

		b.Interactable_GetInteractable = IDX_GUARDED_BINDING(Index_Interactable_GetInteractable);
		b.Interactable_SetInteractable = IDX_GUARDED_BINDING(Index_Interactable_SetInteractable);
		b.Interactable_GetIsHovered    = IDX_GUARDED_BINDING(Index_Interactable_GetIsHovered);
		b.Interactable_GetIsClicked    = IDX_GUARDED_BINDING(Index_Interactable_GetIsClicked);
		b.Interactable_GetIsPressed    = IDX_GUARDED_BINDING(Index_Interactable_GetIsPressed);
		b.Interactable_GetIsMouseDown  = IDX_GUARDED_BINDING(Index_Interactable_GetIsMouseDown);
		b.Interactable_GetIsMouseUp    = IDX_GUARDED_BINDING(Index_Interactable_GetIsMouseUp);
		b.Interactable_GetFocusable    = IDX_GUARDED_BINDING(Index_Interactable_GetFocusable);
		b.Interactable_SetFocusable    = IDX_GUARDED_BINDING(Index_Interactable_SetFocusable);
		b.Interactable_GetIsFocused    = IDX_GUARDED_BINDING(Index_Interactable_GetIsFocused);
		b.Interactable_SetIsFocused    = IDX_GUARDED_BINDING(Index_Interactable_SetIsFocused);

		b.Button_GetNormalColor   = IDX_GUARDED_BINDING(Index_Button_GetNormalColor);
		b.Button_SetNormalColor   = IDX_GUARDED_BINDING(Index_Button_SetNormalColor);
		b.Button_GetHoveredColor  = IDX_GUARDED_BINDING(Index_Button_GetHoveredColor);
		b.Button_SetHoveredColor  = IDX_GUARDED_BINDING(Index_Button_SetHoveredColor);
		b.Button_GetPressedColor  = IDX_GUARDED_BINDING(Index_Button_GetPressedColor);
		b.Button_SetPressedColor  = IDX_GUARDED_BINDING(Index_Button_SetPressedColor);
		b.Button_GetDisabledColor = IDX_GUARDED_BINDING(Index_Button_GetDisabledColor);
		b.Button_SetDisabledColor = IDX_GUARDED_BINDING(Index_Button_SetDisabledColor);

		b.Slider_GetValue                 = IDX_GUARDED_BINDING(Index_Slider_GetValue);
		b.Slider_SetValue                 = IDX_GUARDED_BINDING(Index_Slider_SetValue);
		b.Slider_GetMinValue              = IDX_GUARDED_BINDING(Index_Slider_GetMinValue);
		b.Slider_SetMinValue              = IDX_GUARDED_BINDING(Index_Slider_SetMinValue);
		b.Slider_GetMaxValue              = IDX_GUARDED_BINDING(Index_Slider_GetMaxValue);
		b.Slider_SetMaxValue              = IDX_GUARDED_BINDING(Index_Slider_SetMaxValue);
		b.Slider_GetWholeNumbers          = IDX_GUARDED_BINDING(Index_Slider_GetWholeNumbers);
		b.Slider_SetWholeNumbers          = IDX_GUARDED_BINDING(Index_Slider_SetWholeNumbers);
		b.Slider_GetValueChangedThisFrame = IDX_GUARDED_BINDING(Index_Slider_GetValueChangedThisFrame);
		b.Slider_MarkValueObserved        = IDX_GUARDED_BINDING(Index_Slider_MarkValueObserved);
		b.Slider_GetNormalColor   = IDX_GUARDED_BINDING(Index_Slider_GetNormalColor);
		b.Slider_SetNormalColor   = IDX_GUARDED_BINDING(Index_Slider_SetNormalColor);
		b.Slider_GetHoveredColor  = IDX_GUARDED_BINDING(Index_Slider_GetHoveredColor);
		b.Slider_SetHoveredColor  = IDX_GUARDED_BINDING(Index_Slider_SetHoveredColor);
		b.Slider_GetPressedColor  = IDX_GUARDED_BINDING(Index_Slider_GetPressedColor);
		b.Slider_SetPressedColor  = IDX_GUARDED_BINDING(Index_Slider_SetPressedColor);
		b.Slider_GetDisabledColor = IDX_GUARDED_BINDING(Index_Slider_GetDisabledColor);
		b.Slider_SetDisabledColor = IDX_GUARDED_BINDING(Index_Slider_SetDisabledColor);

		b.Toggle_GetIsOn                  = IDX_GUARDED_BINDING(Index_Toggle_GetIsOn);
		b.Toggle_SetIsOn                  = IDX_GUARDED_BINDING(Index_Toggle_SetIsOn);
		b.Toggle_GetValueChangedThisFrame = IDX_GUARDED_BINDING(Index_Toggle_GetValueChangedThisFrame);
		b.Toggle_MarkIsOnObserved         = IDX_GUARDED_BINDING(Index_Toggle_MarkIsOnObserved);
		b.Toggle_GetNormalColor   = IDX_GUARDED_BINDING(Index_Toggle_GetNormalColor);
		b.Toggle_SetNormalColor   = IDX_GUARDED_BINDING(Index_Toggle_SetNormalColor);
		b.Toggle_GetHoveredColor  = IDX_GUARDED_BINDING(Index_Toggle_GetHoveredColor);
		b.Toggle_SetHoveredColor  = IDX_GUARDED_BINDING(Index_Toggle_SetHoveredColor);
		b.Toggle_GetPressedColor  = IDX_GUARDED_BINDING(Index_Toggle_GetPressedColor);
		b.Toggle_SetPressedColor  = IDX_GUARDED_BINDING(Index_Toggle_SetPressedColor);
		b.Toggle_GetDisabledColor = IDX_GUARDED_BINDING(Index_Toggle_GetDisabledColor);
		b.Toggle_SetDisabledColor = IDX_GUARDED_BINDING(Index_Toggle_SetDisabledColor);

		b.InputField_GetTextBuffer            = IDX_GUARDED_BINDING(Index_InputField_GetTextBuffer);
		b.InputField_SetText                  = IDX_GUARDED_BINDING(Index_InputField_SetText);
		b.InputField_GetPlaceholderTextBuffer = IDX_GUARDED_BINDING(Index_InputField_GetPlaceholderTextBuffer);
		b.InputField_SetPlaceholderText       = IDX_GUARDED_BINDING(Index_InputField_SetPlaceholderText);
		b.InputField_GetIsFocused             = IDX_GUARDED_BINDING(Index_InputField_GetIsFocused);
		b.InputField_SetIsFocused             = IDX_GUARDED_BINDING(Index_InputField_SetIsFocused);
		b.InputField_GetSubmittedThisFrame    = IDX_GUARDED_BINDING(Index_InputField_GetSubmittedThisFrame);
		b.InputField_GetCharacterLimit        = IDX_GUARDED_BINDING(Index_InputField_GetCharacterLimit);
		b.InputField_SetCharacterLimit        = IDX_GUARDED_BINDING(Index_InputField_SetCharacterLimit);
		b.InputField_GetMultiline             = IDX_GUARDED_BINDING(Index_InputField_GetMultiline);
		b.InputField_SetMultiline             = IDX_GUARDED_BINDING(Index_InputField_SetMultiline);
		b.InputField_GetVerticalScrollbarEntity = IDX_GUARDED_BINDING(Index_InputField_GetVerticalScrollbarEntity);
		b.InputField_SetVerticalScrollbarEntity = IDX_GUARDED_BINDING(Index_InputField_SetVerticalScrollbarEntity);
		b.InputField_GetNormalColor   = IDX_GUARDED_BINDING(Index_InputField_GetNormalColor);
		b.InputField_SetNormalColor   = IDX_GUARDED_BINDING(Index_InputField_SetNormalColor);
		b.InputField_GetHoveredColor  = IDX_GUARDED_BINDING(Index_InputField_GetHoveredColor);
		b.InputField_SetHoveredColor  = IDX_GUARDED_BINDING(Index_InputField_SetHoveredColor);
		b.InputField_GetPressedColor  = IDX_GUARDED_BINDING(Index_InputField_GetPressedColor);
		b.InputField_SetPressedColor  = IDX_GUARDED_BINDING(Index_InputField_SetPressedColor);
		b.InputField_GetDisabledColor = IDX_GUARDED_BINDING(Index_InputField_GetDisabledColor);
		b.InputField_SetDisabledColor = IDX_GUARDED_BINDING(Index_InputField_SetDisabledColor);

		b.Dropdown_GetSelectedIndex             = IDX_GUARDED_BINDING(Index_Dropdown_GetSelectedIndex);
		b.Dropdown_SetSelectedIndex             = IDX_GUARDED_BINDING(Index_Dropdown_SetSelectedIndex);
		b.Dropdown_GetIsOpen                    = IDX_GUARDED_BINDING(Index_Dropdown_GetIsOpen);
		b.Dropdown_SetIsOpen                    = IDX_GUARDED_BINDING(Index_Dropdown_SetIsOpen);
		b.Dropdown_GetSelectionChangedThisFrame = IDX_GUARDED_BINDING(Index_Dropdown_GetSelectionChangedThisFrame);
		b.Dropdown_MarkSelectedIndexObserved    = IDX_GUARDED_BINDING(Index_Dropdown_MarkSelectedIndexObserved);
		b.Dropdown_GetOptionCount               = IDX_GUARDED_BINDING(Index_Dropdown_GetOptionCount);
		b.Dropdown_GetOptionBuffer              = IDX_GUARDED_BINDING(Index_Dropdown_GetOptionBuffer);
		b.Dropdown_SetOption                    = IDX_GUARDED_BINDING(Index_Dropdown_SetOption);
		b.Dropdown_AddOption                    = IDX_GUARDED_BINDING(Index_Dropdown_AddOption);
		b.Dropdown_RemoveOption                 = IDX_GUARDED_BINDING(Index_Dropdown_RemoveOption);
		b.Dropdown_ClearOptions                 = IDX_GUARDED_BINDING(Index_Dropdown_ClearOptions);
		b.Dropdown_GetNormalColor   = IDX_GUARDED_BINDING(Index_Dropdown_GetNormalColor);
		b.Dropdown_SetNormalColor   = IDX_GUARDED_BINDING(Index_Dropdown_SetNormalColor);
		b.Dropdown_GetHoveredColor  = IDX_GUARDED_BINDING(Index_Dropdown_GetHoveredColor);
		b.Dropdown_SetHoveredColor  = IDX_GUARDED_BINDING(Index_Dropdown_SetHoveredColor);
		b.Dropdown_GetPressedColor  = IDX_GUARDED_BINDING(Index_Dropdown_GetPressedColor);
		b.Dropdown_SetPressedColor  = IDX_GUARDED_BINDING(Index_Dropdown_SetPressedColor);
		b.Dropdown_GetDisabledColor = IDX_GUARDED_BINDING(Index_Dropdown_GetDisabledColor);
		b.Dropdown_SetDisabledColor = IDX_GUARDED_BINDING(Index_Dropdown_SetDisabledColor);

		b.Button_GetFocusedColor    = IDX_GUARDED_BINDING(Index_Button_GetFocusedColor);
		b.Button_SetFocusedColor    = IDX_GUARDED_BINDING(Index_Button_SetFocusedColor);
		b.Toggle_GetFocusedColor    = IDX_GUARDED_BINDING(Index_Toggle_GetFocusedColor);
		b.Toggle_SetFocusedColor    = IDX_GUARDED_BINDING(Index_Toggle_SetFocusedColor);
		b.Slider_GetFocusedColor    = IDX_GUARDED_BINDING(Index_Slider_GetFocusedColor);
		b.Slider_SetFocusedColor    = IDX_GUARDED_BINDING(Index_Slider_SetFocusedColor);
		b.InputField_GetFocusedColor= IDX_GUARDED_BINDING(Index_InputField_GetFocusedColor);
		b.InputField_SetFocusedColor= IDX_GUARDED_BINDING(Index_InputField_SetFocusedColor);
		b.Dropdown_GetFocusedColor  = IDX_GUARDED_BINDING(Index_Dropdown_GetFocusedColor);
		b.Dropdown_SetFocusedColor  = IDX_GUARDED_BINDING(Index_Dropdown_SetFocusedColor);

		b.Button_GetTransitionMode     = IDX_GUARDED_BINDING(Index_Button_GetTransitionMode);
		b.Button_SetTransitionMode     = IDX_GUARDED_BINDING(Index_Button_SetTransitionMode);
		b.Toggle_GetTransitionMode     = IDX_GUARDED_BINDING(Index_Toggle_GetTransitionMode);
		b.Toggle_SetTransitionMode     = IDX_GUARDED_BINDING(Index_Toggle_SetTransitionMode);
		b.Slider_GetTransitionMode     = IDX_GUARDED_BINDING(Index_Slider_GetTransitionMode);
		b.Slider_SetTransitionMode     = IDX_GUARDED_BINDING(Index_Slider_SetTransitionMode);
		b.InputField_GetTransitionMode = IDX_GUARDED_BINDING(Index_InputField_GetTransitionMode);
		b.InputField_SetTransitionMode = IDX_GUARDED_BINDING(Index_InputField_SetTransitionMode);
		b.Dropdown_GetTransitionMode   = IDX_GUARDED_BINDING(Index_Dropdown_GetTransitionMode);
		b.Dropdown_SetTransitionMode   = IDX_GUARDED_BINDING(Index_Dropdown_SetTransitionMode);

		b.Button_GetNormalSprite   = IDX_GUARDED_BINDING(Index_Button_GetNormalSprite);
		b.Button_SetNormalSprite   = IDX_GUARDED_BINDING(Index_Button_SetNormalSprite);
		b.Button_GetHoveredSprite  = IDX_GUARDED_BINDING(Index_Button_GetHoveredSprite);
		b.Button_SetHoveredSprite  = IDX_GUARDED_BINDING(Index_Button_SetHoveredSprite);
		b.Button_GetPressedSprite  = IDX_GUARDED_BINDING(Index_Button_GetPressedSprite);
		b.Button_SetPressedSprite  = IDX_GUARDED_BINDING(Index_Button_SetPressedSprite);
		b.Button_GetDisabledSprite = IDX_GUARDED_BINDING(Index_Button_GetDisabledSprite);
		b.Button_SetDisabledSprite = IDX_GUARDED_BINDING(Index_Button_SetDisabledSprite);
		b.Button_GetFocusedSprite  = IDX_GUARDED_BINDING(Index_Button_GetFocusedSprite);
		b.Button_SetFocusedSprite  = IDX_GUARDED_BINDING(Index_Button_SetFocusedSprite);

		b.Toggle_GetNormalSprite   = IDX_GUARDED_BINDING(Index_Toggle_GetNormalSprite);
		b.Toggle_SetNormalSprite   = IDX_GUARDED_BINDING(Index_Toggle_SetNormalSprite);
		b.Toggle_GetHoveredSprite  = IDX_GUARDED_BINDING(Index_Toggle_GetHoveredSprite);
		b.Toggle_SetHoveredSprite  = IDX_GUARDED_BINDING(Index_Toggle_SetHoveredSprite);
		b.Toggle_GetPressedSprite  = IDX_GUARDED_BINDING(Index_Toggle_GetPressedSprite);
		b.Toggle_SetPressedSprite  = IDX_GUARDED_BINDING(Index_Toggle_SetPressedSprite);
		b.Toggle_GetDisabledSprite = IDX_GUARDED_BINDING(Index_Toggle_GetDisabledSprite);
		b.Toggle_SetDisabledSprite = IDX_GUARDED_BINDING(Index_Toggle_SetDisabledSprite);
		b.Toggle_GetFocusedSprite  = IDX_GUARDED_BINDING(Index_Toggle_GetFocusedSprite);
		b.Toggle_SetFocusedSprite  = IDX_GUARDED_BINDING(Index_Toggle_SetFocusedSprite);

		b.Slider_GetNormalSprite   = IDX_GUARDED_BINDING(Index_Slider_GetNormalSprite);
		b.Slider_SetNormalSprite   = IDX_GUARDED_BINDING(Index_Slider_SetNormalSprite);
		b.Slider_GetHoveredSprite  = IDX_GUARDED_BINDING(Index_Slider_GetHoveredSprite);
		b.Slider_SetHoveredSprite  = IDX_GUARDED_BINDING(Index_Slider_SetHoveredSprite);
		b.Slider_GetPressedSprite  = IDX_GUARDED_BINDING(Index_Slider_GetPressedSprite);
		b.Slider_SetPressedSprite  = IDX_GUARDED_BINDING(Index_Slider_SetPressedSprite);
		b.Slider_GetDisabledSprite = IDX_GUARDED_BINDING(Index_Slider_GetDisabledSprite);
		b.Slider_SetDisabledSprite = IDX_GUARDED_BINDING(Index_Slider_SetDisabledSprite);
		b.Slider_GetFocusedSprite  = IDX_GUARDED_BINDING(Index_Slider_GetFocusedSprite);
		b.Slider_SetFocusedSprite  = IDX_GUARDED_BINDING(Index_Slider_SetFocusedSprite);

		b.InputField_GetNormalSprite   = IDX_GUARDED_BINDING(Index_InputField_GetNormalSprite);
		b.InputField_SetNormalSprite   = IDX_GUARDED_BINDING(Index_InputField_SetNormalSprite);
		b.InputField_GetHoveredSprite  = IDX_GUARDED_BINDING(Index_InputField_GetHoveredSprite);
		b.InputField_SetHoveredSprite  = IDX_GUARDED_BINDING(Index_InputField_SetHoveredSprite);
		b.InputField_GetPressedSprite  = IDX_GUARDED_BINDING(Index_InputField_GetPressedSprite);
		b.InputField_SetPressedSprite  = IDX_GUARDED_BINDING(Index_InputField_SetPressedSprite);
		b.InputField_GetDisabledSprite = IDX_GUARDED_BINDING(Index_InputField_GetDisabledSprite);
		b.InputField_SetDisabledSprite = IDX_GUARDED_BINDING(Index_InputField_SetDisabledSprite);
		b.InputField_GetFocusedSprite  = IDX_GUARDED_BINDING(Index_InputField_GetFocusedSprite);
		b.InputField_SetFocusedSprite  = IDX_GUARDED_BINDING(Index_InputField_SetFocusedSprite);

		b.Dropdown_GetNormalSprite   = IDX_GUARDED_BINDING(Index_Dropdown_GetNormalSprite);
		b.Dropdown_SetNormalSprite   = IDX_GUARDED_BINDING(Index_Dropdown_SetNormalSprite);
		b.Dropdown_GetHoveredSprite  = IDX_GUARDED_BINDING(Index_Dropdown_GetHoveredSprite);
		b.Dropdown_SetHoveredSprite  = IDX_GUARDED_BINDING(Index_Dropdown_SetHoveredSprite);
		b.Dropdown_GetPressedSprite  = IDX_GUARDED_BINDING(Index_Dropdown_GetPressedSprite);
		b.Dropdown_SetPressedSprite  = IDX_GUARDED_BINDING(Index_Dropdown_SetPressedSprite);
		b.Dropdown_GetDisabledSprite = IDX_GUARDED_BINDING(Index_Dropdown_GetDisabledSprite);
		b.Dropdown_SetDisabledSprite = IDX_GUARDED_BINDING(Index_Dropdown_SetDisabledSprite);
		b.Dropdown_GetFocusedSprite  = IDX_GUARDED_BINDING(Index_Dropdown_GetFocusedSprite);
		b.Dropdown_SetFocusedSprite  = IDX_GUARDED_BINDING(Index_Dropdown_SetFocusedSprite);

		b.Toggle_GetIsReadOnly   = IDX_GUARDED_BINDING(Index_Toggle_GetIsReadOnly);
		b.Toggle_SetIsReadOnly   = IDX_GUARDED_BINDING(Index_Toggle_SetIsReadOnly);
		b.Slider_GetIsReadOnly   = IDX_GUARDED_BINDING(Index_Slider_GetIsReadOnly);
		b.Slider_SetIsReadOnly   = IDX_GUARDED_BINDING(Index_Slider_SetIsReadOnly);
		b.Dropdown_GetIsReadOnly = IDX_GUARDED_BINDING(Index_Dropdown_GetIsReadOnly);
		b.Dropdown_SetIsReadOnly = IDX_GUARDED_BINDING(Index_Dropdown_SetIsReadOnly);

		b.Button_GetTargetGraphic     = IDX_GUARDED_BINDING(Index_Button_GetTargetGraphic);
		b.Button_SetTargetGraphic     = IDX_GUARDED_BINDING(Index_Button_SetTargetGraphic);
		b.Slider_GetFillEntity        = IDX_GUARDED_BINDING(Index_Slider_GetFillEntity);
		b.Slider_SetFillEntity        = IDX_GUARDED_BINDING(Index_Slider_SetFillEntity);
		b.Slider_GetHandleEntity      = IDX_GUARDED_BINDING(Index_Slider_GetHandleEntity);
		b.Slider_SetHandleEntity      = IDX_GUARDED_BINDING(Index_Slider_SetHandleEntity);
		b.Slider_GetBackgroundEntity  = IDX_GUARDED_BINDING(Index_Slider_GetBackgroundEntity);
		b.Slider_SetBackgroundEntity  = IDX_GUARDED_BINDING(Index_Slider_SetBackgroundEntity);
		b.Toggle_GetCheckmarkEntity   = IDX_GUARDED_BINDING(Index_Toggle_GetCheckmarkEntity);
		b.Toggle_SetCheckmarkEntity   = IDX_GUARDED_BINDING(Index_Toggle_SetCheckmarkEntity);
		b.InputField_GetTextEntity    = IDX_GUARDED_BINDING(Index_InputField_GetTextEntity);
		b.InputField_SetTextEntity    = IDX_GUARDED_BINDING(Index_InputField_SetTextEntity);
		b.Dropdown_GetLabelEntity     = IDX_GUARDED_BINDING(Index_Dropdown_GetLabelEntity);
		b.Dropdown_SetLabelEntity     = IDX_GUARDED_BINDING(Index_Dropdown_SetLabelEntity);

		b.Dropdown_GetOptionNormalColor   = IDX_GUARDED_BINDING(Index_Dropdown_GetOptionNormalColor);
		b.Dropdown_SetOptionNormalColor   = IDX_GUARDED_BINDING(Index_Dropdown_SetOptionNormalColor);
		b.Dropdown_GetOptionHoverColor    = IDX_GUARDED_BINDING(Index_Dropdown_GetOptionHoverColor);
		b.Dropdown_SetOptionHoverColor    = IDX_GUARDED_BINDING(Index_Dropdown_SetOptionHoverColor);
		b.Dropdown_GetOptionPressedColor  = IDX_GUARDED_BINDING(Index_Dropdown_GetOptionPressedColor);
		b.Dropdown_SetOptionPressedColor  = IDX_GUARDED_BINDING(Index_Dropdown_SetOptionPressedColor);
		b.Dropdown_GetOptionSelectedColor = IDX_GUARDED_BINDING(Index_Dropdown_GetOptionSelectedColor);
		b.Dropdown_SetOptionSelectedColor = IDX_GUARDED_BINDING(Index_Dropdown_SetOptionSelectedColor);
		b.Dropdown_GetPopupBackgroundColor = IDX_GUARDED_BINDING(Index_Dropdown_GetPopupBackgroundColor);
		b.Dropdown_SetPopupBackgroundColor = IDX_GUARDED_BINDING(Index_Dropdown_SetPopupBackgroundColor);
		b.Dropdown_GetOptionTextColor      = IDX_GUARDED_BINDING(Index_Dropdown_GetOptionTextColor);
		b.Dropdown_SetOptionTextColor      = IDX_GUARDED_BINDING(Index_Dropdown_SetOptionTextColor);

		b.Entity_GetComponentPtr   = IDX_GUARDED_BINDING(Index_Entity_GetComponentPtr);
		b.Entity_GetComponentPtrReadOnly = IDX_GUARDED_BINDING(Index_Entity_GetComponentPtrReadOnly);
		b.Entity_GetComponentSize  = IDX_GUARDED_BINDING(Index_Entity_GetComponentSize);
		b.Scene_OpenQueryView      = IDX_GUARDED_BINDING(Index_Scene_OpenQueryView);

		// ── UI (appended for binary compat — order MUST match NativeBindings in ScriptGlue.hpp and the C# mirror) ──

		// Scrollbar
		b.Scrollbar_GetValue                 = IDX_GUARDED_BINDING(Index_Scrollbar_GetValue);
		b.Scrollbar_SetValue                 = IDX_GUARDED_BINDING(Index_Scrollbar_SetValue);
		b.Scrollbar_GetSize                  = IDX_GUARDED_BINDING(Index_Scrollbar_GetSize);
		b.Scrollbar_SetSize                  = IDX_GUARDED_BINDING(Index_Scrollbar_SetSize);
		b.Scrollbar_GetNumberOfSteps         = IDX_GUARDED_BINDING(Index_Scrollbar_GetNumberOfSteps);
		b.Scrollbar_SetNumberOfSteps         = IDX_GUARDED_BINDING(Index_Scrollbar_SetNumberOfSteps);
		b.Scrollbar_GetDirection             = IDX_GUARDED_BINDING(Index_Scrollbar_GetDirection);
		b.Scrollbar_SetDirection             = IDX_GUARDED_BINDING(Index_Scrollbar_SetDirection);
		b.Scrollbar_GetIsReadOnly            = IDX_GUARDED_BINDING(Index_Scrollbar_GetIsReadOnly);
		b.Scrollbar_SetIsReadOnly            = IDX_GUARDED_BINDING(Index_Scrollbar_SetIsReadOnly);
		b.Scrollbar_GetHandleEntity          = IDX_GUARDED_BINDING(Index_Scrollbar_GetHandleEntity);
		b.Scrollbar_SetHandleEntity          = IDX_GUARDED_BINDING(Index_Scrollbar_SetHandleEntity);
		b.Scrollbar_GetValueChangedThisFrame = IDX_GUARDED_BINDING(Index_Scrollbar_GetValueChangedThisFrame);
		b.Scrollbar_MarkValueObserved        = IDX_GUARDED_BINDING(Index_Scrollbar_MarkValueObserved);
		b.Scrollbar_GetNormalColor   = IDX_GUARDED_BINDING(Index_Scrollbar_GetNormalColor);
		b.Scrollbar_SetNormalColor   = IDX_GUARDED_BINDING(Index_Scrollbar_SetNormalColor);
		b.Scrollbar_GetHoveredColor  = IDX_GUARDED_BINDING(Index_Scrollbar_GetHoveredColor);
		b.Scrollbar_SetHoveredColor  = IDX_GUARDED_BINDING(Index_Scrollbar_SetHoveredColor);
		b.Scrollbar_GetPressedColor  = IDX_GUARDED_BINDING(Index_Scrollbar_GetPressedColor);
		b.Scrollbar_SetPressedColor  = IDX_GUARDED_BINDING(Index_Scrollbar_SetPressedColor);
		b.Scrollbar_GetDisabledColor = IDX_GUARDED_BINDING(Index_Scrollbar_GetDisabledColor);
		b.Scrollbar_SetDisabledColor = IDX_GUARDED_BINDING(Index_Scrollbar_SetDisabledColor);
		b.Scrollbar_GetFocusedColor  = IDX_GUARDED_BINDING(Index_Scrollbar_GetFocusedColor);
		b.Scrollbar_SetFocusedColor  = IDX_GUARDED_BINDING(Index_Scrollbar_SetFocusedColor);
		b.Scrollbar_GetTransitionMode = IDX_GUARDED_BINDING(Index_Scrollbar_GetTransitionMode);
		b.Scrollbar_SetTransitionMode = IDX_GUARDED_BINDING(Index_Scrollbar_SetTransitionMode);
		b.Scrollbar_GetNormalSprite   = IDX_GUARDED_BINDING(Index_Scrollbar_GetNormalSprite);
		b.Scrollbar_SetNormalSprite   = IDX_GUARDED_BINDING(Index_Scrollbar_SetNormalSprite);
		b.Scrollbar_GetHoveredSprite  = IDX_GUARDED_BINDING(Index_Scrollbar_GetHoveredSprite);
		b.Scrollbar_SetHoveredSprite  = IDX_GUARDED_BINDING(Index_Scrollbar_SetHoveredSprite);
		b.Scrollbar_GetPressedSprite  = IDX_GUARDED_BINDING(Index_Scrollbar_GetPressedSprite);
		b.Scrollbar_SetPressedSprite  = IDX_GUARDED_BINDING(Index_Scrollbar_SetPressedSprite);
		b.Scrollbar_GetDisabledSprite = IDX_GUARDED_BINDING(Index_Scrollbar_GetDisabledSprite);
		b.Scrollbar_SetDisabledSprite = IDX_GUARDED_BINDING(Index_Scrollbar_SetDisabledSprite);
		b.Scrollbar_GetFocusedSprite  = IDX_GUARDED_BINDING(Index_Scrollbar_GetFocusedSprite);
		b.Scrollbar_SetFocusedSprite  = IDX_GUARDED_BINDING(Index_Scrollbar_SetFocusedSprite);

		// ScrollRect
		b.ScrollRect_GetContent                       = IDX_GUARDED_BINDING(Index_ScrollRect_GetContent);
		b.ScrollRect_SetContent                       = IDX_GUARDED_BINDING(Index_ScrollRect_SetContent);
		b.ScrollRect_GetViewport                      = IDX_GUARDED_BINDING(Index_ScrollRect_GetViewport);
		b.ScrollRect_SetViewport                      = IDX_GUARDED_BINDING(Index_ScrollRect_SetViewport);
		b.ScrollRect_GetHorizontal                    = IDX_GUARDED_BINDING(Index_ScrollRect_GetHorizontal);
		b.ScrollRect_SetHorizontal                    = IDX_GUARDED_BINDING(Index_ScrollRect_SetHorizontal);
		b.ScrollRect_GetVertical                      = IDX_GUARDED_BINDING(Index_ScrollRect_GetVertical);
		b.ScrollRect_SetVertical                      = IDX_GUARDED_BINDING(Index_ScrollRect_SetVertical);
		b.ScrollRect_GetMovementType                  = IDX_GUARDED_BINDING(Index_ScrollRect_GetMovementType);
		b.ScrollRect_SetMovementType                  = IDX_GUARDED_BINDING(Index_ScrollRect_SetMovementType);
		b.ScrollRect_GetElasticity                    = IDX_GUARDED_BINDING(Index_ScrollRect_GetElasticity);
		b.ScrollRect_SetElasticity                    = IDX_GUARDED_BINDING(Index_ScrollRect_SetElasticity);
		b.ScrollRect_GetInertia                       = IDX_GUARDED_BINDING(Index_ScrollRect_GetInertia);
		b.ScrollRect_SetInertia                       = IDX_GUARDED_BINDING(Index_ScrollRect_SetInertia);
		b.ScrollRect_GetDecelerationRate              = IDX_GUARDED_BINDING(Index_ScrollRect_GetDecelerationRate);
		b.ScrollRect_SetDecelerationRate              = IDX_GUARDED_BINDING(Index_ScrollRect_SetDecelerationRate);
		b.ScrollRect_GetScrollSensitivity             = IDX_GUARDED_BINDING(Index_ScrollRect_GetScrollSensitivity);
		b.ScrollRect_SetScrollSensitivity             = IDX_GUARDED_BINDING(Index_ScrollRect_SetScrollSensitivity);
		b.ScrollRect_GetHorizontalScrollbar           = IDX_GUARDED_BINDING(Index_ScrollRect_GetHorizontalScrollbar);
		b.ScrollRect_SetHorizontalScrollbar           = IDX_GUARDED_BINDING(Index_ScrollRect_SetHorizontalScrollbar);
		b.ScrollRect_GetVerticalScrollbar             = IDX_GUARDED_BINDING(Index_ScrollRect_GetVerticalScrollbar);
		b.ScrollRect_SetVerticalScrollbar             = IDX_GUARDED_BINDING(Index_ScrollRect_SetVerticalScrollbar);
		b.ScrollRect_GetHorizontalScrollbarVisibility = IDX_GUARDED_BINDING(Index_ScrollRect_GetHorizontalScrollbarVisibility);
		b.ScrollRect_SetHorizontalScrollbarVisibility = IDX_GUARDED_BINDING(Index_ScrollRect_SetHorizontalScrollbarVisibility);
		b.ScrollRect_GetVerticalScrollbarVisibility   = IDX_GUARDED_BINDING(Index_ScrollRect_GetVerticalScrollbarVisibility);
		b.ScrollRect_SetVerticalScrollbarVisibility   = IDX_GUARDED_BINDING(Index_ScrollRect_SetVerticalScrollbarVisibility);
		b.ScrollRect_GetHorizontalScrollbarSpacing    = IDX_GUARDED_BINDING(Index_ScrollRect_GetHorizontalScrollbarSpacing);
		b.ScrollRect_SetHorizontalScrollbarSpacing    = IDX_GUARDED_BINDING(Index_ScrollRect_SetHorizontalScrollbarSpacing);
		b.ScrollRect_GetVerticalScrollbarSpacing      = IDX_GUARDED_BINDING(Index_ScrollRect_GetVerticalScrollbarSpacing);
		b.ScrollRect_SetVerticalScrollbarSpacing      = IDX_GUARDED_BINDING(Index_ScrollRect_SetVerticalScrollbarSpacing);
		b.ScrollRect_GetNormalizedPosition            = IDX_GUARDED_BINDING(Index_ScrollRect_GetNormalizedPosition);
		b.ScrollRect_SetNormalizedPosition            = IDX_GUARDED_BINDING(Index_ScrollRect_SetNormalizedPosition);
		b.ScrollRect_GetValueChangedThisFrame         = IDX_GUARDED_BINDING(Index_ScrollRect_GetValueChangedThisFrame);
		b.ScrollRect_MarkValueObserved                = IDX_GUARDED_BINDING(Index_ScrollRect_MarkValueObserved);

		// Mask
		b.Mask_GetShowMaskGraphic = IDX_GUARDED_BINDING(Index_Mask_GetShowMaskGraphic);
		b.Mask_SetShowMaskGraphic = IDX_GUARDED_BINDING(Index_Mask_SetShowMaskGraphic);

		// CircularSlider
		b.CircularSlider_GetValue                 = IDX_GUARDED_BINDING(Index_CircularSlider_GetValue);
		b.CircularSlider_SetValue                 = IDX_GUARDED_BINDING(Index_CircularSlider_SetValue);
		b.CircularSlider_GetMinValue              = IDX_GUARDED_BINDING(Index_CircularSlider_GetMinValue);
		b.CircularSlider_SetMinValue              = IDX_GUARDED_BINDING(Index_CircularSlider_SetMinValue);
		b.CircularSlider_GetMaxValue              = IDX_GUARDED_BINDING(Index_CircularSlider_GetMaxValue);
		b.CircularSlider_SetMaxValue              = IDX_GUARDED_BINDING(Index_CircularSlider_SetMaxValue);
		b.CircularSlider_GetWholeNumbers          = IDX_GUARDED_BINDING(Index_CircularSlider_GetWholeNumbers);
		b.CircularSlider_SetWholeNumbers          = IDX_GUARDED_BINDING(Index_CircularSlider_SetWholeNumbers);
		b.CircularSlider_GetIsReadOnly            = IDX_GUARDED_BINDING(Index_CircularSlider_GetIsReadOnly);
		b.CircularSlider_SetIsReadOnly            = IDX_GUARDED_BINDING(Index_CircularSlider_SetIsReadOnly);
		b.CircularSlider_GetStartAngleDegrees     = IDX_GUARDED_BINDING(Index_CircularSlider_GetStartAngleDegrees);
		b.CircularSlider_SetStartAngleDegrees     = IDX_GUARDED_BINDING(Index_CircularSlider_SetStartAngleDegrees);
		b.CircularSlider_GetSweepDegrees          = IDX_GUARDED_BINDING(Index_CircularSlider_GetSweepDegrees);
		b.CircularSlider_SetSweepDegrees          = IDX_GUARDED_BINDING(Index_CircularSlider_SetSweepDegrees);
		b.CircularSlider_GetClockwise             = IDX_GUARDED_BINDING(Index_CircularSlider_GetClockwise);
		b.CircularSlider_SetClockwise             = IDX_GUARDED_BINDING(Index_CircularSlider_SetClockwise);
		b.CircularSlider_GetRingThickness         = IDX_GUARDED_BINDING(Index_CircularSlider_GetRingThickness);
		b.CircularSlider_SetRingThickness         = IDX_GUARDED_BINDING(Index_CircularSlider_SetRingThickness);
		b.CircularSlider_GetRingSegments          = IDX_GUARDED_BINDING(Index_CircularSlider_GetRingSegments);
		b.CircularSlider_SetRingSegments          = IDX_GUARDED_BINDING(Index_CircularSlider_SetRingSegments);
		b.CircularSlider_GetBackgroundColor       = IDX_GUARDED_BINDING(Index_CircularSlider_GetBackgroundColor);
		b.CircularSlider_SetBackgroundColor       = IDX_GUARDED_BINDING(Index_CircularSlider_SetBackgroundColor);
		b.CircularSlider_GetFillColor             = IDX_GUARDED_BINDING(Index_CircularSlider_GetFillColor);
		b.CircularSlider_SetFillColor             = IDX_GUARDED_BINDING(Index_CircularSlider_SetFillColor);
		b.CircularSlider_GetHandleEntity          = IDX_GUARDED_BINDING(Index_CircularSlider_GetHandleEntity);
		b.CircularSlider_SetHandleEntity          = IDX_GUARDED_BINDING(Index_CircularSlider_SetHandleEntity);
		b.CircularSlider_GetValueChangedThisFrame = IDX_GUARDED_BINDING(Index_CircularSlider_GetValueChangedThisFrame);
		b.CircularSlider_MarkValueObserved        = IDX_GUARDED_BINDING(Index_CircularSlider_MarkValueObserved);
		b.CircularSlider_GetNormalColor   = IDX_GUARDED_BINDING(Index_CircularSlider_GetNormalColor);
		b.CircularSlider_SetNormalColor   = IDX_GUARDED_BINDING(Index_CircularSlider_SetNormalColor);
		b.CircularSlider_GetHoveredColor  = IDX_GUARDED_BINDING(Index_CircularSlider_GetHoveredColor);
		b.CircularSlider_SetHoveredColor  = IDX_GUARDED_BINDING(Index_CircularSlider_SetHoveredColor);
		b.CircularSlider_GetPressedColor  = IDX_GUARDED_BINDING(Index_CircularSlider_GetPressedColor);
		b.CircularSlider_SetPressedColor  = IDX_GUARDED_BINDING(Index_CircularSlider_SetPressedColor);
		b.CircularSlider_GetDisabledColor = IDX_GUARDED_BINDING(Index_CircularSlider_GetDisabledColor);
		b.CircularSlider_SetDisabledColor = IDX_GUARDED_BINDING(Index_CircularSlider_SetDisabledColor);
		b.CircularSlider_GetFocusedColor  = IDX_GUARDED_BINDING(Index_CircularSlider_GetFocusedColor);
		b.CircularSlider_SetFocusedColor  = IDX_GUARDED_BINDING(Index_CircularSlider_SetFocusedColor);
		b.CircularSlider_GetTransitionMode = IDX_GUARDED_BINDING(Index_CircularSlider_GetTransitionMode);
		b.CircularSlider_SetTransitionMode = IDX_GUARDED_BINDING(Index_CircularSlider_SetTransitionMode);
		b.CircularSlider_GetNormalSprite   = IDX_GUARDED_BINDING(Index_CircularSlider_GetNormalSprite);
		b.CircularSlider_SetNormalSprite   = IDX_GUARDED_BINDING(Index_CircularSlider_SetNormalSprite);
		b.CircularSlider_GetHoveredSprite  = IDX_GUARDED_BINDING(Index_CircularSlider_GetHoveredSprite);
		b.CircularSlider_SetHoveredSprite  = IDX_GUARDED_BINDING(Index_CircularSlider_SetHoveredSprite);
		b.CircularSlider_GetPressedSprite  = IDX_GUARDED_BINDING(Index_CircularSlider_GetPressedSprite);
		b.CircularSlider_SetPressedSprite  = IDX_GUARDED_BINDING(Index_CircularSlider_SetPressedSprite);
		b.CircularSlider_GetDisabledSprite = IDX_GUARDED_BINDING(Index_CircularSlider_GetDisabledSprite);
		b.CircularSlider_SetDisabledSprite = IDX_GUARDED_BINDING(Index_CircularSlider_SetDisabledSprite);
		b.CircularSlider_GetFocusedSprite  = IDX_GUARDED_BINDING(Index_CircularSlider_GetFocusedSprite);
		b.CircularSlider_SetFocusedSprite  = IDX_GUARDED_BINDING(Index_CircularSlider_SetFocusedSprite);

		// HorizontalLayoutGroup
		b.HorizontalLayoutGroup_GetPaddingLeft            = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_GetPaddingLeft);
		b.HorizontalLayoutGroup_SetPaddingLeft            = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_SetPaddingLeft);
		b.HorizontalLayoutGroup_GetPaddingRight           = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_GetPaddingRight);
		b.HorizontalLayoutGroup_SetPaddingRight           = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_SetPaddingRight);
		b.HorizontalLayoutGroup_GetPaddingTop             = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_GetPaddingTop);
		b.HorizontalLayoutGroup_SetPaddingTop             = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_SetPaddingTop);
		b.HorizontalLayoutGroup_GetPaddingBottom          = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_GetPaddingBottom);
		b.HorizontalLayoutGroup_SetPaddingBottom          = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_SetPaddingBottom);
		b.HorizontalLayoutGroup_GetSpacing                = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_GetSpacing);
		b.HorizontalLayoutGroup_SetSpacing                = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_SetSpacing);
		b.HorizontalLayoutGroup_GetChildAlignment         = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_GetChildAlignment);
		b.HorizontalLayoutGroup_SetChildAlignment         = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_SetChildAlignment);
		b.HorizontalLayoutGroup_GetReverseArrangement     = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_GetReverseArrangement);
		b.HorizontalLayoutGroup_SetReverseArrangement     = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_SetReverseArrangement);
		b.HorizontalLayoutGroup_GetControlChildWidth      = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_GetControlChildWidth);
		b.HorizontalLayoutGroup_SetControlChildWidth      = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_SetControlChildWidth);
		b.HorizontalLayoutGroup_GetControlChildHeight     = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_GetControlChildHeight);
		b.HorizontalLayoutGroup_SetControlChildHeight     = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_SetControlChildHeight);
		b.HorizontalLayoutGroup_GetUseChildScaleWidth     = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_GetUseChildScaleWidth);
		b.HorizontalLayoutGroup_SetUseChildScaleWidth     = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_SetUseChildScaleWidth);
		b.HorizontalLayoutGroup_GetUseChildScaleHeight    = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_GetUseChildScaleHeight);
		b.HorizontalLayoutGroup_SetUseChildScaleHeight    = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_SetUseChildScaleHeight);
		b.HorizontalLayoutGroup_GetChildForceExpandWidth  = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_GetChildForceExpandWidth);
		b.HorizontalLayoutGroup_SetChildForceExpandWidth  = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_SetChildForceExpandWidth);
		b.HorizontalLayoutGroup_GetChildForceExpandHeight = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_GetChildForceExpandHeight);
		b.HorizontalLayoutGroup_SetChildForceExpandHeight = IDX_GUARDED_BINDING(Index_HorizontalLayoutGroup_SetChildForceExpandHeight);

		// VerticalLayoutGroup
		b.VerticalLayoutGroup_GetPaddingLeft            = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_GetPaddingLeft);
		b.VerticalLayoutGroup_SetPaddingLeft            = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_SetPaddingLeft);
		b.VerticalLayoutGroup_GetPaddingRight           = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_GetPaddingRight);
		b.VerticalLayoutGroup_SetPaddingRight           = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_SetPaddingRight);
		b.VerticalLayoutGroup_GetPaddingTop             = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_GetPaddingTop);
		b.VerticalLayoutGroup_SetPaddingTop             = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_SetPaddingTop);
		b.VerticalLayoutGroup_GetPaddingBottom          = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_GetPaddingBottom);
		b.VerticalLayoutGroup_SetPaddingBottom          = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_SetPaddingBottom);
		b.VerticalLayoutGroup_GetSpacing                = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_GetSpacing);
		b.VerticalLayoutGroup_SetSpacing                = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_SetSpacing);
		b.VerticalLayoutGroup_GetChildAlignment         = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_GetChildAlignment);
		b.VerticalLayoutGroup_SetChildAlignment         = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_SetChildAlignment);
		b.VerticalLayoutGroup_GetReverseArrangement     = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_GetReverseArrangement);
		b.VerticalLayoutGroup_SetReverseArrangement     = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_SetReverseArrangement);
		b.VerticalLayoutGroup_GetControlChildWidth      = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_GetControlChildWidth);
		b.VerticalLayoutGroup_SetControlChildWidth      = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_SetControlChildWidth);
		b.VerticalLayoutGroup_GetControlChildHeight     = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_GetControlChildHeight);
		b.VerticalLayoutGroup_SetControlChildHeight     = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_SetControlChildHeight);
		b.VerticalLayoutGroup_GetUseChildScaleWidth     = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_GetUseChildScaleWidth);
		b.VerticalLayoutGroup_SetUseChildScaleWidth     = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_SetUseChildScaleWidth);
		b.VerticalLayoutGroup_GetUseChildScaleHeight    = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_GetUseChildScaleHeight);
		b.VerticalLayoutGroup_SetUseChildScaleHeight    = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_SetUseChildScaleHeight);
		b.VerticalLayoutGroup_GetChildForceExpandWidth  = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_GetChildForceExpandWidth);
		b.VerticalLayoutGroup_SetChildForceExpandWidth  = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_SetChildForceExpandWidth);
		b.VerticalLayoutGroup_GetChildForceExpandHeight = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_GetChildForceExpandHeight);
		b.VerticalLayoutGroup_SetChildForceExpandHeight = IDX_GUARDED_BINDING(Index_VerticalLayoutGroup_SetChildForceExpandHeight);

		// GridLayoutGroup
		b.GridLayoutGroup_GetPaddingLeft     = IDX_GUARDED_BINDING(Index_GridLayoutGroup_GetPaddingLeft);
		b.GridLayoutGroup_SetPaddingLeft     = IDX_GUARDED_BINDING(Index_GridLayoutGroup_SetPaddingLeft);
		b.GridLayoutGroup_GetPaddingRight    = IDX_GUARDED_BINDING(Index_GridLayoutGroup_GetPaddingRight);
		b.GridLayoutGroup_SetPaddingRight    = IDX_GUARDED_BINDING(Index_GridLayoutGroup_SetPaddingRight);
		b.GridLayoutGroup_GetPaddingTop      = IDX_GUARDED_BINDING(Index_GridLayoutGroup_GetPaddingTop);
		b.GridLayoutGroup_SetPaddingTop      = IDX_GUARDED_BINDING(Index_GridLayoutGroup_SetPaddingTop);
		b.GridLayoutGroup_GetPaddingBottom   = IDX_GUARDED_BINDING(Index_GridLayoutGroup_GetPaddingBottom);
		b.GridLayoutGroup_SetPaddingBottom   = IDX_GUARDED_BINDING(Index_GridLayoutGroup_SetPaddingBottom);
		b.GridLayoutGroup_GetCellSize        = IDX_GUARDED_BINDING(Index_GridLayoutGroup_GetCellSize);
		b.GridLayoutGroup_SetCellSize        = IDX_GUARDED_BINDING(Index_GridLayoutGroup_SetCellSize);
		b.GridLayoutGroup_GetSpacing         = IDX_GUARDED_BINDING(Index_GridLayoutGroup_GetSpacing);
		b.GridLayoutGroup_SetSpacing         = IDX_GUARDED_BINDING(Index_GridLayoutGroup_SetSpacing);
		b.GridLayoutGroup_GetStartCorner     = IDX_GUARDED_BINDING(Index_GridLayoutGroup_GetStartCorner);
		b.GridLayoutGroup_SetStartCorner     = IDX_GUARDED_BINDING(Index_GridLayoutGroup_SetStartCorner);
		b.GridLayoutGroup_GetStartAxis       = IDX_GUARDED_BINDING(Index_GridLayoutGroup_GetStartAxis);
		b.GridLayoutGroup_SetStartAxis       = IDX_GUARDED_BINDING(Index_GridLayoutGroup_SetStartAxis);
		b.GridLayoutGroup_GetChildAlignment  = IDX_GUARDED_BINDING(Index_GridLayoutGroup_GetChildAlignment);
		b.GridLayoutGroup_SetChildAlignment  = IDX_GUARDED_BINDING(Index_GridLayoutGroup_SetChildAlignment);
		b.GridLayoutGroup_GetConstraint      = IDX_GUARDED_BINDING(Index_GridLayoutGroup_GetConstraint);
		b.GridLayoutGroup_SetConstraint      = IDX_GUARDED_BINDING(Index_GridLayoutGroup_SetConstraint);
		b.GridLayoutGroup_GetConstraintCount = IDX_GUARDED_BINDING(Index_GridLayoutGroup_GetConstraintCount);
		b.GridLayoutGroup_SetConstraintCount = IDX_GUARDED_BINDING(Index_GridLayoutGroup_SetConstraintCount);
		b.GridLayoutGroup_GetReverse         = IDX_GUARDED_BINDING(Index_GridLayoutGroup_GetReverse);
		b.GridLayoutGroup_SetReverse         = IDX_GUARDED_BINDING(Index_GridLayoutGroup_SetReverse);

		// ContentSizeFitter
		b.ContentSizeFitter_GetHorizontalFit  = IDX_GUARDED_BINDING(Index_ContentSizeFitter_GetHorizontalFit);
		b.ContentSizeFitter_SetHorizontalFit  = IDX_GUARDED_BINDING(Index_ContentSizeFitter_SetHorizontalFit);
		b.ContentSizeFitter_GetVerticalFit    = IDX_GUARDED_BINDING(Index_ContentSizeFitter_GetVerticalFit);
		b.ContentSizeFitter_SetVerticalFit    = IDX_GUARDED_BINDING(Index_ContentSizeFitter_SetVerticalFit);
		b.ContentSizeFitter_GetPaddingLeft    = IDX_GUARDED_BINDING(Index_ContentSizeFitter_GetPaddingLeft);
		b.ContentSizeFitter_SetPaddingLeft    = IDX_GUARDED_BINDING(Index_ContentSizeFitter_SetPaddingLeft);
		b.ContentSizeFitter_GetPaddingRight   = IDX_GUARDED_BINDING(Index_ContentSizeFitter_GetPaddingRight);
		b.ContentSizeFitter_SetPaddingRight   = IDX_GUARDED_BINDING(Index_ContentSizeFitter_SetPaddingRight);
		b.ContentSizeFitter_GetPaddingTop     = IDX_GUARDED_BINDING(Index_ContentSizeFitter_GetPaddingTop);
		b.ContentSizeFitter_SetPaddingTop     = IDX_GUARDED_BINDING(Index_ContentSizeFitter_SetPaddingTop);
		b.ContentSizeFitter_GetPaddingBottom  = IDX_GUARDED_BINDING(Index_ContentSizeFitter_GetPaddingBottom);
		b.ContentSizeFitter_SetPaddingBottom  = IDX_GUARDED_BINDING(Index_ContentSizeFitter_SetPaddingBottom);

		// WidthConstraint
		b.WidthConstraint_GetMinWidth = IDX_GUARDED_BINDING(Index_WidthConstraint_GetMinWidth);
		b.WidthConstraint_SetMinWidth = IDX_GUARDED_BINDING(Index_WidthConstraint_SetMinWidth);
		b.WidthConstraint_GetMaxWidth = IDX_GUARDED_BINDING(Index_WidthConstraint_GetMaxWidth);
		b.WidthConstraint_SetMaxWidth = IDX_GUARDED_BINDING(Index_WidthConstraint_SetMaxWidth);

		// ── SpriteRenderer filter (appended for binary compat) ──

		// ── SpriteRenderer slice name (appended for binary compat) ──

		// ── Dynamic component registration (appended for binary compat) ──
		b.Component_RegisterDynamic       = IDX_GUARDED_BINDING(Index_Component_RegisterDynamic);
		b.Component_UnregisterAllDynamic  = IDX_GUARDED_BINDING(Index_Component_UnregisterAllDynamic);

		// ── Scene load-by-GUID (appended for binary compat) ──
		b.Scene_LoadByGuid           = IDX_GUARDED_BINDING(Index_Scene_LoadByGuid);
		b.Scene_LoadAdditiveByGuid   = IDX_GUARDED_BINDING(Index_Scene_LoadAdditiveByGuid);
		b.Scene_UnloadByGuid         = IDX_GUARDED_BINDING(Index_Scene_UnloadByGuid);
		b.Scene_SetActiveByGuid      = IDX_GUARDED_BINDING(Index_Scene_SetActiveByGuid);
		b.Scene_ReloadByGuid         = IDX_GUARDED_BINDING(Index_Scene_ReloadByGuid);
		b.Scene_DoesSceneExistByGuid = IDX_GUARDED_BINDING(Index_Scene_DoesSceneExistByGuid);
		b.Scene_GetActiveSceneGuid   = IDX_GUARDED_BINDING(Index_Scene_GetActiveSceneGuid);

		// Rigidbody2D motion locks (appended for binary compat).


		b.Scene_OpenQueryViewWithEntities = IDX_GUARDED_BINDING(Index_Scene_OpenQueryViewWithEntities);
	}

} // namespace Index
