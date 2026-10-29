#include "pch.hpp"
#include "Scripting/ScriptBindingGuard.hpp"
#include "Scripting/EntityCommandBufferWire.hpp"
#include "Scripting/EntityCommandBufferParallel.hpp"
#include "Scripting/NativeEntityCommandBuffer.hpp"
#include "Scripting/ScriptGlue.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#endif
#include "Serialization/PrefabTemplateCache.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Core/Application.hpp"
#include "Core/Log.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace Funke {

	class EcbPhaseTimings {
	public:
		EcbPhaseTimings(uint32_t entities, uint32_t commands) : m_Entities(entities), m_Commands(commands) {
			static const bool enabled = [] {
#ifdef FNK_PLATFORM_WINDOWS
				char* value = nullptr;
				size_t length = 0;
				_dupenv_s(&value, &length, "FUNKE_ECB_TIMINGS");
				const bool result = value && value[0] == '1';
				std::free(value);
				return result;
#else
				const char* value = std::getenv("FUNKE_ECB_TIMINGS");
				return value && value[0] == '1';
#endif
			}();
			m_Enabled = enabled;
			if (m_Enabled) m_Last = Clock::now();
		}
		void Mark(size_t phase) {
			if (!m_Enabled) return;
			const auto now = Clock::now();
			m_Phases[phase] = std::chrono::duration<double, std::milli>(now - m_Last).count();
			m_Last = now;
		}
		~EcbPhaseTimings() {
			if (!m_Enabled) return;
			Mark(6);
			FNK_CORE_INFO_TAG("EcbTiming", "entities={} commands={} preflight_ms={:.6f} prepare_ms={:.6f} allocate_ms={:.6f} metadata_ms={:.6f} execute_ms={:.6f} finalize_ms={:.6f} deferred_ms={:.6f}",
				m_Entities, m_Commands, m_Phases[0], m_Phases[1], m_Phases[2], m_Phases[3], m_Phases[4], m_Phases[5], m_Phases[6]);
		}
	private:
		using Clock = std::chrono::steady_clock;
		Clock::time_point m_Last;
		std::array<double, 7> m_Phases{};
		uint32_t m_Entities;
		uint32_t m_Commands;
		bool m_Enabled = false;
	};

	static Scene* EcbResolveScene()
	{
		if (Scene* scene = GetCurrentExecutionScene()) {
			return scene;
		}
		if (Scene* scene = ScriptEngine::GetScene()) {
			return scene;
		}
		return ResolveImplicitScene();
	}

	static uint32_t Funke_Component_GetTypeId(const char* componentName)
	{
		if (componentName == nullptr || componentName[0] == '\0') {
			return 0u;
		}
		const auto& registry = SceneManager::GetComponentRegistry();
		if (const ComponentInfo* hit = registry.FindBySerializedName(componentName)) {
			return hit->typeIdU32;
		}
		constexpr const char kSuffix[] = "Component";
		constexpr std::size_t kSuffixLen = sizeof(kSuffix) - 1;
		const std::size_t nameLen = std::strlen(componentName);
		if (nameLen > kSuffixLen) {
			if (std::memcmp(componentName + nameLen - kSuffixLen, kSuffix, kSuffixLen) == 0) {
				std::string stem(componentName, nameLen - kSuffixLen);
				if (const ComponentInfo* hit = registry.FindBySerializedName(stem)) {
					return hit->typeIdU32;
				}
			}
		}
		uint32_t found = 0u;
		registry.ForEachComponentInfo([&](const std::type_index&, const ComponentInfo& info) {
			if (found != 0u) return;
			if (info.displayName == componentName) {
				found = info.typeIdU32;
			}
		});
		return found;
	}

	class EcbPlaybackHandles {
	public:
		EcbPlaybackHandles() : m_OwnsShared(!s_InUse) {
			if (m_OwnsShared) s_InUse = true;
		}
		~EcbPlaybackHandles() {
			if (m_OwnsShared) s_InUse = false;
		}
		EcbPlaybackHandles(const EcbPlaybackHandles&) = delete;
		EcbPlaybackHandles& operator=(const EcbPlaybackHandles&) = delete;
		std::vector<EntityHandle>& Get() { return m_OwnsShared ? s_Handles : m_Local; }
	private:
		// Callbacks may nest main-thread playback; only the outer call may reuse shared storage.
		inline static std::vector<EntityHandle> s_Handles;
		inline static bool s_InUse = false;
		std::vector<EntityHandle> m_Local;
		bool m_OwnsShared;
	};

	static int EcbValidateCommands(const uint8_t* cursor, const uint8_t* bufferEnd, const EcbHeader& header, bool& hasPrefabs)
	{
		const ComponentRegistry& components = SceneManager::GetComponentRegistry();
		for (uint32_t command = 0; command < header.commandCount; ++command) {
			if (bufferEnd - cursor < 11) return kEcbErrorTruncated;
			const uint8_t opcode = cursor[0];
			const uint32_t entityIndex = EcbReadLE<uint32_t>(cursor + 1);
			const uint16_t payloadSize = EcbReadLE<uint16_t>(cursor + 9);
			const uint8_t* payload = cursor + 11;
			if (static_cast<std::size_t>(bufferEnd - payload) < payloadSize) return kEcbErrorTruncated;
			const bool localEntity = entityIndex < header.entityCount;
			const bool liveEntity = entityIndex == kEcbNoName;

			switch (opcode) {
			case Ecb_AddComponent:
			case Ecb_SetComponent:
			case Ecb_DefaultConstructComponent:
			case Ecb_RemoveComponent: {
				if (!localEntity && !liveEntity) return kEcbErrorTruncated;
				const size_t identitySize = liveEntity ? sizeof(uint64_t) : 0;
				if (payloadSize < identitySize) return kEcbErrorTruncated;
				const size_t componentSize = payloadSize - identitySize;
				const uint32_t typeId = EcbReadLE<uint32_t>(cursor + 5);
				const ComponentInfo* info = components.GetByTypeId(typeId);
				const bool useDefault = opcode == Ecb_DefaultConstructComponent;
				const bool remove = opcode == Ecb_RemoveComponent;
				if (!info || !info->IsModuleActive() || (remove ? !info->remove : useDefault ? !info->defaultEmplace : !info->emplaceFromBytes)) {
					return kEcbErrorUnknownComponent;
				}
				if (((useDefault || remove) && componentSize != 0)
					|| (!useDefault && !remove && info->bytePayloadSize != 0 && componentSize != info->bytePayloadSize)) {
					FNK_CORE_WARN_TAG("Ecb", "Component payload size mismatch at command {} (typeId={}, size={})",
						command, typeId, payloadSize);
					return kEcbErrorTruncated;
				}
				break;
			}
			case Ecb_InstantiatePrefab:
				if (!localEntity || payloadSize != sizeof(uint64_t)) return kEcbErrorTruncated;
				hasPrefabs = true;
				break;
			case Ecb_DestroyEntity:
				if (!(localEntity && payloadSize == 0) && !(liveEntity && payloadSize == sizeof(uint64_t))) {
					return kEcbErrorTruncated;
				}
				break;
			case Ecb_SetEntityEnabled:
				if (!(localEntity && payloadSize == sizeof(uint8_t))
					&& !(liveEntity && payloadSize == sizeof(uint8_t) + sizeof(uint64_t))) {
					return kEcbErrorTruncated;
				}
				break;
			default:
				return kEcbErrorTruncated;
			}
			cursor = payload + payloadSize;
		}
		return 0;
	}

	static int Funke_Ecb_PlaybackImpl(const uint8_t* buffer, int length,
		uint64_t* outRuntimeIds, int maxOut, int mode)
	{
		if (mode < 0 || mode > 2) return kEcbErrorInvalidMode;
		if (buffer == nullptr || length < static_cast<int>(sizeof(EcbHeader))) {
			return kEcbErrorTruncated;
		}
		Scene* scene = EcbResolveScene();
		if (scene == nullptr) {
			return kEcbErrorNoScene;
		}

		EcbHeader header;
		std::memcpy(&header, buffer, sizeof(EcbHeader));
		EcbPhaseTimings timings(header.entityCount, header.commandCount);

		// Verify the entity table fits before we trust entityCount.
		if (header.entityCount > (static_cast<std::size_t>(length) - sizeof(EcbHeader)) / sizeof(uint32_t)) {
			return kEcbErrorTruncated;
		}
		const std::size_t entityTableBytes = static_cast<std::size_t>(header.entityCount) * sizeof(uint32_t);
		if (outRuntimeIds == nullptr && header.entityCount > 0) {
			return kEcbErrorOutputTooSmall;
		}
		// Reject negative maxOut explicitly: casting it to uint32_t below would wrap to
		// a huge value, defeating the capacity guard and allowing an OOB write into a
		// caller buffer that was declared negative-sized.
		if (maxOut < 0) {
			return kEcbErrorOutputTooSmall;
		}
		if (static_cast<uint32_t>(maxOut) < header.entityCount) {
			return kEcbErrorOutputTooSmall;
		}

		const uint8_t* entityTablePtr = buffer + sizeof(EcbHeader);
		const uint8_t* commandStreamPtr = entityTablePtr + entityTableBytes;
		const uint8_t* bufferEnd = buffer + length;
		// Validate before prefab baking, which can create temporary entities and invoke scene callbacks.
		bool hasPrefabs = false;
		if (const int error = EcbValidateCommands(commandStreamPtr, bufferEnd, header, hasPrefabs); error != 0) return error;
		timings.Mark(0);

		// Keep prefab templates in command order for the execute pass.
		PrefabTemplateCache& prefabCache = PrefabTemplateCache::Get();
		struct PrefabSpawn {
			uint32_t rootEntityIndex;        // ECB-local root slot
			uint64_t prefabGuid;
			uint32_t childBaseIndex;         // filled in after bulk-create sizing
			PrefabTemplateCache::TemplateSnapshot templ;
		};
		std::vector<PrefabSpawn> prefabSpawns;
		uint64_t totalExtraChildren = 0;

		if (hasPrefabs) {
			const uint8_t* cursor = commandStreamPtr;
			for (uint32_t cmdIdx = 0; cmdIdx < header.commandCount; ++cmdIdx) {
				const uint8_t  opcode      = cursor[0];
				const uint32_t entityIndex = EcbReadLE<uint32_t>(cursor + 1);
				const uint16_t payloadSize = EcbReadLE<uint16_t>(cursor + 9);
				const uint8_t* payload     = cursor + 11;

				if (opcode == Ecb_InstantiatePrefab) {
					const uint64_t guid = EcbReadLE<uint64_t>(payload);
					auto templ = prefabCache.Find(guid);
					if (!templ) {
						// Bake-on-demand: instantiate + destroy a throwaway tree to populate the cache; cost paid once per GUID per process.
						EntityHandle scratchRoot = SceneSerializer::InstantiatePrefab(*scene, guid);
						if (scratchRoot != entt::null) {
							scene->DestroyEntity(scratchRoot);
						}
						templ = prefabCache.Find(guid);
					}
					if (!templ) {
						// Distinct from the unbakeable case: the GUID never resolved to a loadable
						// Prefab asset (SceneSerializer::InstantiatePrefab returned null), so no
						// template was baked. Entity.Instantiate(guid) would fail identically.
						FNK_CORE_WARN_TAG("Ecb",
							"Ecb_InstantiatePrefab rejected prefab {}: unknown prefab — the GUID does "
							"not resolve to a loadable Prefab asset (missing/unscanned .prefab or .meta, "
							"or a wrong/stale GUID). NOT a bake limitation; Entity.Instantiate would fail too.",
							guid);
						return kEcbErrorUnknownPrefab;
					}
					if (!templ->bakeable) {
						FNK_CORE_WARN_TAG("Ecb",
							"Ecb_InstantiatePrefab rejected prefab {}: {}",
							guid, templ->unbakeableReason);
						return kEcbErrorBadPrefab;
					}
					prefabSpawns.push_back({ entityIndex, guid, /*childBaseIndex=*/0u, templ });
					totalExtraChildren += static_cast<uint64_t>(templ->EntityCount() - 1u);
				}

				cursor = payload + payloadSize;
			}
		}

		const uint64_t totalEntityCount64 =
			static_cast<uint64_t>(header.entityCount) + totalExtraChildren;

		// Guard against exceeding EnTT's entity_mask: registry.create() at the cap wraps the index and corrupts sparse-set writes.
		using EntityTraits = entt::entt_traits<EntityHandle>;
		constexpr uint64_t kMaxLiveEntities = static_cast<uint64_t>(EntityTraits::entity_mask);
		const uint64_t currentCount = static_cast<uint64_t>(scene->GetEntityCount());
		if (currentCount + totalEntityCount64 > kMaxLiveEntities) {
			FNK_CORE_ERROR_TAG("Ecb",
				"Ecb_Playback rejected: batch would exceed entity cap "
				"(current={}, requested={}+{} prefab children, cap={}). Raise 'entityBits' in "
				"project.json and rebuild (Project Settings > Entity ID bits).",
				currentCount, header.entityCount, totalExtraChildren, kMaxLiveEntities);
			return kEcbErrorEntityCapExceeded;
		}
		const uint32_t totalEntityCount = static_cast<uint32_t>(totalEntityCount64);
		timings.Mark(1);
		if (!hasPrefabs && EcbTryParallelPlayback(*scene, header, commandStreamPtr, outRuntimeIds, mode)) {
			timings.Mark(4);
			return static_cast<int>(header.entityCount);
		}

		// Root slots [0, entityCount); prefab children [entityCount, totalEntityCount). Only roots are surfaced to managed code.
		scene->ReserveForLoadRuntime(totalEntityCount, {});

		EcbPlaybackHandles handleScratch;
		std::vector<EntityHandle>& handles = handleScratch.Get();
		handles.resize(totalEntityCount);
		scene->CreateEntitiesBulk(totalEntityCount, handles);
		timings.Mark(2);

		// Assign each prefab spawn its child block now that the layout
		// is known.
		{
			uint32_t base = header.entityCount;
			for (PrefabSpawn& spawn : prefabSpawns) {
				spawn.childBaseIndex = base;
				base += (spawn.templ->EntityCount() - 1u);
			}
		}

		Scene::LoadGuard guard(*scene);

		// Skip prefab roots: HydrateInto stamps them with Origin::Prefab; overwriting here would clobber the GUID.
		std::vector<uint8_t> isPrefabRoot(header.entityCount, 0);
		for (const PrefabSpawn& spawn : prefabSpawns) {
			isPrefabRoot[spawn.rootEntityIndex] = 1;
		}
		std::vector<uint8_t> destroyedRoot(header.entityCount, 0);
		scene->SetRuntimeEntityMetaDataBulk(std::span<const EntityHandle>(handles.data(), header.entityCount), isPrefabRoot);

		const ComponentRegistry& componentRegistry = SceneManager::GetComponentRegistry();
		timings.Mark(3);

		std::vector<EntityHandle> prefabSlotScratch;
		std::vector<EntityHandle> liveTransformWrites;
		std::size_t nextPrefabSpawn = 0;

		// Raw emplaces skip AddWithDependencies, so conflicts (e.g. a 3D component on a 2D entity) are checked here;
		// only first-time adds of components that have conflicts pay for it.
		bool warnedConflict = false;
		auto refusesConflictingAdd = [&](const ComponentInfo& info, EntityHandle handle) {
			if (info.conflictsWith.empty() || info.isDynamic || !info.has) return false;
			const ComponentInfo* conflict = componentRegistry.FindConflict(scene->GetRegistry(), handle, info);
			if (!conflict) return false;
			if (!warnedConflict) {
				warnedConflict = true;
				FNK_CORE_WARN_TAG("Ecb", "AddComponent '{}' skipped: the entity has '{}', which it can't be combined with.",
					info.displayName, conflict->displayName);
			}
			return true;
		};

		// Dynamic IComponent adds must seed a ScriptComponent.Scripts entry so the inspector renders it; idempotent.
		auto seedScriptsEntryForDynamic = [&](uint32_t typeId, EntityHandle handle) {
			const ComponentInfo* info = componentRegistry.GetByTypeId(typeId);
			if (!info || !info->isDynamic || !info->IsModuleActive()) return;
			Entity entity = scene->GetEntity(handle);
			if (!entity.HasComponent<ScriptComponent>()) {
				entity.AddComponent<ScriptComponent>();
				if (!entity.IsValid() || !entity.HasComponent<ScriptComponent>()) return;
				info = componentRegistry.GetByTypeId(typeId);
				if (!info || !info->isDynamic || !info->IsModuleActive()) return;
			}
			auto& sc = entity.GetComponent<ScriptComponent>();
			if (!sc.HasScript(info->serializedName, ScriptType::Managed)) {
				sc.AddScript(info->serializedName, ScriptType::Managed);
			}
		};

		const uint8_t* cursor = commandStreamPtr;
		for (uint32_t cmdIdx = 0; cmdIdx < header.commandCount; ++cmdIdx) {
			if (cursor + 11 > bufferEnd) {
				return kEcbErrorTruncated;
			}
			const uint8_t  opcode       = cursor[0];
			const uint32_t entityIndex  = EcbReadLE<uint32_t>(cursor + 1);
			const uint32_t typeId       = EcbReadLE<uint32_t>(cursor + 5);
			const uint16_t payloadSize  = EcbReadLE<uint16_t>(cursor + 9);
			const uint8_t* payload      = cursor + 11;
			if (payload + payloadSize > bufferEnd) {
				return kEcbErrorTruncated;
			}
			cursor = payload + payloadSize;

			const bool targetsLiveEntity = entityIndex == kEcbNoName;
			if (!targetsLiveEntity && entityIndex >= header.entityCount) {
				FNK_CORE_WARN_TAG("Ecb", "Skipping command with out-of-range entityIndex {}", entityIndex);
				continue;
			}

			if (opcode == Ecb_AddComponent || opcode == Ecb_SetComponent ||
				opcode == Ecb_DefaultConstructComponent || opcode == Ecb_RemoveComponent) {
				EntityHandle handle = entt::null;
				const uint8_t* componentBytes = payload;
				size_t componentSize = payloadSize;
				if (targetsLiveEntity) {
					if (!scene->TryResolveEntityRef(EcbReadLE<uint64_t>(payload), handle)) continue;
					componentBytes += sizeof(uint64_t);
					componentSize -= sizeof(uint64_t);
				}
				else {
					if (destroyedRoot[entityIndex] != 0) continue;
					handle = handles[entityIndex];
				}
				if (!scene->IsValid(handle)) continue;
				const ComponentInfo* info = componentRegistry.GetByTypeId(typeId);
				const auto commandAvailable = [&](const ComponentInfo* component) {
					if (!component || !component->IsModuleActive()) return false;
					if (opcode == Ecb_RemoveComponent) return static_cast<bool>(component->remove);
					if (opcode == Ecb_DefaultConstructComponent) return static_cast<bool>(component->defaultEmplace);
					return component->emplaceFromBytes
						&& (component->bytePayloadSize == 0 || componentSize == component->bytePayloadSize);
				};
				if (!commandAvailable(info)) continue;
				if (opcode == Ecb_RemoveComponent) {
					ScriptBindings::RemoveComponent(scene->GetEntity(handle), *info);
					continue;
				}
				if (targetsLiveEntity && opcode == Ecb_DefaultConstructComponent) {
					ScriptBindings::AddComponent(scene->GetEntity(handle), *info);
					continue;
				}
				bool present;
				if (info->isDynamic) {
					present = info->has && info->has(scene->GetEntity(handle));
					if (!scene->IsValid(handle)) continue;
					info = componentRegistry.GetByTypeId(typeId);
					if (!commandAvailable(info)) continue;
				}
				else {
					const auto* pool = std::as_const(scene->GetRegistry()).storage(info->storageHash);
					present = pool && pool->contains(handle);
				}
				if (opcode == Ecb_DefaultConstructComponent && present) continue;
				if (!present) {
					if (refusesConflictingAdd(*info, handle)) continue;
					if (!info->dependsOn.empty()) {
						componentRegistry.AddDependencies(scene->GetEntity(handle), info->typeId);
						if (!scene->IsValid(handle)) continue;
						info = componentRegistry.GetByTypeId(typeId);
						if (!commandAvailable(info)) continue;
						if (opcode == Ecb_DefaultConstructComponent && !info->isDynamic) {
							const auto* pool = std::as_const(scene->GetRegistry()).storage(info->storageHash);
							if (pool && pool->contains(handle)) continue;
						}
					}
				}
				const bool dynamic = info->isDynamic;
				[[maybe_unused]] const entt::id_type storageHash = info->storageHash;
				if (opcode == Ecb_DefaultConstructComponent) info->defaultEmplace(scene->GetRegistry(), handle);
				else info->emplaceFromBytes(scene->GetRegistry(), handle, componentBytes, componentSize);
				if (!scene->IsValid(handle)) continue;
				if (dynamic) {
					seedScriptsEntryForDynamic(typeId, handle);
					if (!scene->IsValid(handle)) continue;
				}
#if FUNKE_WITH_2D
				if (storageHash == entt::type_hash<Transform2DComponent>::value()) {
					if (auto* transform = scene->GetRegistry().try_get<Transform2DComponent>(handle)) {
						transform->BindOwner(scene, handle);
						if (targetsLiveEntity) liveTransformWrites.push_back(handle);
					}
				}
#endif
			}
			else if (opcode == Ecb_InstantiatePrefab) {
				if (nextPrefabSpawn >= prefabSpawns.size()) {
					FNK_CORE_ERROR_TAG("Ecb",
						"Ecb_InstantiatePrefab execute-pass iterator out of sync with pre-scan (cmd {})", cmdIdx);
					return kEcbErrorTruncated;
				}
				const PrefabSpawn& spawn = prefabSpawns[nextPrefabSpawn++];
				const PrefabTemplate& templ = *spawn.templ;
				const uint32_t childCount = templ.EntityCount();
				if (destroyedRoot[spawn.rootEntityIndex] != 0) {
					for (uint32_t k = 1; k < childCount; ++k) {
						EntityHandle child = handles[spawn.childBaseIndex + k - 1u];
						if (scene->IsValid(child)) {
							scene->DestroyEntity(child);
						}
					}
					continue;
				}

				prefabSlotScratch.resize(childCount);
				prefabSlotScratch[0] = handles[spawn.rootEntityIndex];
				for (uint32_t k = 1; k < childCount; ++k) {
					prefabSlotScratch[k] = handles[spawn.childBaseIndex + k - 1u];
				}

				prefabCache.HydrateInto(spawn.prefabGuid, spawn.templ, *scene,
					std::span<const EntityHandle>(prefabSlotScratch));
			}
			else if (opcode == Ecb_DestroyEntity) {
				if (entityIndex == kEcbNoName) {
					const uint64_t entityId = EcbReadLE<uint64_t>(payload);
					EntityHandle resolved = entt::null;
					if (scene->TryResolveEntityRef(entityId, resolved)) {
						scene->DestroyEntity(resolved);
					}
				}
				else if (destroyedRoot[entityIndex] == 0) {
					destroyedRoot[entityIndex] = 1;
					if (scene->IsValid(handles[entityIndex])) {
						scene->DestroyEntity(handles[entityIndex]);
					}
				}
			}
			else if (opcode == Ecb_SetEntityEnabled) {
				// payload[0] = enabled flag; live entities carry the u64 persistent id after it.
				const bool enabled = payload[0] != 0;
				if (entityIndex == kEcbNoName) {
					const uint64_t entityId = EcbReadLE<uint64_t>(payload + 1);
					EntityHandle resolved = entt::null;
					if (scene->TryResolveEntityRef(entityId, resolved) && scene->IsValid(resolved)) {
						scene->GetEntity(resolved).SetEnabled(enabled);
					}
				}
				else if (destroyedRoot[entityIndex] == 0 && scene->IsValid(handles[entityIndex])) {
					scene->GetEntity(handles[entityIndex]).SetEnabled(enabled);
				}
			}
		}

		timings.Mark(4);
#if FUNKE_WITH_2D
		// A Transform2D written by the ECB to place a spawned prefab (e.g.
		// ecb.AddComponent(root, new NativeTransform2D{Position})) is a raw byte copy. m_Dirty is set in
		// the bytes, but m_OwnerScene/m_OwnerEntity are zeroed, so MarkDirty can't enqueue the entity in
		// the scene dirty-transform set; and because the root already has a Transform2D from hydrate
		// this is an emplace_or_replace REPLACE (fires on_update, not on_construct), so no hook moves
		// the body the eager Rigidbody2D/collider construct hooks already created at the prefab origin.
		// Net: transform/sprite at spawnPos, body left at the origin. Re-stamp the owner + mark dirty
		// (ongoing sync + serialization), and teleport each root physics body straight to its transform
		// so the spawn position takes effect immediately.
		{
			entt::registry& reg = scene->GetRegistry();
			auto* transforms = const_cast<entt::registry::storage_for_type<Transform2DComponent>*>(std::as_const(reg).storage<Transform2DComponent>());
			const auto* hierarchies = std::as_const(reg).storage<HierarchyComponent>();
			auto* rigidbodies = const_cast<entt::registry::storage_for_type<Rigidbody2DComponent>*>(std::as_const(reg).storage<Rigidbody2DComponent>());
			for (size_t i = 0; i < handles.size() + liveTransformWrites.size(); ++i) {
				const EntityHandle h = i < handles.size() ? handles[i] : liveTransformWrites[i - handles.size()];
				if (!scene->IsValid(h)) {
					continue;
				}
				Transform2DComponent* tr = transforms && transforms->contains(h) ? &transforms->get(h) : nullptr;
				if (tr == nullptr) {
					continue;
				}
				tr->BindOwner(scene, h);
				tr->MarkDirty();
				// Only roots have world == local now (child world transforms are composed later by the
				// hierarchy pass), so only teleport their body straight to the transform.
				const HierarchyComponent* hc = hierarchies && hierarchies->contains(h) ? &hierarchies->get(h) : nullptr;
				const bool isRoot = (hc == nullptr) || hc->Parent == entt::null;
				if (Rigidbody2DComponent* rb = rigidbodies && rigidbodies->contains(h) ? &rigidbodies->get(h) : nullptr; rb != nullptr) {
					if (isRoot && rb->IsValid()) {
						rb->SetTransform(*tr);
					}
				}
			}
		}
#endif

		const auto* identities = std::as_const(scene->GetRegistry()).storage<UUIDComponent>();
		for (uint32_t i = 0; i < header.entityCount; ++i) {
			outRuntimeIds[i] = destroyedRoot[i] == 0 && identities && identities->contains(handles[i])
				? static_cast<uint64_t>(identities->get(handles[i]).Id)
				: 0;
		}

		// Pulse lives outside the guard scope so it isn't double-counted by a later MarkAllDirtyOnce().
		scene->MarkAllDirtyOnce();
		timings.Mark(5);
		return static_cast<int>(header.entityCount);
	}

	// Boundary guard: Ecb_Playback is reached via [UnmanagedCallersOnly]; an exception unwinding
	// into CoreCLR is UB / fatal. Playback allocates heavily (prefab bake, scratch vectors) and can
	// throw std::bad_alloc, so swallow here and surface as an error code the C# side already handles.
	static int Funke_Ecb_PlaybackWithMode(const uint8_t* buffer, int length,
		uint64_t* outRuntimeIds, int maxOut, int mode)
	{
		try {
			return Funke_Ecb_PlaybackImpl(buffer, length, outRuntimeIds, maxOut, mode);
		}
		catch (const std::exception& e) {
			FNK_CORE_ERROR_TAG("ScriptBinding", "exception in Funke_Ecb_Playback: {}", e.what());
			return kEcbErrorInternal;
		}
		catch (...) {
			FNK_CORE_ERROR_TAG("ScriptBinding", "unknown exception in Funke_Ecb_Playback");
			return kEcbErrorInternal;
		}
	}

	static int Funke_Ecb_Playback(const uint8_t* buffer, int length,
		uint64_t* outRuntimeIds, int maxOut)
	{
		return Funke_Ecb_PlaybackWithMode(buffer, length, outRuntimeIds, maxOut, 0);
	}

	int detail::PlaybackNativeEntityCommandBuffer(Scene& scene, const uint8_t* bytes, int length,
		uint64_t* identities, int capacity, EntityCommandBufferPlaybackMode mode) {
		SceneExecutionScope executionScope(scene);
		return Funke_Ecb_PlaybackWithMode(bytes, length, identities, capacity, static_cast<int>(mode));
	}

	void PopulateEcbBindings(NativeBindings& b)
	{
		b.Component_GetTypeId = FNK_GUARDED_BINDING(Funke_Component_GetTypeId);
		b.Ecb_Playback        = FNK_GUARDED_BINDING(Funke_Ecb_Playback);
		b.Ecb_PlaybackWithMode = FNK_GUARDED_BINDING(Funke_Ecb_PlaybackWithMode);
	}

} // namespace Funke
