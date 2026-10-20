#include "pch.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Scene/SceneExtensions.hpp"

#include "Components/Audio/AudioSourceComponent.hpp"

#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/Camera2DComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#include "Components/Physics/BoxCollider2DComponent.hpp"
#include "Components/Physics/CircleCollider2DComponent.hpp"
#include "Components/Physics/PolygonCollider2DComponent.hpp"
#include "Components/Physics/Joint2DComponents.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Physics/Box2DWorld.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#endif
#if FUNKE_WITH_3D
#include "Components/General/Transform3DComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#endif
#include "Components/General/NameComponent.hpp"
#include "Components/General/RectTransform2DComponent.hpp"
#include "Core/EngineModules.hpp"
#include "Graphics/StaticRenderData.hpp"
#include <Components/Tags.hpp>

#include "Components/General/UUIDComponent.hpp"
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#include "Components/General/PrefabInstanceComponent.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/ComponentInfo.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <typeinfo>
#include <unordered_set>
#include <utility>

namespace Funke {
	namespace {
		// C# component wrappers are keyed by entity UUID. A scene restart loads the new copy before releasing
		// the old one, so both hold the same UUIDs; the old copy must not invalidate wrappers the new one uses.
		bool IsPersistentIdLiveInOtherScene(const Scene& self, uint64_t persistentId)
		{
			const size_t count = SceneManager::GetLoadedSceneCount();
			for (size_t i = 0; i < count; ++i) {
				const Scene* other = SceneManager::GetLoadedSceneAt(i);
				EntityHandle handle = entt::null;
				if (other && other != &self && other->TryResolveEntityRef(persistentId, handle)) {
					return true;
				}
			}
			return false;
		}

		void NotifyScriptsEntityDestroyed(const Scene& self, uint64_t persistentId)
		{
			if (persistentId != 0 && !IsPersistentIdLiveInOtherScene(self, persistentId)) {
				ScriptEngine::NotifyEntityDestroyed(persistentId);
			}
		}

		class ManagedSceneSystem final : public SceneSystem {
		public:
			explicit ManagedSceneSystem(std::string className)
				: m_ClassName(std::move(className)) {}

			~ManagedSceneSystem() noexcept override
			{
				const uint32_t handle = std::exchange(m_Handle, 0);
				if (handle == 0) return;

				// A failed Awake owns an instance but must not receive lifecycle callbacks.
				try {
					ScriptEngine::DestroySceneSystemInstance(handle);
				}
				catch (...) {
				}
			}

			const std::string& GetClassName() const { return m_ClassName; }
			uint32_t GetHandle() const { return m_Handle; }
			bool HasEnteredLifecycle() const { return m_Handle != 0 || m_AwakeInvoked || m_StartInvoked; }

			void Start(Scene& scene) override
			{
				if (!EnsureInstance(scene, true)) return;
				if (!IsEnabled()) return;
				InvokeEnableIfNeeded();
				if (!IsEnabled()) return;
				InvokeStartIfNeeded();
			}

			void Update(Scene&) override
			{
				if (!ShouldRunInCurrentMode()) {
					return;
				}
				if (m_Handle != 0) {
					FUNKE_PROFILE_SCOPE("ManagedSceneSystem");
					ScriptEngine::InvokeSceneSystemUpdate(m_Handle);
				}
			}

			// Lazy-creates managed instance and guards against double-fire across reload.
			void Awake(Scene& scene) override
			{
				if (!EnsureInstance(scene, false)) return;
				InvokeAwakeIfNeeded();
			}

			void FixedUpdate(Scene&) override
			{
				if (!ShouldRunInCurrentMode()) {
					return;
				}
				if (m_Handle != 0) {
					ScriptEngine::InvokeSceneSystemFixedUpdate(m_Handle);
				}
			}

			void OnEnable(Scene& scene) override
			{
				// If a SceneSystem starts disabled, Awake/Start are skipped by the
				// scene lifecycle. Enabling it later from code should still bring
				// the managed instance fully online before the next Update.
				if (!EnsureInstance(scene, true)) return;
				InvokeAwakeIfNeeded();
				if (!IsEnabled()) return;
				InvokeEnableIfNeeded();
				if (!IsEnabled()) return;
				InvokeStartIfNeeded();
			}

			void OnDisable(Scene&) override
			{
				if (m_Handle != 0 && m_EnableInvoked) {
					m_EnableInvoked = false;
					ScriptEngine::InvokeSceneSystemDisable(m_Handle);
				}
			}

			void OnDestroy(Scene&) override
			{
				if (m_Handle == 0) {
					return;
				}

				if (m_EnableInvoked) {
					ScriptEngine::InvokeSceneSystemDisable(m_Handle);
					m_EnableInvoked = false;
				}
				ScriptEngine::InvokeSceneSystemDestroy(m_Handle);
				ScriptEngine::DestroySceneSystemInstance(m_Handle);
				m_Handle = 0;
				m_StartInvoked = false;
				m_AwakeInvoked = false;
			}

		private:
			bool ShouldRunInCurrentMode() const
			{
				return !Application::IsEditor() || Application::GetIsPlaying();
			}

			bool EnsureInstance(Scene& scene, bool warnIfMissing)
			{
				if (!ShouldRunInCurrentMode()) {
					return false;
				}

				if (m_Handle != 0) {
					return true;
				}

				if (!ScriptEngine::SceneSystemClassExists(m_ClassName)) {
					if (warnIfMissing) {
						FNK_CORE_WARN_TAG("Scene", "SceneSystem '{}' was registered on scene '{}' but no matching class was found", m_ClassName, scene.GetName());
					}
					return false;
				}

				m_Handle = ScriptEngine::CreateSceneSystemInstance(m_ClassName, scene.GetName());
				ApplyFieldOverrides(scene);
				return m_Handle != 0;
			}

			void InvokeAwakeIfNeeded()
			{
				if (m_Handle != 0 && !m_AwakeInvoked) {
					ScriptEngine::InvokeSceneSystemAwake(m_Handle);
					m_AwakeInvoked = true;
				}
			}

			void InvokeEnableIfNeeded()
			{
				if (m_Handle != 0 && !m_EnableInvoked) {
					m_EnableInvoked = true;
					ScriptEngine::InvokeSceneSystemEnable(m_Handle);
				}
			}

			void InvokeStartIfNeeded()
			{
				if (m_Handle != 0 && !m_StartInvoked) {
					ScriptEngine::InvokeSceneSystemStart(m_Handle);
					m_StartInvoked = true;
				}
			}

			void ApplyFieldOverrides(Scene& scene)
			{
				if (m_Handle == 0) return;
				const auto* overrides = scene.GetSceneSystemFieldValues(m_ClassName);
				if (!overrides) return;

				// Entity-ref fields (e.g. a Transform2D pointing at the Player) resolve
				// against ScriptEngine::GetScene(). In the standalone runtime these scene-
				// system overrides are applied during the scene's Awake pass, BEFORE the
				// scene is registered as loaded/active, so without a current-scene scope
				// ResolveEntityReference has nothing to search and the ref silently
				// resolves to null (the editor dodges this by applying scene-system fields
				// at play-start, after the scene is live). Point the current scene at this
				// one for the duration of the apply, mirroring ScriptSystem's ScriptSceneScope.
				Scene* const previousScene = ScriptEngine::GetScene();
				ScriptEngine::SetScene(&scene);
				for (const auto& [fieldName, value] : *overrides) {
					ScriptEngine::SetSceneSystemField(m_Handle, fieldName.c_str(), value.c_str());
				}
				ScriptEngine::SetScene(previousScene);
			}

			std::string m_ClassName;
			uint32_t m_Handle = 0;
			bool m_EnableInvoked = false;
			bool m_AwakeInvoked = false; // H7
			bool m_StartInvoked = false;
		};

		bool IsManagedSceneSystem(const std::unique_ptr<SceneSystem>& system)
		{
			return dynamic_cast<ManagedSceneSystem*>(system.get()) != nullptr;
		}
	}

	void Scene::AddDefaultTransform(EntityHandle entity) {
#if FUNKE_WITH_2D
		if (EngineModules::IsActive(EngineModule::TwoD)) {
			AddComponent<Transform2DComponent>(entity);
			return;
		}
#endif
#if FUNKE_WITH_3D
		if (EngineModules::IsActive(EngineModule::ThreeD)) {
			AddComponent<Transform3DComponent>(entity);
		}
#endif
		(void)entity;
	}

	Entity Scene::CreateEntity() {
		auto entityHandle = CreateEntityHandle(EntityOrigin::Scene);
		AddDefaultTransform(entityHandle);
		return Entity(entityHandle, *this);
	}
	Entity Scene::CreateEntity(const std::string& name) {
		auto entityHandle = CreateEntityHandle(EntityOrigin::Scene);
		AddDefaultTransform(entityHandle);
		if (!name.empty()) {
			AddComponent<NameComponent>(entityHandle, name);
		}
		return Entity(entityHandle, *this);
	}
	Entity Scene::CreateEmptyEntity() {
		auto entityHandle = CreateEntityHandle(EntityOrigin::Scene);
		return Entity(entityHandle, *this);
	}

	Entity Scene::CreateRuntimeEntity() {
		auto entityHandle = CreateEntityHandle(EntityOrigin::Runtime);
		AddDefaultTransform(entityHandle);
		return Entity(entityHandle, *this);
	}

	Entity Scene::CreateRuntimeEntity(const std::string& name) {
		auto entityHandle = CreateEntityHandle(EntityOrigin::Runtime);
		AddDefaultTransform(entityHandle);
		if (!name.empty()) {
			AddComponent<NameComponent>(entityHandle, name);
		}
		return Entity(entityHandle, *this);
	}

	Entity Scene::CreatePrefabInstance(AssetGUID prefabGuid, const std::string& name) {
		// Instantiate through the serializer so prefab component data, identity, and overrides follow one path.
		const EntityHandle entityHandle = SceneSerializer::InstantiatePrefab(*this, static_cast<uint64_t>(prefabGuid));
		if (entityHandle == entt::null) {
			FNK_CORE_WARN_TAG("Scene", "Failed to instantiate prefab '{}'", static_cast<uint64_t>(prefabGuid));
			return Entity::Null;
		}

		if (!name.empty()) {
			if (HasComponent<NameComponent>(entityHandle)) {
				GetComponent<NameComponent>(entityHandle).Name = name;
			}
			else {
				AddComponent<NameComponent>(entityHandle, name);
			}
		}
		return Entity(entityHandle, *this);
	}

	Scene::LoadGuard::LoadGuard(Scene& scene)
		: m_Scene(scene)
		// Only the outermost guard owns the suppression scope; inner guards become no-ops.
		, m_OwnsSuppression(!scene.m_SuppressConstructHooks)
	{
		if (m_OwnsSuppression) {
			m_Scene.m_SuppressConstructHooks = true;
		}
	}

	Scene::LoadGuard::~LoadGuard() {
		if (!m_OwnsSuppression) {
			return;
		}
		// Clear FIRST so re-invoked hooks run their body (each hook short-circuits while flag is set).
		m_Scene.m_SuppressConstructHooks = false;

		if (m_Scene.m_DeferredConstructHooks.empty()) {
			return;
		}

		// Move-out so hooks triggered during flush append to a fresh vector instead of mutating the one we're iterating.
		auto hooks = std::move(m_Scene.m_DeferredConstructHooks);
		m_Scene.m_FlushingDeferredHooks = true;
		for (auto& h : hooks) {
			(m_Scene.*h.fn)(m_Scene.m_Registry, h.entity);
		}
		m_Scene.m_FlushingDeferredHooks = false;
	}

	Scene::EnabledStateBatch::EnabledStateBatch(Scene* scene)
		: m_Scene(scene)
	{
		if (m_Scene) {
			++m_Scene->m_EnabledStateBatchDepth;
		}
	}

	Scene::EnabledStateBatch::~EnabledStateBatch()
	{
		if (m_Scene && --m_Scene->m_EnabledStateBatchDepth == 0) {
			m_Scene->FlushEnabledStateEdges();
		}
	}

	void Scene::ReserveForLoadRuntime(std::size_t entityCount,
		std::span<const std::pair<uint32_t, std::size_t>> /*perTypeCounts*/)
	{
		m_Registry.storage<EntityHandle>().reserve(entityCount);
		m_RuntimeIdToEntity.reserve(m_RuntimeIdToEntity.size() + entityCount);
		m_SceneGuidToEntity.reserve(m_SceneGuidToEntity.size() + entityCount);
		// Per-component reservation is omitted: our stable u32 ID doesn't map back to entt::id_type;
		// use the templated ReserveForLoad<T...> overload for statically-typed fast path.
	}

	void Scene::SetEntityMetaDataNoFlags(
		EntityHandle entity,
		EntityOrigin origin,
		AssetGUID prefabGuid,
		AssetGUID sceneGuid,
		EntityID runtimeId)
	{
		// Snapshot + restore m_Dirty/m_UIDirty so this is a no-op on dirty flags regardless of origin/play state; caller calls MarkAllDirtyOnce() for the batch.
		const bool savedDirty = m_Dirty;
		const bool savedUIDirty = m_UIDirty;
		SetEntityMetaData(entity, origin, prefabGuid, sceneGuid, runtimeId);
		m_Dirty = savedDirty;
		m_UIDirty = savedUIDirty;
	}

	void Scene::MarkAllDirtyOnce()
	{
		m_Dirty = true;
		m_UIDirty = true;
	}

	void Scene::CreateEntitiesBulk(std::size_t n, std::span<EntityHandle> out) {
		if (n == 0) {
			return;
		}
		FNK_CORE_ASSERT(out.size() >= n, FunkeErrorCode::InvalidValue,
			"Scene::CreateEntitiesBulk: output span is smaller than the requested count");
		// Beyond entity_mask, registry.create() wraps and the next sparse-set write corrupts memory.
		using EntityTraits = entt::entt_traits<EntityHandle>;
		constexpr std::size_t kMaxLiveEntities = static_cast<std::size_t>(EntityTraits::entity_mask);
		FNK_CORE_ASSERT(m_EntityCount + n <= kMaxLiveEntities, FunkeErrorCode::InvalidValue,
			"Scene::CreateEntitiesBulk: batch would exceed the EnTT entity cap. "
			"Raise 'entityBits' in project.json and rebuild.");
		m_Registry.storage<EntityHandle>().reserve(
			m_Registry.storage<EntityHandle>().size() + n);
		m_Registry.create(out.begin(), out.begin() + n);
		m_EntityCount += n;
	}

	EntityHandle Scene::CreateEntityHandle(EntityOrigin origin, AssetGUID prefabGuid, AssetGUID sceneGuid, EntityID runtimeId) {
		EntityHandle entityHandle = m_Registry.create();
		++m_EntityCount;
		SetEntityMetaData(entityHandle, origin, prefabGuid, sceneGuid, runtimeId);
		if (origin != EntityOrigin::Runtime && (m_IsDetached || !Application::GetIsPlaying())) {
			m_Dirty = true;
		}
		// MUST pulse in both editor and runtime: UIEventSystem preview path early-outs on !IsUIDirty(),
		// so newly-created widgets would render invisibly until the value was touched.
		m_UIDirty = true;
		return entityHandle;
	}

	EntityID Scene::AllocateRuntimeEntityID() {
		EntityID runtimeId = 0;
		do {
			runtimeId = static_cast<uint64_t>(UUID());
		} while (runtimeId == 0
			|| m_RuntimeIdToEntity.contains(runtimeId)
			|| m_SceneGuidToEntity.contains(runtimeId));

		return runtimeId;
	}

	void Scene::RegisterEntityIdentity(EntityHandle entity) {
		if (!m_Registry.valid(entity) || !m_Registry.all_of<EntityMetaDataComponent>(entity)) {
			return;
		}

		const EntityMetaData& metaData = m_Registry.get<EntityMetaDataComponent>(entity).MetaData;
		if (metaData.RuntimeID != 0) {
			m_RuntimeIdToEntity[metaData.RuntimeID] = entity;
		}
		if (metaData.Origin == EntityOrigin::Scene && static_cast<uint64_t>(metaData.SceneGUID) != 0) {
			m_SceneGuidToEntity[static_cast<uint64_t>(metaData.SceneGUID)] = entity;
		}
	}

	void Scene::UnregisterEntityIdentity(EntityHandle entity) {
		if (!m_Registry.valid(entity) || !m_Registry.all_of<EntityMetaDataComponent>(entity)) {
			return;
		}

		const EntityMetaData& metaData = m_Registry.get<EntityMetaDataComponent>(entity).MetaData;
		if (metaData.RuntimeID != 0) {
			auto runtimeIt = m_RuntimeIdToEntity.find(metaData.RuntimeID);
			if (runtimeIt != m_RuntimeIdToEntity.end() && runtimeIt->second == entity) {
				m_RuntimeIdToEntity.erase(runtimeIt);
			}
		}
		if (static_cast<uint64_t>(metaData.SceneGUID) != 0) {
			auto sceneIt = m_SceneGuidToEntity.find(static_cast<uint64_t>(metaData.SceneGUID));
			if (sceneIt != m_SceneGuidToEntity.end() && sceneIt->second == entity) {
				m_SceneGuidToEntity.erase(sceneIt);
			}
		}
	}

	void Scene::SetEntityMetaData(
		EntityHandle entity,
		EntityOrigin origin,
		AssetGUID prefabGuid,
		AssetGUID sceneGuid,
		EntityID runtimeId) {
		if (entity == entt::null || !m_Registry.valid(entity)) {
			return;
		}

		UnregisterEntityIdentity(entity);

		EntityMetaData metaData;
		if (m_Registry.all_of<EntityMetaDataComponent>(entity)) {
			metaData = m_Registry.get<EntityMetaDataComponent>(entity).MetaData;
		}

		metaData.RuntimeID = runtimeId != 0 ? runtimeId : (metaData.RuntimeID != 0 ? metaData.RuntimeID : AllocateRuntimeEntityID());
		metaData.Origin = origin;
		metaData.PrefabGUID = origin == EntityOrigin::Prefab ? prefabGuid : AssetGUID(0);
		metaData.SceneGUID = origin == EntityOrigin::Scene
			? (static_cast<uint64_t>(sceneGuid) != 0 ? sceneGuid : AssetGUID(UUID()))
			: AssetGUID(0);

		m_Registry.emplace_or_replace<EntityMetaDataComponent>(entity, EntityMetaDataComponent{ metaData });

		UUID legacyId = UUID(metaData.RuntimeID);
		if (metaData.Origin == EntityOrigin::Scene && static_cast<uint64_t>(metaData.SceneGUID) != 0) {
			legacyId = metaData.SceneGUID;
		}
		m_Registry.emplace_or_replace<UUIDComponent>(entity, legacyId);

		if (metaData.Origin == EntityOrigin::Prefab && static_cast<uint64_t>(metaData.PrefabGUID) != 0) {
			m_Registry.emplace_or_replace<PrefabInstanceComponent>(entity, PrefabInstanceComponent{ metaData.PrefabGUID });
		}
		else if (m_Registry.all_of<PrefabInstanceComponent>(entity)) {
			m_Registry.remove<PrefabInstanceComponent>(entity);
		}

		RegisterEntityIdentity(entity);
		if (origin != EntityOrigin::Runtime && (m_IsDetached || !Application::GetIsPlaying())) {
			m_Dirty = true;
		}
	}

	EntityMetaData* Scene::GetEntityMetaData(EntityHandle entity) {
		if (entity == entt::null || !m_Registry.valid(entity) || !m_Registry.all_of<EntityMetaDataComponent>(entity)) {
			return nullptr;
		}

		return &m_Registry.get<EntityMetaDataComponent>(entity).MetaData;
	}

	const EntityMetaData* Scene::GetEntityMetaData(EntityHandle entity) const {
		if (entity == entt::null || !m_Registry.valid(entity) || !m_Registry.all_of<EntityMetaDataComponent>(entity)) {
			return nullptr;
		}

		return &m_Registry.get<EntityMetaDataComponent>(entity).MetaData;
	}

	EntityOrigin Scene::GetEntityOrigin(EntityHandle entity) const {
		const EntityMetaData* metaData = GetEntityMetaData(entity);
		return metaData ? metaData->Origin : EntityOrigin::Runtime;
	}

	EntityID Scene::GetRuntimeID(EntityHandle entity) const {
		const EntityMetaData* metaData = GetEntityMetaData(entity);
		return metaData ? metaData->RuntimeID : 0;
	}

	AssetGUID Scene::GetSceneEntityGUID(EntityHandle entity) const {
		const EntityMetaData* metaData = GetEntityMetaData(entity);
		return metaData ? metaData->SceneGUID : AssetGUID(0);
	}

	AssetGUID Scene::GetPrefabGUID(EntityHandle entity) const {
		const EntityMetaData* metaData = GetEntityMetaData(entity);
		return metaData ? metaData->PrefabGUID : AssetGUID(0);
	}

	bool Scene::TryResolveRuntimeID(EntityID runtimeId, EntityHandle& outHandle) const {
		outHandle = entt::null;
		if (runtimeId == 0) {
			return false;
		}

		const auto it = m_RuntimeIdToEntity.find(runtimeId);
		if (it == m_RuntimeIdToEntity.end() || !IsValid(it->second)) {
			return false;
		}

		outHandle = it->second;
		return true;
	}

	uint64_t Scene::GetEntityPersistentID(EntityHandle entity) const {
		if (entity == entt::null || !m_Registry.valid(entity) || !m_Registry.all_of<UUIDComponent>(entity)) {
			return 0;
		}
		return static_cast<uint64_t>(m_Registry.get<UUIDComponent>(entity).Id);
	}

	bool Scene::SetEntityPersistentID(
		EntityHandle entity,
		uint64_t persistentId,
		EntityHandle expectedPreviousOwner) {
		if (entity == entt::null || persistentId == 0 || !m_Registry.valid(entity)
			|| !m_Registry.all_of<EntityMetaDataComponent, UUIDComponent>(entity)) {
			return false;
		}
		const auto conflicts = [entity, expectedPreviousOwner, persistentId](const auto& identities) {
			const auto it = identities.find(persistentId);
			return it != identities.end() && it->second != entity && it->second != expectedPreviousOwner;
		};
		if (conflicts(m_RuntimeIdToEntity) || conflicts(m_SceneGuidToEntity)) {
			return false;
		}

		EntityMetaData& metaData = m_Registry.get<EntityMetaDataComponent>(entity).MetaData;
		if (metaData.Origin == EntityOrigin::Scene && metaData.RuntimeID == persistentId) return false;
		auto& identities = metaData.Origin == EntityOrigin::Scene
			? m_SceneGuidToEntity
			: m_RuntimeIdToEntity;
		const uint64_t previousId = metaData.Origin == EntityOrigin::Scene
			? static_cast<uint64_t>(metaData.SceneGUID)
			: metaData.RuntimeID;
		if (previousId == persistentId) {
			identities[persistentId] = entity;
			m_Registry.get<UUIDComponent>(entity).Id = UUID(persistentId);
			return true;
		}

		auto target = identities.find(persistentId);
		if (target == identities.end()) {
			target = identities.emplace(persistentId, entity).first;
		}
		else {
			target->second = entity;
		}
		if (previousId != 0) {
			const auto previous = identities.find(previousId);
			if (previous != identities.end() && previous->second == entity) identities.erase(previous);
		}
		if (metaData.Origin == EntityOrigin::Scene) metaData.SceneGUID = AssetGUID(persistentId);
		else metaData.RuntimeID = persistentId;
		m_Registry.get<UUIDComponent>(entity).Id = UUID(persistentId);
		return true;
	}

	void Scene::PushEntityRefRemap() { ++m_EntityRefRemapDepth; }

	void Scene::PopEntityRefRemap() {
		if (m_EntityRefRemapDepth > 0 && --m_EntityRefRemapDepth == 0) {
			m_EntityRefRemap.clear();
		}
	}

	void Scene::RegisterEntityRefRemap(uint64_t oldId, uint64_t newId) {
		if (m_EntityRefRemapDepth == 0 || oldId == 0 || newId == 0 || oldId == newId) {
			return;
		}
		m_EntityRefRemap[oldId] = newId;
	}

	void Scene::RetargetEntityRefRemap(uint64_t fromId, uint64_t toId) {
		if (m_EntityRefRemapDepth == 0 || fromId == 0 || toId == 0) {
			return;
		}
		for (auto& [oldId, newId] : m_EntityRefRemap) {
			if (newId == fromId) newId = toId;
		}
	}

	uint64_t Scene::TranslateEntityRef(uint64_t id) const {
		if (m_EntityRefRemapDepth == 0 || id == 0) {
			return id;
		}
		const auto it = m_EntityRefRemap.find(id);
		return it != m_EntityRefRemap.end() ? it->second : id;
	}

	bool Scene::TryResolveEntityRef(uint64_t entityId, EntityHandle& outHandle) const {
		outHandle = entt::null;
		if (entityId == 0) {
			return false;
		}

		// During a clone/paste batch, retarget a ref that pointed into the duplicated subtree
		// to the corresponding clone; external ids are not in the map and pass through unchanged.
		entityId = TranslateEntityRef(entityId);

		if (TryResolveRuntimeID(entityId, outHandle)) {
			return true;
		}

		return TryResolveSceneGUID(AssetGUID(entityId), outHandle);
	}

	void Scene::DeferEntityRefFixup(std::function<void()> fixup) {
		if (fixup) {
			m_PendingEntityRefFixups.emplace_back(std::move(fixup));
		}
	}

	void Scene::RunPendingEntityRefFixups() {
		// Swap-and-drain in a loop: fixups can enqueue more fixups (forward refs resolved incrementally).
		while (!m_PendingEntityRefFixups.empty()) {
			std::vector<std::function<void()>> batch;
			batch.swap(m_PendingEntityRefFixups);
			for (auto& fn : batch) {
				if (fn) fn();
			}
		}
	}

	void Scene::RunPendingEntityRefFixupsFrom(std::size_t startIndex) {
		// Run only [startIndex, end). Fixups always append (DeferEntityRefFixup -> emplace_back), so any
		// new ones land past startIndex and get drained on a later loop pass; entries before startIndex are
		// left untouched for the outer drain.
		while (m_PendingEntityRefFixups.size() > startIndex) {
			std::vector<std::function<void()>> batch;
			batch.reserve(m_PendingEntityRefFixups.size() - startIndex);
			for (std::size_t i = startIndex; i < m_PendingEntityRefFixups.size(); ++i) {
				batch.push_back(std::move(m_PendingEntityRefFixups[i]));
			}
			m_PendingEntityRefFixups.erase(
				m_PendingEntityRefFixups.begin() + static_cast<std::ptrdiff_t>(startIndex),
				m_PendingEntityRefFixups.end());
			for (auto& fn : batch) {
				if (fn) fn();
			}
		}
	}

	void Scene::DiscardPendingEntityRefFixupsFrom(std::size_t startIndex) {
		if (startIndex < m_PendingEntityRefFixups.size()) {
			m_PendingEntityRefFixups.resize(startIndex);
		}
	}

	bool Scene::TryResolveSceneGUID(AssetGUID sceneGuid, EntityHandle& outHandle) const {
		outHandle = entt::null;
		const uint64_t guidValue = static_cast<uint64_t>(sceneGuid);
		if (guidValue == 0) {
			return false;
		}

		const auto it = m_SceneGuidToEntity.find(guidValue);
		if (it == m_SceneGuidToEntity.end() || !IsValid(it->second)) {
			return false;
		}

		outHandle = it->second;
		return true;
	}

	void Scene::DestroyEntity(Entity entity) { DestroyEntity(entity.GetHandle()); }
	void Scene::DestroyEntity(EntityHandle nativeEntity) { DestroyEntityInternal(nativeEntity, true); }

	void Scene::DestroyEntity(EntityHandle nativeEntity, float delay) {
		if (delay <= 0.0f) {
			DestroyEntityInternal(nativeEntity, true);
			return;
		}
		if (!m_Registry.valid(nativeEntity)) return;
		// Keep the shorter remaining time if already queued so the earliest scheduled destruction wins.
		for (auto& [h, remaining] : m_PendingDestroys) {
			if (h == nativeEntity) {
				if (delay < remaining) remaining = delay;
				return;
			}
		}
		m_PendingDestroys.emplace_back(nativeEntity, delay);
	}

	Entity Scene::CloneEntity(Entity source) {
		if (!source.IsValid()) return Entity::Null;
		const std::string cloneName = source.GetName() + " (Clone)";
		// Tree-aware snapshot (same path as the editor's Duplicate): clones the whole
		// child subtree and remaps entity references that point inside it.
		// SerializeEntityFull serializes only the root, which silently dropped children.
		const Json::Value snapshot = SceneSerializer::SerializeEntityForClipboard(*source.GetScene(), source.GetHandle());
		const EntityHandle cloneHandle = SceneSerializer::DeserializeEntityFromValue(
			*this, snapshot, EntityOrigin::Runtime);
		if (cloneHandle == entt::null || !IsValid(cloneHandle)) return Entity::Null;
		Entity clone(cloneHandle, *this);

		if (Application* app = Application::GetInstance(); app && app->GetSceneManager()) {
			const auto& componentRegistry = SceneManager::GetComponentRegistry();
			componentRegistry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
				if (info.category != ComponentCategory::Component || !info.has || !info.copyTo) return;
				if (info.has(source) && !info.has(clone)) info.copyTo(source, clone);
			});
		}

		if (source.HasComponent<ScriptComponent>() && HasComponent<ScriptComponent>(cloneHandle)) {
			const auto& sourceScripts = source.GetComponent<ScriptComponent>();
			auto& cloneScripts = GetComponent<ScriptComponent>(cloneHandle);
			for (const auto& [key, value] : sourceScripts.PendingFieldValues) {
				cloneScripts.PendingFieldValues.try_emplace(key, value);
			}
			for (const auto& [key, value] : sourceScripts.PendingDynamicComponentValues) {
				cloneScripts.PendingDynamicComponentValues.try_emplace(key, value);
			}
		}
		if (clone.HasComponent<NameComponent>()) clone.GetComponent<NameComponent>().Name = cloneName;
		else clone.AddComponent<NameComponent>(cloneName);
		if (Application::GetIsPlaying()) PlayOnAwakeSubtree(cloneHandle);
		return clone;
	}

	namespace {
		void PlayOnAwakeRecursive(entt::registry& registry, EntityHandle entity, int depth) {
			// Cap depth so a malformed (cyclic) hierarchy can't infinite-recurse.
			constexpr int kMaxDepth = 1024;
			if (depth > kMaxDepth || !registry.valid(entity)) return;

			// Disabled entities don't awaken — mirrors the scene-start passes' exclude<DisabledTag>.
			if (!registry.any_of<DisabledTag>(entity)) {
#if FUNKE_WITH_2D
				if (auto* particles = registry.try_get<ParticleSystem2DComponent>(entity)) particles->PlayOnAwakeIfEnabled();
#endif
#if FUNKE_WITH_3D
				if (auto* particles = registry.try_get<ParticleSystem3DComponent>(entity)) particles->PlayOnAwakeIfEnabled();
#endif
				if (auto* audio = registry.try_get<AudioSourceComponent>(entity)) audio->PlayOnAwakeIfEnabled();
			}

			if (auto* hierarchy = registry.try_get<HierarchyComponent>(entity)) {
				for (EntityHandle child : hierarchy->Children) {
					PlayOnAwakeRecursive(registry, child, depth + 1);
				}
			}
		}
	}

	void Scene::PlayOnAwakeSubtree(EntityHandle root) {
		PlayOnAwakeRecursive(m_Registry, root, 0);
	}

	void Scene::ClearEntities() {
		// Gate m_TearingDown so Camera2D destroy hooks don't re-enter RefreshMainCameraSelection mid-iteration.
		m_MainCameraEntity = entt::null;
		m_TearingDown = true;
		try {
			auto view = m_Registry.view<entt::entity>();
			std::vector<EntityHandle> entities;
			entities.reserve(view.size());

			for (EntityHandle entity : view) {
				entities.push_back(entity);
			}

			for (EntityHandle entity : entities) {
				if (m_Registry.valid(entity)) {
					DestroyEntityInternal(entity, false);
				}
			}
			if (Application* app = Application::GetInstance(); app && app->GetSceneManager()) {
				SceneManager::GetComponentRegistry().ScrubRegistry(m_Registry);
			}
		}
		catch (...) {
			m_TearingDown = false;
			throw;
		}

		m_TearingDown = false;
		m_TransformHierarchyDirty = true;
		m_TransformDirtyAll = true;
		m_DirtyTransformEntities.clear();
		m_DirtyTransformEntitySet.clear();
		m_PendingDestroys.clear();
		// Fixups hold raw pointers into component storage that was just destroyed.
		m_PendingEntityRefFixups.clear();
		MarkStaticRenderDataDirty();
#if FUNKE_WITH_2D
		Renderer2D::ClearSceneCache(this);
#endif
	}

	bool Scene::BeginContentReplacement(bool& restartLifecycle) {
		const bool restoreSuspendedLoadedState = m_IsLoaded && m_LifecycleSuspended;
		restartLifecycle = false;
		if (m_IsLoaded && !m_LifecycleSuspended) {
			FNK_CORE_ERROR_TAG("Scene", "Active loaded content requires an owner-level staged swap");
			return false;
		}
		if (restoreSuspendedLoadedState) {
			m_IsLoaded = false;
		}

		try {
			ClearEntities();
			ClearSceneSystems();
		}
		catch (...) {
			if (restoreSuspendedLoadedState) m_IsLoaded = true;
			throw;
		}
		if (restoreSuspendedLoadedState) m_IsLoaded = true;
		return true;
	}

	void Scene::EndContentReplacement(bool restartLifecycle) {
		if (!restartLifecycle) return;

		size_t awakenedSystemCount = 0;
		try {
			AwakeSystems(&awakenedSystemCount);
			StartSystems();
			m_IsLoaded = true;
			m_LifecycleSuspended = false;
		}
		catch (...) {
			try {
				DestroyScene(awakenedSystemCount);
			}
			catch (...) {
				FNK_CORE_ERROR_TAG("Scene", "System rollback failed after content replacement");
			}
			throw;
		}
	}

	void Scene::MarkDirty() {
		// Detached scenes (the prefab-edit and prefab-inspector views) are NOT part of the
		// play-mode snapshot/restore cycle, so they must still flag dirty during play — that
		// is how their auto-save (RunPrefabAutoSaveTick / PrefabInspector) knows to write the
		// .prefab. Regular managed scenes skip dirtying in play so transient play-time entity
		// state is never saved back to disk.
		if (m_IsDetached || !Application::GetIsPlaying()) m_Dirty = true;
		m_UIDirty = true;
		MarkStaticRenderDataDirty();
	}

	bool Scene::HasSceneSystem(const std::string& className) const
	{
		return std::find(m_SceneSystemClassNames.begin(), m_SceneSystemClassNames.end(), className) != m_SceneSystemClassNames.end();
	}

	bool Scene::AddSceneSystem(const std::string& className)
	{
		if (className.empty() || HasSceneSystem(className)) {
			return false;
		}

		auto managedSystem = std::make_unique<ManagedSceneSystem>(className);
		SceneSystem* system = managedSystem.get();
		m_SceneSystemClassNames.push_back(className);
		m_Systems.push_back(std::move(managedSystem));

		if (m_IsLoaded && !m_LifecycleSuspended
			&& (!Application::IsEditor() || Application::GetIsPlaying())) {
			SceneExecutionScope executionScope(*this);
			system->Awake(*this);
			system->Start(*this);
			if (auto* gameSystem = dynamic_cast<ManagedSceneSystem*>(system);
				gameSystem && gameSystem->HasEnteredLifecycle()
				&& std::find(m_AwakenedSystems.begin(), m_AwakenedSystems.end(), system) == m_AwakenedSystems.end()) {
				m_AwakenedSystems.push_back(system);
			}
		}

		MarkDirty();
		return true;
	}

	bool Scene::RemoveSceneSystem(size_t index)
	{
		if (index >= m_SceneSystemClassNames.size()) {
			return false;
		}

		const std::string className = m_SceneSystemClassNames[index];
		for (auto it = m_Systems.begin(); it != m_Systems.end(); ++it) {
			if (auto* managedSystem = dynamic_cast<ManagedSceneSystem*>(it->get());
				managedSystem && managedSystem->GetClassName() == className) {
				if (m_IsLoaded && !m_LifecycleSuspended) {
					SceneExecutionScope executionScope(*this);
					managedSystem->OnDisable(*this);
					managedSystem->OnDestroy(*this);
				}
				m_AwakenedSystems.erase(std::remove(m_AwakenedSystems.begin(), m_AwakenedSystems.end(), managedSystem), m_AwakenedSystems.end());
				m_Systems.erase(it);
				break;
			}
		}

		m_SceneSystemClassNames.erase(m_SceneSystemClassNames.begin() + static_cast<std::ptrdiff_t>(index));
		m_SceneSystemFieldOverrides.erase(className);
		MarkDirty();
		return true;
	}

	bool Scene::MoveSceneSystem(size_t fromIndex, size_t toIndex)
	{
		if (fromIndex >= m_SceneSystemClassNames.size() || toIndex >= m_SceneSystemClassNames.size() || fromIndex == toIndex) {
			return false;
		}

		// Reorder the names list.
		const std::string movedClassName = m_SceneSystemClassNames[fromIndex];
		m_SceneSystemClassNames.erase(m_SceneSystemClassNames.begin() + static_cast<std::ptrdiff_t>(fromIndex));
		m_SceneSystemClassNames.insert(m_SceneSystemClassNames.begin() + static_cast<std::ptrdiff_t>(toIndex), movedClassName);

		// Reorder managed systems to match new name order WITHOUT firing OnDisable/OnDestroy — drag-reorder must not trigger lifecycle callbacks.
		std::vector<std::unique_ptr<SceneSystem>> managedExtracted;
		std::vector<size_t> managedSlots;
		managedExtracted.reserve(m_SceneSystemClassNames.size());
		managedSlots.reserve(m_SceneSystemClassNames.size());

		for (size_t i = 0; i < m_Systems.size(); ++i) {
			if (IsManagedSceneSystem(m_Systems[i])) {
				managedExtracted.push_back(std::move(m_Systems[i]));
				managedSlots.push_back(i);
			}
		}

		std::vector<std::unique_ptr<SceneSystem>> managedReordered;
		managedReordered.reserve(m_SceneSystemClassNames.size());
		for (const std::string& className : m_SceneSystemClassNames) {
			bool placed = false;
			for (auto it = managedExtracted.begin(); it != managedExtracted.end(); ++it) {
				if (!*it) continue;
				auto* mg = static_cast<ManagedSceneSystem*>(it->get());
				if (mg->GetClassName() == className) {
					managedReordered.push_back(std::move(*it));
					placed = true;
					break;
				}
			}
			if (!placed) {
				managedReordered.push_back(std::make_unique<ManagedSceneSystem>(className));
			}
		}

		for (size_t i = 0; i < managedSlots.size() && i < managedReordered.size(); ++i) {
			m_Systems[managedSlots[i]] = std::move(managedReordered[i]);
		}
		for (size_t i = managedSlots.size(); i < managedReordered.size(); ++i) {
			m_Systems.push_back(std::move(managedReordered[i]));
		}

		// Purge m_AwakenedSystems raw pointers that match leftover managedExtracted entries before they dangle.
		std::erase_if(m_AwakenedSystems, [&](SceneSystem* awakened) {
			for (const auto& leftover : managedExtracted) {
				if (leftover && leftover.get() == awakened) {
					return true;
				}
			}
			return false;
		});

		MarkDirty();
		return true;
	}

	void Scene::ClearSceneSystems()
	{
		for (auto it = m_Systems.begin(); it != m_Systems.end(); ) {
			if (IsManagedSceneSystem(*it)) {
				SceneSystem* system = it->get();
				if (m_IsLoaded && !m_LifecycleSuspended) {
					SceneExecutionScope executionScope(*this);
					system->OnDisable(*this);
					system->OnDestroy(*this);
				}
				m_AwakenedSystems.erase(std::remove(m_AwakenedSystems.begin(), m_AwakenedSystems.end(), system), m_AwakenedSystems.end());
				it = m_Systems.erase(it);
			}
			else {
				++it;
			}
		}
		m_SceneSystemClassNames.clear();
		m_SceneSystemFieldOverrides.clear();
	}

	uint32_t Scene::GetSceneSystemHandle(const std::string& className) const
	{
		for (const auto& sysPtr : m_Systems) {
			const auto* managedSystem = dynamic_cast<const ManagedSceneSystem*>(sysPtr.get());
			if (managedSystem && managedSystem->GetClassName() == className) {
				return managedSystem->GetHandle();
			}
		}
		return 0;
	}

	void Scene::SetSceneSystemFieldValue(const std::string& className, const std::string& fieldName, const std::string& value)
	{
		m_SceneSystemFieldOverrides[className][fieldName] = value;
	}

	const std::unordered_map<std::string, std::string>* Scene::GetSceneSystemFieldValues(const std::string& className) const
	{
		auto it = m_SceneSystemFieldOverrides.find(className);
		if (it == m_SceneSystemFieldOverrides.end()) return nullptr;
		return &it->second;
	}

	void Scene::ClearSceneSystemFieldOverrides(const std::string& className)
	{
		m_SceneSystemFieldOverrides.erase(className);
	}

	bool Scene::IsSceneSystemEnabled(const std::string& className) const
	{
		if (className.empty()) {
			return false;
		}

		for (const auto& system : m_Systems) {
			const auto* managedSystem = dynamic_cast<const ManagedSceneSystem*>(system.get());
			if (managedSystem && managedSystem->GetClassName() == className) {
				return managedSystem->IsEnabled();
			}
		}

		return false;
	}

	bool Scene::SetSceneSystemEnabled(const std::string& className, bool enabled)
	{
		if (className.empty()) {
			return false;
		}

		for (auto& system : m_Systems) {
			auto* managedSystem = dynamic_cast<ManagedSceneSystem*>(system.get());
			if (!managedSystem || managedSystem->GetClassName() != className) {
				continue;
			}

			if (managedSystem->IsEnabled() == enabled) {
				return true;
			}

			managedSystem->SetEnabled(enabled, *this);
			SceneSystem* systemBase = managedSystem;
			if (enabled && managedSystem->HasEnteredLifecycle()
				&& std::find(m_AwakenedSystems.begin(), m_AwakenedSystems.end(), systemBase) == m_AwakenedSystems.end()) {
				m_AwakenedSystems.push_back(systemBase);
			}

			MarkDirty();
			return true;
		}

		return false;
	}

	void Scene::StartManagedSceneSystemsForPlayMode()
	{
		if (m_LifecycleSuspended) return;
		SceneExecutionScope executionScope(*this);
		for (const auto& systemPointer : m_Systems) {
			if (!systemPointer || !systemPointer->IsEnabled() || !IsManagedSceneSystem(systemPointer)) {
				continue;
			}

			SceneSystem* system = systemPointer.get();
			system->Awake(*this);
			system->Start(*this);
			if (std::find(m_AwakenedSystems.begin(), m_AwakenedSystems.end(), system) == m_AwakenedSystems.end()) {
				m_AwakenedSystems.push_back(system);
			}
		}
	}

	Camera2DComponent* Scene::GetMainCamera() {
#if FUNKE_WITH_2D
		const auto isUsableCamera = [this](EntityHandle entity) {
			return entity != entt::null
				&& m_Registry.valid(entity)
				&& m_Registry.all_of<Camera2DComponent, Transform2DComponent>(entity)
				&& !m_Registry.all_of<DisabledTag>(entity);
		};

		if (isUsableCamera(m_MainCameraEntity)) {
			return &m_Registry.get<Camera2DComponent>(m_MainCameraEntity);
		}

		auto view = m_Registry.view<Camera2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle entity : view) {
			m_MainCameraEntity = entity;
			return &view.get<Camera2DComponent>(entity);
		}

		m_MainCameraEntity = entt::null;
		return nullptr;
#else
		return nullptr;
#endif
	}

	const Camera2DComponent* Scene::GetMainCamera() const {
#if FUNKE_WITH_2D
		// m_MainCameraEntity is `mutable` so this const overload can do
		// the same lazy-cache update without const_casting the registry.
		const auto isUsableCamera = [this](EntityHandle entity) {
			return entity != entt::null
				&& m_Registry.valid(entity)
				&& m_Registry.all_of<Camera2DComponent, Transform2DComponent>(entity)
				&& !m_Registry.all_of<DisabledTag>(entity);
		};

		if (isUsableCamera(m_MainCameraEntity)) {
			return &m_Registry.get<Camera2DComponent>(m_MainCameraEntity);
		}

		auto view = m_Registry.view<Camera2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle entity : view) {
			m_MainCameraEntity = entity;
			return &view.get<Camera2DComponent>(entity);
		}

		m_MainCameraEntity = entt::null;
		return nullptr;
#else
		return nullptr;
#endif
	}

	EntityHandle Scene::GetMainCameraEntity() {
		// Side effect: refreshes m_MainCameraEntity if cached camera is stale.
		(void)GetMainCamera();
		return m_MainCameraEntity;
	}

	bool Scene::SetMainCamera(EntityHandle entity) {
#if !FUNKE_WITH_2D
		return false;
#endif
		if (entity == entt::null
			|| !m_Registry.valid(entity)
#if FUNKE_WITH_2D
			|| !m_Registry.all_of<Camera2DComponent, Transform2DComponent>(entity)
#endif
			|| m_Registry.all_of<DisabledTag>(entity)) {
			return false;
		}

		m_MainCameraEntity = entity;
		return true;
	}

	void Scene::DestroyEntityInternal(EntityHandle nativeEntity, bool markDirty) {
		if (nativeEntity == entt::null || !m_Registry.valid(nativeEntity)) {
			return;
		}

		if (markDirty && (m_IsDetached || !Application::GetIsPlaying())) {
			m_Dirty = true;
		}
		// markDirty=false during cascade: outermost call already pulsed the flag.
		if (markDirty) {
			m_UIDirty = true;
		}

		// Snapshot Children + Parent before recursing: EnTT's swap-and-pop pool can relocate the HierarchyComponent during child destroys.
		if (m_Registry.all_of<HierarchyComponent>(nativeEntity)) {
			std::vector<EntityHandle> childrenSnapshot;
			EntityHandle parent = entt::null;
			{
				const HierarchyComponent& hc = m_Registry.get<HierarchyComponent>(nativeEntity);
				childrenSnapshot = hc.Children;
				parent = hc.Parent;
			}

			for (EntityHandle child : childrenSnapshot) {
				DestroyEntityInternal(child, false);
			}

			// Re-fetch the parent's HierarchyComponent after the recursive destroys; any
			// reference taken before the loop may now be moved-from.
			if (parent != entt::null
				&& m_Registry.valid(parent)
				&& m_Registry.all_of<HierarchyComponent>(parent))
			{
				auto& parentHc = m_Registry.get<HierarchyComponent>(parent);
				parentHc.Children.erase(
					std::remove(parentHc.Children.begin(), parentHc.Children.end(), nativeEntity),
					parentHc.Children.end());
			}
		}

		// Native-initiated destruction: drop the entity's cached C# component
		// wrappers. DestroyEntityInternal is the choke point for single, cascade-
		// child, ClearEntities, and scene-unload destroys, so covering it here covers
		// all of them. Read the UUID while the entity is still alive.
		if (!m_LifecycleSuspended && !m_SuppressEntityDestructionCallbacks) {
			NotifyScriptsEntityDestroyed(*this, GetEntityPersistentID(nativeEntity));
		}

		UnregisterEntityIdentity(nativeEntity);
		// Dynamic components don't get on_destroy hooks; scrub via Application path so detached/headless scenes don't assert.
		if (Application* app = Application::GetInstance(); app && app->GetSceneManager()) {
			SceneManager::GetComponentRegistry().ScrubEntity(m_Registry, nativeEntity);
		}
		TrackEntityDestruction(nativeEntity);
		try {
			m_Registry.destroy(nativeEntity);
		}
		catch (...) {
			UntrackEntityDestruction(nativeEntity);
			throw;
		}
		UntrackEntityDestruction(nativeEntity);
		if (m_EntityCount > 0) {
			--m_EntityCount;
		}
	}

	void Scene::TrackEntityDestruction(EntityHandle entity) {
		m_EntitiesBeingDestroyed.insert(static_cast<uint32_t>(entity));
	}

	void Scene::UntrackEntityDestruction(EntityHandle entity) {
		m_EntitiesBeingDestroyed.erase(static_cast<uint32_t>(entity));
	}

	bool Scene::IsEntityBeingDestroyed(EntityHandle entity) const {
		return m_EntitiesBeingDestroyed.contains(static_cast<uint32_t>(entity));
	}

	void Scene::RunLifecycleSystems(const std::function<void(SceneSystem&)>& func, size_t* enteredSystemCount) {
		for (const auto& systemPointer : m_Systems) {
			SceneSystem& system = *systemPointer;
			if (!system.IsEnabled()) {
				continue;
			}

			if (enteredSystemCount) {
				++(*enteredSystemCount);
			}

			func(system);
		}
	}

	void Scene::AwakeSystems(size_t* enteredSystemCount) {
		SceneExecutionScope executionScope(*this);
		m_AwakenedSystems.clear();
		m_AwakenedSystems.reserve(m_Systems.size());
		for (const auto& systemPointer : m_Systems) {
			SceneSystem& system = *systemPointer;
			if (!system.IsEnabled()) {
				continue;
			}

			m_AwakenedSystems.push_back(&system);
			if (enteredSystemCount) {
				++(*enteredSystemCount);
			}
			system.Awake(*this);
		}
	}

	void Scene::StartSystems(size_t* enteredSystemCount) {
		SceneExecutionScope executionScope(*this);
		RunLifecycleSystems([this](SceneSystem& s) { s.Start(*this); }, enteredSystemCount);
	}

	void Scene::InitializeRuntimeComponentState() {
#if FUNKE_WITH_2D
		if (PhysicsSystem2D::IsInitialized()) {
			for (EntityHandle entity : m_Registry.view<Rigidbody2DComponent>()) {
				OnRigidBody2DComponentConstruct(m_Registry, entity);
			}
			for (EntityHandle entity : m_Registry.view<BoxCollider2DComponent>()) {
				OnBoxCollider2DComponentConstruct(m_Registry, entity);
			}
			for (EntityHandle entity : m_Registry.view<CircleCollider2DComponent>()) {
				OnCircleCollider2DComponentConstruct(m_Registry, entity);
			}
			for (EntityHandle entity : m_Registry.view<PolygonCollider2DComponent>()) {
				OnPolygonCollider2DComponentConstruct(m_Registry, entity);
			}
			for (auto&& [entity, rigidbody] : m_Registry.view<Rigidbody2DComponent>().each()) {
				(void)entity;
				rigidbody.RestoreBodyFromState();
			}
		}
		for (EntityHandle entity : m_Registry.view<Camera2DComponent>()) {
			OnCamera2DComponentConstruct(m_Registry, entity);
		}
		for (EntityHandle entity : m_Registry.view<ParticleSystem2DComponent>()) {
			OnParticleSystem2DComponentConstruct(m_Registry, entity);
		}
#endif
		SceneExtensions::NotifyRuntimeStateInitialized(*this);
	}

	void Scene::UpdateSystems() {
		SceneExecutionScope executionScope(*this);
		// Drain delayed destroys BEFORE system updates so elapsed entities are gone when views are iterated.
		if (!m_PendingDestroys.empty()) {
			const float dt = Time::GetDeltaTime();
			for (size_t i = 0; i < m_PendingDestroys.size();) {
				auto& [h, remaining] = m_PendingDestroys[i];
				remaining -= dt;
				if (remaining <= 0.0f) {
					if (m_Registry.valid(h)) {
						DestroyEntityInternal(h, true);
					}
					// swap-pop; do NOT increment i — re-test slot i which
					// now holds the previously-last element.
					m_PendingDestroys[i] = m_PendingDestroys.back();
					m_PendingDestroys.pop_back();
				}
				else {
					++i;
				}
			}
		}

		ForeachEnabledSystem("Update", [this](SceneSystem& s) { s.Update(*this); });
	}

	void Scene::FixedUpdateSystems() {
		ForeachEnabledSystem("FixedUpdate", [this](SceneSystem& s) { s.FixedUpdate(*this); });
	}

	void Scene::OnPreRenderSystems() {
		ForeachEnabledSystem("OnPreRender", [this](SceneSystem& s) { s.OnPreRender(*this); });
	}

	void Scene::ForeachEnabledSystem(std::string_view phase, const std::function<void(SceneSystem&)>& func) {
		SceneExecutionScope executionScope(*this);
		for (size_t i = 0; i < m_Systems.size(); ++i) {
			SceneSystem& system = *m_Systems[i];
			if (!system.IsEnabled()) {
				continue;
			}

			try {
				func(system);
			}
			catch (const std::exception& e) {
				FNK_CORE_ERROR_TAG("Scene", "{} failed in '{}': {}", typeid(system).name(), phase, e.what());
				throw;
			}
			catch (...) {
				FNK_CORE_ERROR_TAG("Scene", "{} failed in '{}' with an unknown exception", typeid(system).name(), phase);
				throw;
			}
		}
	}

	void Scene::DestroyScene() {
		DestroyScene(std::numeric_limits<size_t>::max());
	}

	void Scene::DestroyScene(size_t awakenedSystemCount) {
		SceneExecutionScope executionScope(*this);
		std::vector<SceneSystem*> systemsToDestroy;
		systemsToDestroy.reserve(m_Systems.size());
		const bool fullTeardown = awakenedSystemCount == std::numeric_limits<size_t>::max();

		std::unordered_set<SceneSystem*> alreadyQueued;
		alreadyQueued.reserve(m_AwakenedSystems.size());

		if (!m_AwakenedSystems.empty()) {
			const size_t count = fullTeardown
				? m_AwakenedSystems.size()
				: std::min(awakenedSystemCount, m_AwakenedSystems.size());
			for (size_t i = 0; i < count; ++i) {
				SceneSystem* sys = m_AwakenedSystems[i];
				systemsToDestroy.push_back(sys);
				alreadyQueued.insert(sys);
			}
		}

		if (fullTeardown) {
			// Normal unload also destroys systems added after Awake.
			for (const auto& systemPointer : m_Systems) {
				SceneSystem* system = systemPointer.get();
				if (alreadyQueued.contains(system)) {
					continue;
				}
				const auto* managedSystem = dynamic_cast<const ManagedSceneSystem*>(system);
				if (!system->IsEnabled() && (!managedSystem || managedSystem->GetHandle() == 0)) {
					continue;
				}

				systemsToDestroy.push_back(system);
			}
		}

		std::exception_ptr firstFailure;
		for (auto it = systemsToDestroy.rbegin(); it != systemsToDestroy.rend(); ++it) {
			try {
				(*it)->OnDestroy(*this);
			}
			catch (const std::exception& e) {
				FNK_CORE_ERROR_TAG("Scene", "System error during destroy: {}", e.what());
				if (!firstFailure) {
					firstFailure = std::current_exception();
				}
			}
			catch (...) {
				FNK_CORE_ERROR_TAG("Scene", "Unknown system error during destroy");
				if (!firstFailure) {
					firstFailure = std::current_exception();
				}
			}
		}

		m_AwakenedSystems.clear();

		if (firstFailure) {
			std::rethrow_exception(firstFailure);
		}
	}

	std::unique_ptr<Scene> Scene::CreateDetachedScene(const std::string& name) {
		auto scene = std::unique_ptr<Scene>(new Scene(name, nullptr, false));
		scene->m_IsDetached = true;
		scene->m_SuppressEntityDestructionCallbacks = true;
		return scene;
	}

	Scene::Scene(const std::string& name, const SceneDefinition* definition, bool IsPersistent)
		: m_Name(name)
		, m_Definition(definition)
		, m_Persistent(IsPersistent)
		, m_IsLoaded(false) {

#if FUNKE_WITH_2D
		m_Registry.on_construct<Transform2DComponent>().connect<&Scene::OnTransform2DComponentConstruct>(this);
		m_Registry.on_destroy<Transform2DComponent>().connect<&Scene::OnTransform2DComponentDestroy>(this);
		m_Registry.on_construct<SpriteRendererComponent>().connect<&Scene::OnSpriteRendererComponentConstruct>(this);
		m_Registry.on_destroy<SpriteRendererComponent>().connect<&Scene::OnSpriteRendererComponentDestroy>(this);
		m_Registry.on_construct<Rigidbody2DComponent>().connect<&Scene::OnRigidBody2DComponentConstruct>(this);
		m_Registry.on_construct<BoxCollider2DComponent>().connect<&Scene::OnBoxCollider2DComponentConstruct>(this);
		m_Registry.on_construct<CircleCollider2DComponent>().connect<&Scene::OnCircleCollider2DComponentConstruct>(this);
		m_Registry.on_construct<PolygonCollider2DComponent>().connect<&Scene::OnPolygonCollider2DComponentConstruct>(this);
		m_Registry.on_construct<Camera2DComponent>().connect<&Scene::OnCamera2DComponentConstruct>(this);
		m_Registry.on_construct<ParticleSystem2DComponent>().connect<&Scene::OnParticleSystem2DComponentConstruct>(this);
		m_Registry.on_destroy<Rigidbody2DComponent>().connect<&Scene::OnRigidBody2DComponentDestroy>(this);
		m_Registry.on_destroy<BoxCollider2DComponent>().connect<&Scene::OnBoxCollider2DComponentDestroy>(this);
		m_Registry.on_destroy<CircleCollider2DComponent>().connect<&Scene::OnCircleCollider2DComponentDestroy>(this);
		m_Registry.on_destroy<PolygonCollider2DComponent>().connect<&Scene::OnPolygonCollider2DComponentDestroy>(this);
		m_Registry.on_destroy<RevoluteJoint2DComponent>().connect<&Scene::OnJoint2DComponentDestroy<RevoluteJoint2DComponent>>(this);
		m_Registry.on_destroy<PrismaticJoint2DComponent>().connect<&Scene::OnJoint2DComponentDestroy<PrismaticJoint2DComponent>>(this);
		m_Registry.on_destroy<WeldJoint2DComponent>().connect<&Scene::OnJoint2DComponentDestroy<WeldJoint2DComponent>>(this);
		m_Registry.on_destroy<Camera2DComponent>().connect<&Scene::OnCamera2DComponentDestruct>(this);
		m_Registry.on_destroy<ParticleSystem2DComponent>().connect<&Scene::OnParticleSystem2DComponentDestruct>(this);
#endif
		m_Registry.on_construct<DisabledTag>().connect<&Scene::OnDisabledTagConstruct>(this);
		m_Registry.on_construct<StaticTag>().connect<&Scene::OnStaticTagConstruct>(this);

		m_Registry.on_destroy<AudioSourceComponent>().connect<&Scene::OnAudioSourceComponentDestroy>(this);
		m_Registry.on_destroy<ScriptComponent>().connect<&Scene::OnScriptComponentDestroy>(this);
		m_Registry.on_destroy<DisabledTag>().connect<&Scene::OnDisabledTagDestroy>(this);
		m_Registry.on_destroy<StaticTag>().connect<&Scene::OnStaticTagDestroy>(this);

		SceneExtensions::NotifySceneCreated(*this);
	}

	Scene::~Scene()
	{
		// Empty the registry while every Scene member is still alive. Otherwise the
		// implicit dtor destroys m_Registry last, firing on_destroy hooks (camera,
		// transform, physics) that read/write already-destroyed members — UB. The
		// teardown gate stops the Camera2D hook from scanning a dying registry.
		// Normal scenes are already cleared by SceneManager::ReleaseScene (no-op here);
		// detached scenes (prefab inspector) reach this path with live entities.
		m_TearingDown = true;
		m_MainCameraEntity = entt::null;
		try {
			SceneExtensions::NotifySceneDestroyed(*this);
		}
		catch (...) {
		}
		try {
			if (!m_LifecycleSuspended && !m_SuppressEntityDestructionCallbacks) {
				m_Registry.view<UUIDComponent>().each([this](const UUIDComponent& uuid) {
					NotifyScriptsEntityDestroyed(*this, static_cast<uint64_t>(uuid.Id));
				});
			}
			m_Registry.clear();
		}
		catch (...) {
			// A destructor must not throw; destroy hooks are individually guarded,
			// but swallow defensively so teardown always completes.
		}
		try {
			if (Application* app = Application::GetInstance(); app && app->GetSceneManager()) {
				SceneManager::GetComponentRegistry().ScrubRegistry(m_Registry);
			}
		}
		catch (...) {
		}
		m_EntityLifetime.reset();
	}

	void Scene::MarkTransformDirty(EntityHandle entity)
	{
		m_TransformHierarchyDirty = true;

		if (entity == entt::null || !m_Registry.valid(entity)) {
			// No specific entity (e.g. a raw component pointer handed to C#): the
			// propagation pass must reprocess every root, including in flat scenes.
			m_TransformDirtyAll = true;
			return;
		}

		if (m_DirtyTransformEntitySet.insert(entity).second) {
			m_DirtyTransformEntities.push_back(entity);
		}

		if (m_Registry.all_of<StaticTag>(entity)) {
			MarkStaticRenderDataDirty();
		}
	}

	std::vector<EntityHandle> Scene::ConsumeDirtyTransformEntities()
	{
		m_TransformHierarchyDirty = false;
		m_TransformDirtyAll = false;
		m_DirtyTransformEntitySet.clear();

		std::vector<EntityHandle> dirty;
		dirty.swap(m_DirtyTransformEntities);
		return dirty;
	}

	void Scene::MarkStaticRenderDataDirty()
	{
		++m_StaticRenderDataVersion;
		if (m_StaticRenderDataVersion == 0) {
			m_StaticRenderDataVersion = 1;
		}
	}

	// Dynamic sprites aren't in the static grid; bumping for them would rebuild it on every spawn/despawn.
	// Their draw-index lookup simply misses and falls back to group order until the next real rebuild.
	void Scene::MarkStaticRenderDataDirtyIfStatic(const entt::registry& registry, EntityHandle entity)
	{
		if (registry.valid(entity) && registry.all_of<StaticTag>(entity)) {
			MarkStaticRenderDataDirty();
		}
	}

	void Scene::MarkHierarchyStructureDirty()
	{
		++m_HierarchyVersion;
		if (m_HierarchyVersion == 0) {
			m_HierarchyVersion = 1;
		}
	}

#if FUNKE_WITH_2D
	namespace {
		// Lets a 2D component adopt a missing Transform2D, except on a 3D or UI entity: that transform would sit
		// next to the entity's own, so the component stays inert instead (the editor refuses the mix up front).
		bool EnsureTransform2D(entt::registry& registry, EntityHandle entity, const char* component) {
			if (registry.all_of<Transform2DComponent>(entity)) return true;
			const char* otherTransform = nullptr;
#if FUNKE_WITH_3D
			if (registry.all_of<Transform3DComponent>(entity)) otherTransform = "Transform3D";
#endif
			if (!otherTransform && registry.all_of<RectTransform2DComponent>(entity)) otherTransform = "RectTransform2D";
			if (otherTransform) {
				FNK_CORE_WARN_TAG("Scene", "{} on entity {} needs a Transform2D, but the entity uses {}; the component stays inactive.",
					component, static_cast<uint32_t>(entity), otherTransform);
				return false;
			}
			FNK_CORE_WARN_TAG("Scene", "Adding missing Transform2DComponent before creating {} for entity {}",
				component, static_cast<uint32_t>(entity));
			registry.emplace<Transform2DComponent>(entity);
			return true;
		}
	}
#endif

#if FUNKE_WITH_2D
	void Scene::OnTransform2DComponentConstruct(entt::registry& registry, EntityHandle entity)
	{
		if (m_SuppressConstructHooks) {
			m_DeferredConstructHooks.push_back({ &Scene::OnTransform2DComponentConstruct, entity });
			return;
		}
		auto& transform = registry.get<Transform2DComponent>(entity);
		transform.BindOwner(this, entity);
		transform.MarkDirty();
		MarkStaticRenderDataDirtyIfStatic(registry, entity);
	}

	void Scene::OnTransform2DComponentDestroy(entt::registry& registry, EntityHandle entity)
	{
		m_TransformHierarchyDirty = true;
		MarkStaticRenderDataDirtyIfStatic(registry, entity);
	}

	void Scene::OnSpriteRendererComponentConstruct(entt::registry& registry, EntityHandle entity)
	{
		if (m_SuppressConstructHooks) {
			m_DeferredConstructHooks.push_back({ &Scene::OnSpriteRendererComponentConstruct, entity });
			return;
		}
		MarkStaticRenderDataDirtyIfStatic(registry, entity);
	}

	void Scene::OnSpriteRendererComponentDestroy(entt::registry& registry, EntityHandle entity)
	{
		MarkStaticRenderDataDirtyIfStatic(registry, entity);
	}

	void Scene::OnRigidBody2DComponentConstruct(entt::registry& registry, EntityHandle entity)
	{
		// Body adoption invariant: rigidbody adopts an existing collider's b2BodyId; the destroyer that calls b2DestroyBody MUST zero the sibling's m_BodyId to prevent aliasing on ID recycle.
		// No physics world while the 2D module is off: the component keeps its data but stays inert.
		if (m_IsDetached || m_LifecycleSuspended || !PhysicsSystem2D::IsInitialized()) return;

		if (!EnsureTransform2D(registry, entity, "Rigidbody2D")) return;

		bool isEnabled = !registry.all_of<DisabledTag>(entity);
		Rigidbody2DComponent& rb2D = registry.get<Rigidbody2DComponent>(entity);

		// Honor the component's body type instead of forcing Dynamic. For a freshly-added
		// rigidbody m_BodyType defaults to Dynamic, so editor/runtime/slow-load behavior is
		// unchanged; only an ECB fast-spawn (which byte-restores m_BodyType before the body
		// exists) differs — a prefab authored Static now spawns Static instead of Dynamic.
		if (HasAnyComponent<BoxCollider2DComponent>(entity)
			&& b2Body_IsValid(GetComponent<BoxCollider2DComponent>(entity).m_BodyId)) {
			rb2D.m_BodyId = GetComponent<BoxCollider2DComponent>(entity).m_BodyId;
			rb2D.SetBodyType(rb2D.m_BodyType);
		}
		else if (HasAnyComponent<CircleCollider2DComponent>(entity)
			&& b2Body_IsValid(GetComponent<CircleCollider2DComponent>(entity).m_BodyId)) {
			rb2D.m_BodyId = GetComponent<CircleCollider2DComponent>(entity).m_BodyId;
			rb2D.SetBodyType(rb2D.m_BodyType);
		}
		else if (HasAnyComponent<PolygonCollider2DComponent>(entity)
			&& b2Body_IsValid(GetComponent<PolygonCollider2DComponent>(entity).m_BodyId)) {
			rb2D.m_BodyId = GetComponent<PolygonCollider2DComponent>(entity).m_BodyId;
			rb2D.SetBodyType(rb2D.m_BodyType);
		}
		else {
			rb2D.m_BodyId = PhysicsSystem2D::GetMainPhysicsWorld().CreateBody(entity, *this, rb2D.m_BodyType);
		}

		rb2D.SetEnabled(isEnabled);
	}

	void Scene::OnRigidBody2DComponentDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		auto& rb2D = GetComponent<Rigidbody2DComponent>(entity);

		if (!rb2D.IsValid()) return;

		const bool hasAnyCollider =
			registry.all_of<BoxCollider2DComponent>(entity) ||
			registry.all_of<CircleCollider2DComponent>(entity) ||
			registry.all_of<PolygonCollider2DComponent>(entity);

		if (IsEntityBeingDestroyed(entity)) {
			if (registry.all_of<BoxCollider2DComponent>(entity)) {
				registry.get<BoxCollider2DComponent>(entity).Destroy();
				rb2D.m_BodyId = b2_nullBodyId;
				return;
			}
			if (registry.all_of<CircleCollider2DComponent>(entity)) {
				registry.get<CircleCollider2DComponent>(entity).Destroy();
				rb2D.m_BodyId = b2_nullBodyId;
				return;
			}
			if (registry.all_of<PolygonCollider2DComponent>(entity)) {
				registry.get<PolygonCollider2DComponent>(entity).Destroy();
				rb2D.m_BodyId = b2_nullBodyId;
				return;
			}
		}

		if (hasAnyCollider) {
			rb2D.SetBodyType(BodyType::Static);
		}
		else {
			rb2D.Destroy();
			// Defensive: rb2D.Destroy() destroys the body. Make sure any mirror is
			// cleared even if the collider branch above didn't run.
			rb2D.m_BodyId = b2_nullBodyId;
		}
	}

	void Scene::OnBoxCollider2DComponentConstruct(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached || m_LifecycleSuspended || !PhysicsSystem2D::IsInitialized()) return;
		if (!EnsureTransform2D(registry, entity, "BoxCollider2D")) return;

		bool isEnabled = !registry.all_of<DisabledTag>(entity);
		BoxCollider2DComponent& boxCollider = GetComponent<BoxCollider2DComponent>(entity);
		boxCollider.m_EntityHandle = entity;

		// Reuse existing rigidbody if present, else spawn a static body.
		if (HasComponent<Rigidbody2DComponent>(entity)) {
			auto& rb = GetComponent<Rigidbody2DComponent>(entity);
			boxCollider.m_BodyId = rb.GetBodyHandle();
		}
		else {
			boxCollider.m_BodyId = PhysicsSystem2D::GetMainPhysicsWorld().CreateBody(entity, *this, BodyType::Static);
		}

		boxCollider.m_ShapeId = PhysicsSystem2D::GetMainPhysicsWorld().CreateShape(entity, *this, boxCollider.m_BodyId, ShapeType::Square);
		boxCollider.SetEnabled(isEnabled);

		// The shape attach made Box2D recompute mass from density*area; restore an
		// explicit mass if the rigidbody pinned one.
		if (HasComponent<Rigidbody2DComponent>(entity)) {
			GetComponent<Rigidbody2DComponent>(entity).ApplyMassFromState();
		}
	}

	void Scene::OnBoxCollider2DComponentDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		auto& boxCollider2D = GetComponent<BoxCollider2DComponent>(entity);
		// Rigidbody2D owns body lifetime when present; collider must NOT call b2DestroyBody or both hooks double-free on ID recycle.
		if (registry.all_of<Rigidbody2DComponent>(entity)) {
			boxCollider2D.DestroyShape(false);
		}
		else {
			boxCollider2D.Destroy();
		}
		if (registry.all_of<Rigidbody2DComponent>(entity)) {
			auto& rb = registry.get<Rigidbody2DComponent>(entity);
			if (!b2Body_IsValid(rb.m_BodyId)) {
				rb.m_BodyId = b2_nullBodyId;
			}
		}
	}

	void Scene::OnCircleCollider2DComponentConstruct(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached || m_LifecycleSuspended || !PhysicsSystem2D::IsInitialized()) return;
		if (!EnsureTransform2D(registry, entity, "CircleCollider2D")) return;

		bool isEnabled = !registry.all_of<DisabledTag>(entity);
		CircleCollider2DComponent& circleCollider = GetComponent<CircleCollider2DComponent>(entity);
		circleCollider.m_EntityHandle = entity;

		// Reuse existing rigidbody body if present, else spawn a static body —
		// mirrors BoxCollider2D's body-sharing rules.
		if (HasComponent<Rigidbody2DComponent>(entity)) {
			auto& rb = GetComponent<Rigidbody2DComponent>(entity);
			circleCollider.m_BodyId = rb.GetBodyHandle();
		}
		else {
			circleCollider.m_BodyId = PhysicsSystem2D::GetMainPhysicsWorld().CreateBody(entity, *this, BodyType::Static);
		}

		circleCollider.m_ShapeId = PhysicsSystem2D::GetMainPhysicsWorld().CreateShape(entity, *this, circleCollider.m_BodyId, ShapeType::Circle);
		circleCollider.SetEnabled(isEnabled);

		if (HasComponent<Rigidbody2DComponent>(entity)) {
			GetComponent<Rigidbody2DComponent>(entity).ApplyMassFromState();
		}
	}

	void Scene::OnCircleCollider2DComponentDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		auto& circleCollider = GetComponent<CircleCollider2DComponent>(entity);
		// See OnBoxCollider2DComponentDestroy: collider does NOT own the body when
		// a Rigidbody2D is present.
		if (registry.all_of<Rigidbody2DComponent>(entity)) {
			circleCollider.DestroyShape(false);
		}
		else {
			circleCollider.Destroy();
		}
		if (registry.all_of<Rigidbody2DComponent>(entity)) {
			auto& rb = registry.get<Rigidbody2DComponent>(entity);
			if (!b2Body_IsValid(rb.m_BodyId)) {
				rb.m_BodyId = b2_nullBodyId;
			}
		}
	}

	void Scene::OnPolygonCollider2DComponentConstruct(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached || m_LifecycleSuspended || !PhysicsSystem2D::IsInitialized()) return;
		if (!EnsureTransform2D(registry, entity, "PolygonCollider2D")) return;

		bool isEnabled = !registry.all_of<DisabledTag>(entity);
		PolygonCollider2DComponent& polygonCollider = GetComponent<PolygonCollider2DComponent>(entity);
		polygonCollider.m_EntityHandle = entity;

		if (HasComponent<Rigidbody2DComponent>(entity)) {
			auto& rb = GetComponent<Rigidbody2DComponent>(entity);
			polygonCollider.m_BodyId = rb.GetBodyHandle();
		}
		else {
			polygonCollider.m_BodyId = PhysicsSystem2D::GetMainPhysicsWorld().CreateBody(entity, *this, BodyType::Static);
		}

		polygonCollider.m_ShapeId = PhysicsSystem2D::GetMainPhysicsWorld().CreateShape(entity, *this, polygonCollider.m_BodyId, ShapeType::Polygon);
		polygonCollider.SetEnabled(isEnabled);

		if (HasComponent<Rigidbody2DComponent>(entity)) {
			GetComponent<Rigidbody2DComponent>(entity).ApplyMassFromState();
		}
	}

	void Scene::OnPolygonCollider2DComponentDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		auto& polygonCollider = GetComponent<PolygonCollider2DComponent>(entity);
		// See OnBoxCollider2DComponentDestroy: collider does NOT own the body when
		// a Rigidbody2D is present.
		if (registry.all_of<Rigidbody2DComponent>(entity)) {
			polygonCollider.DestroyShape(false);
		}
		else {
			polygonCollider.Destroy();
		}
		if (registry.all_of<Rigidbody2DComponent>(entity)) {
			auto& rb = registry.get<Rigidbody2DComponent>(entity);
			if (!b2Body_IsValid(rb.m_BodyId)) {
				rb.m_BodyId = b2_nullBodyId;
			}
		}
	}
	template <typename TJoint>
	void Scene::OnJoint2DComponentDestroy(entt::registry& registry, EntityHandle entity) {
		if (m_IsDetached) return;
		auto& joint = registry.get<TJoint>(entity);
		if (joint.m_Owner == entity) joint.DestroyJoint();
	}
#endif

	void Scene::OnAudioSourceComponentDestroy(entt::registry& registry, EntityHandle entity)
	{
		if (m_IsDetached) return;
		registry.get<AudioSourceComponent>(entity).Destroy();
	}

	void Scene::OnScriptComponentDestroy(entt::registry& registry, EntityHandle entity)
	{
		if (m_IsDetached || m_LifecycleSuspended || m_SuppressEntityDestructionCallbacks) return;
		(void)registry;
		ScriptSystem::RemoveAllScripts(Entity(entity, *this));
	}

#if FUNKE_WITH_2D
	void Scene::OnCamera2DComponentConstruct(entt::registry& registry, EntityHandle entity)
	{
		if (m_IsDetached || m_LifecycleSuspended) return;

		if (!EnsureTransform2D(registry, entity, "Camera2D")) return;

		Camera2DComponent& camera2D = GetComponent<Camera2DComponent>(entity);
		camera2D.Initialize(*this, entity);
		camera2D.UpdateViewport();

		if (!registry.all_of<DisabledTag>(entity)
			&& (m_MainCameraEntity == entt::null
				|| !registry.valid(m_MainCameraEntity)
				|| registry.all_of<DisabledTag>(m_MainCameraEntity))) {
			RefreshMainCameraSelection(registry, entity);
		}
	}

	void Scene::OnCamera2DComponentDestruct(entt::registry& registry, EntityHandle entity)
	{
		Camera2DComponent& camera2D = GetComponent<Camera2DComponent>(entity);
		camera2D.Destroy();

		// Skip during ClearEntities: every camera is doomed anyway; cache cleared up front.
		if (m_TearingDown) {
			return;
		}

		if (m_MainCameraEntity == entity) {
			RefreshMainCameraSelection(registry, entt::null, entity);
		}
	}
#endif

	void Scene::OnDisabledTagConstruct(entt::registry& registry, EntityHandle entity)
	{
		if (IsEntityBeingDestroyed(entity)) {
			return;
		}

		EnabledStateBatch batch(this);
		MarkStaticRenderDataDirtyIfStatic(registry, entity);
		QueueEnabledStateEdge(entity, false);

		// Snapshot children before iterating (emplace may reorder pool). InheritedDisabledTag tracks which DisabledTags this cascade owns.
		if (registry.all_of<HierarchyComponent>(entity)) {
			std::vector<EntityHandle> children = registry.get<HierarchyComponent>(entity).Children;
			for (EntityHandle child : children) {
				if (registry.valid(child) && !registry.all_of<DisabledTag>(child)) {
					registry.emplace<InheritedDisabledTag>(child);
					registry.emplace<DisabledTag>(child);
				}
			}
		}
	}

	void Scene::OnDisabledTagDestroy(entt::registry& registry, EntityHandle entity)
	{
		if (IsEntityBeingDestroyed(entity)) {
			return;
		}

		// EnTT drops the tag only after this hook returns. Without an outer batch (a raw registry removal) the
		// callbacks still see the tag; Entity::SetEnabled and SetParent batch around the removal.
		EnabledStateBatch batch(this);
		MarkStaticRenderDataDirtyIfStatic(registry, entity);
		if (registry.all_of<InheritedDisabledTag>(entity)) {
			registry.remove<InheritedDisabledTag>(entity);
		}

		QueueEnabledStateEdge(entity, true);

		if (registry.all_of<HierarchyComponent>(entity)) {
			std::vector<EntityHandle> children = registry.get<HierarchyComponent>(entity).Children;
			for (EntityHandle child : children) {
				if (registry.valid(child)
					&& registry.all_of<DisabledTag>(child)
					&& registry.all_of<InheritedDisabledTag>(child)) {
					registry.remove<InheritedDisabledTag>(child);
					registry.remove<DisabledTag>(child);
				}
			}
		}
	}

	void Scene::OnStaticTagConstruct(entt::registry&, EntityHandle entity)
	{
		if (m_SuppressConstructHooks) {
			m_DeferredConstructHooks.push_back({ &Scene::OnStaticTagConstruct, entity });
			return;
		}
		MarkStaticRenderDataDirty();
	}

	void Scene::OnStaticTagDestroy(entt::registry& registry, EntityHandle entity)
	{
		MarkStaticRenderDataDirty();
		if (registry.all_of<StaticRenderData>(entity)) {
			registry.remove<StaticRenderData>(entity);
		}
	}

#if FUNKE_WITH_2D
	void Scene::OnParticleSystem2DComponentConstruct(entt::registry& registry, EntityHandle entity) {
		if (m_LifecycleSuspended) return;
		if (m_SuppressConstructHooks) {
			m_DeferredConstructHooks.push_back({ &Scene::OnParticleSystem2DComponentConstruct, entity });
			return;
		}
		auto& ps = registry.get<ParticleSystem2DComponent>(entity);
		ps.m_EmitterScene = this;
		ps.m_EmitterEntity = entity;
	}

	void Scene::OnParticleSystem2DComponentDestruct(entt::registry& registry, EntityHandle entity) {
		auto& ps = registry.get<ParticleSystem2DComponent>(entity);
		ps.m_EmitterScene = nullptr;
		ps.m_EmitterEntity = entt::null;
	}

#endif

	void Scene::QueueEnabledStateEdge(EntityHandle entity, bool enabled)
	{
		if (m_LifecycleSuspended || m_TearingDown) {
			return;
		}

		if (auto it = m_PendingEnabledEdgeIndex.find(entity); it != m_PendingEnabledEdgeIndex.end()) {
			// Switched back before its callbacks ran: neither edge fires.
			if (m_PendingEnabledEdges[it->second].second != enabled) {
				m_PendingEnabledEdges[it->second].first = entt::null;
				m_PendingEnabledEdgeIndex.erase(it);
			}
			return;
		}

		m_PendingEnabledEdgeIndex.emplace(entity, m_PendingEnabledEdges.size());
		m_PendingEnabledEdges.emplace_back(entity, enabled);
	}

	void Scene::FlushEnabledStateEdges()
	{
		// Callbacks that toggle enabled state queue further edges; this loop runs them instead of recursing.
		if (m_FlushingEnabledEdges) {
			return;
		}

		m_FlushingEnabledEdges = true;
		for (size_t index = 0; index < m_PendingEnabledEdges.size(); ++index) {
			const auto [entity, enabled] = m_PendingEnabledEdges[index];
			if (entity == entt::null) {
				continue;
			}

			m_PendingEnabledEdgeIndex.erase(entity);
			if (!m_Registry.valid(entity)) {
				continue;
			}

			try {
				ApplyEntityEnabledState(m_Registry, entity, enabled);
				if (m_Registry.valid(entity) && m_OnEntityEnabledChanged.HasListeners()) {
					m_OnEntityEnabledChanged.Invoke(Entity(entity, *this), enabled);
				}
			}
			catch (const std::exception& e) {
				// The remaining edges must still run, or their entities keep a stale enabled state.
				FNK_CORE_ERROR_TAG("Scene", "Enabled-state callback for entity {} threw: {}", static_cast<uint32_t>(entity), e.what());
			}
		}

		m_PendingEnabledEdges.clear();
		m_PendingEnabledEdgeIndex.clear();
		m_FlushingEnabledEdges = false;
	}

	void Scene::ApplyEntityEnabledState(entt::registry& registry, EntityHandle entity, bool enabled)
	{
		if (m_IsDetached || m_LifecycleSuspended || !registry.valid(entity)) {
			return;
		}

		// Audio rides the enabled-state edge like PlayOnAwake: mute on disable, re-awaken on (re)enable while playing.
		// The DisabledTag cascade re-runs this per child, so a whole subtree mutes/awakens together.
		if (auto* audio = registry.try_get<AudioSourceComponent>(entity)) {
			if (!enabled) {
				audio->Stop();
			}
			else if (Application::GetIsPlaying()) {
				audio->PlayOnAwakeIfEnabled();
			}
		}

#if FUNKE_WITH_2D
		if (auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity); rigidbody && rigidbody->IsValid()) {
			rigidbody->SetEnabled(enabled);
		}

		if (auto* collider = registry.try_get<BoxCollider2DComponent>(entity); collider && collider->IsValid()) {
			collider->SetEnabled(enabled);
		}

		if (auto* collider = registry.try_get<CircleCollider2DComponent>(entity); collider && collider->IsValid()) {
			collider->SetEnabled(enabled);
		}

		if (auto* collider = registry.try_get<PolygonCollider2DComponent>(entity); collider && collider->IsValid()) {
			collider->SetEnabled(enabled);
		}
#endif

		if (registry.all_of<ScriptComponent>(entity)) {
			ScriptSystem::SetScriptsEnabled(Entity(entity, *this), enabled);
		}

#if FUNKE_WITH_2D
		if (!registry.all_of<Camera2DComponent>(entity)) {
			return;
		}

		if (!enabled && m_MainCameraEntity == entity) {
			RefreshMainCameraSelection(registry, entt::null, entity);
		}
		else if (enabled
			&& (m_MainCameraEntity == entt::null
				|| !registry.valid(m_MainCameraEntity)
				|| registry.all_of<DisabledTag>(m_MainCameraEntity))) {
			RefreshMainCameraSelection(registry, entity);
		}
#endif
	}

#if FUNKE_WITH_2D
	void Scene::RefreshMainCameraSelection(entt::registry& registry, EntityHandle preferred, EntityHandle excluded)
	{
		const auto isUsableCamera = [&](EntityHandle entity) {
			return entity != entt::null
				&& entity != excluded
				&& registry.valid(entity)
				&& registry.all_of<Camera2DComponent, Transform2DComponent>(entity)
				&& !registry.all_of<DisabledTag>(entity);
		};

		if (isUsableCamera(preferred)) {
			m_MainCameraEntity = preferred;
			return;
		}

		auto view = registry.view<Camera2DComponent, Transform2DComponent>(entt::exclude<DisabledTag>);
		for (EntityHandle candidate : view) {
			if (candidate != excluded) {
				m_MainCameraEntity = candidate;
				return;
			}
		}

		m_MainCameraEntity = entt::null;
	}
#endif
}
