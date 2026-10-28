#include "pch.hpp"
#include "Scripting/ScriptBindingGuard.hpp"
#include "Scripting/EntityCommandBufferWire.hpp"
#include "Scripting/ScriptGlue.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#endif
#include "Serialization/PrefabTemplateCache.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Core/Application.hpp"
#include "Core/Log.hpp"

#include <cstring>
#include <span>
#include <vector>

namespace Funke {

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

	// memcpy to dodge unaligned-load UB: ECB stream is byte-packed, fields land on odd boundaries; compiler folds to a single load on x86/x64.
	template<typename T>
	static T EcbReadLE(const uint8_t* src) {
		T value;
		std::memcpy(&value, src, sizeof(T));
		return value;
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

	// Reused scratch vector: avoids per-call alloc in steady-state spawn loops. ECB playback is main-thread only.
	static std::vector<EntityHandle>& EcbHandleScratch()
	{
		static std::vector<EntityHandle> scratch;
		return scratch;
	}

	static int EcbValidateCommands(const uint8_t* cursor, const uint8_t* bufferEnd, const EcbHeader& header)
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
			case Ecb_DefaultConstructComponent: {
				if (!localEntity) return kEcbErrorTruncated;
				const uint32_t typeId = EcbReadLE<uint32_t>(cursor + 5);
				const ComponentInfo* info = components.GetByTypeId(typeId);
				const bool useDefault = opcode == Ecb_DefaultConstructComponent;
				if (!info || (useDefault ? !info->defaultEmplace : !info->emplaceFromBytes)) {
					return kEcbErrorUnknownComponent;
				}
				if ((useDefault && payloadSize != 0)
					|| (!useDefault && info->bytePayloadSize != 0 && payloadSize != info->bytePayloadSize)) {
					FNK_CORE_WARN_TAG("Ecb", "Component payload size mismatch at command {} (typeId={}, size={})",
						command, typeId, payloadSize);
					return kEcbErrorTruncated;
				}
				break;
			}
			case Ecb_InstantiatePrefab:
				if (!localEntity || payloadSize != sizeof(uint64_t)) return kEcbErrorTruncated;
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
		uint64_t* outRuntimeIds, int maxOut)
	{
		if (buffer == nullptr || length < static_cast<int>(sizeof(EcbHeader))) {
			return kEcbErrorTruncated;
		}
		Scene* scene = EcbResolveScene();
		if (scene == nullptr) {
			return kEcbErrorNoScene;
		}

		EcbHeader header;
		std::memcpy(&header, buffer, sizeof(EcbHeader));

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
		if (const int error = EcbValidateCommands(commandStreamPtr, bufferEnd, header); error != 0) return error;

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

		{
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

		// Root slots [0, entityCount); prefab children [entityCount, totalEntityCount). Only roots are surfaced to managed code.
		scene->ReserveForLoadRuntime(totalEntityCount, {});

		std::vector<EntityHandle>& handles = EcbHandleScratch();
		handles.resize(totalEntityCount);
		scene->CreateEntitiesBulk(totalEntityCount, handles);

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
		for (uint32_t i = 0; i < header.entityCount; ++i) {
			if (isPrefabRoot[i] == 0) {
				scene->SetEntityMetaDataNoFlags(handles[i], EntityOrigin::Runtime);
			}
		}

		const ComponentRegistry& componentRegistry = SceneManager::GetComponentRegistry();

		std::vector<EntityHandle> prefabSlotScratch;
		std::size_t nextPrefabSpawn = 0;

		// Raw emplaces skip AddWithDependencies, so conflicts (e.g. a 3D component on a 2D entity) are checked here;
		// only first-time adds of components that have conflicts pay for it.
		bool warnedConflict = false;
		auto refusesConflictingAdd = [&](const ComponentInfo& info, EntityHandle handle) {
			if (info.conflictsWith.empty() || info.isDynamic || !info.has) return false;
			const Entity entity = scene->GetEntity(handle);
			if (info.has(entity)) return false;
			const ComponentInfo* conflict = componentRegistry.FindConflict(entity, info.typeId);
			if (!conflict) return false;
			if (!warnedConflict) {
				warnedConflict = true;
				FNK_CORE_WARN_TAG("Ecb", "AddComponent '{}' skipped: the entity has '{}', which it can't be combined with.",
					info.displayName, conflict->displayName);
			}
			return true;
		};

		// Dynamic IComponent adds must seed a ScriptComponent.Scripts entry so the inspector renders it; idempotent.
		auto seedScriptsEntryForDynamic = [&](const ComponentInfo* info, EntityHandle handle) {
			if (!info->isDynamic) return;
			Entity entity = scene->GetEntity(handle);
			if (!entity.HasComponent<ScriptComponent>()) {
				entity.AddComponent<ScriptComponent>();
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

			const bool targetsLiveEntity =
				(opcode == Ecb_DestroyEntity || opcode == Ecb_SetEntityEnabled)
				&& entityIndex == kEcbNoName;
			if (!targetsLiveEntity && entityIndex >= header.entityCount) {
				FNK_CORE_WARN_TAG("Ecb", "Skipping command with out-of-range entityIndex {}", entityIndex);
				continue;
			}

			if (opcode == Ecb_AddComponent || opcode == Ecb_SetComponent) {
				if (destroyedRoot[entityIndex] != 0) {
					continue;
				}
				const ComponentInfo* info = componentRegistry.GetByTypeId(typeId);
				if (info == nullptr || info->emplaceFromBytes == nullptr) {
					FNK_CORE_WARN_TAG("Ecb",
						"AddComponent for typeId {} cannot proceed: no registered "
						"component or component has no emplaceFromBytes callback "
						"(component holds non-memcpy-safe state and needs a "
						"custom emplacer — see ComponentRegistry contract).",
						typeId);
					return kEcbErrorUnknownComponent;
				}
				if (refusesConflictingAdd(*info, handles[entityIndex])) continue;
				info->emplaceFromBytes(scene->GetRegistry(), handles[entityIndex],
					payload, payloadSize);
				seedScriptsEntryForDynamic(info, handles[entityIndex]);
			}
			else if (opcode == Ecb_DefaultConstructComponent) {
				if (destroyedRoot[entityIndex] != 0) {
					continue;
				}
				const ComponentInfo* info = componentRegistry.GetByTypeId(typeId);
				if (info == nullptr || info->defaultEmplace == nullptr) {
					FNK_CORE_WARN_TAG("Ecb",
						"DefaultConstructComponent for typeId {} cannot proceed: no "
						"registered component or component is not default-constructible "
						"(use the value or patch overload of AddComponent instead).",
						typeId);
					return kEcbErrorUnknownComponent;
				}
				if (refusesConflictingAdd(*info, handles[entityIndex])) continue;
				info->defaultEmplace(scene->GetRegistry(), handles[entityIndex]);
				seedScriptsEntryForDynamic(info, handles[entityIndex]);
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
				// Unknown opcodes: skip (forwards-compat). We already advanced
			// `cursor` past the record's payload, so the stream stays
			// aligned for the next iteration.
		}

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
			for (uint32_t i = 0; i < totalEntityCount; ++i) {
				const EntityHandle h = handles[i];
				if (!scene->IsValid(h)) {
					continue;
				}
				Transform2DComponent* tr = reg.try_get<Transform2DComponent>(h);
				if (tr == nullptr) {
					continue;
				}
				tr->BindOwner(scene, h);
				tr->MarkDirty();
				// Only roots have world == local now (child world transforms are composed later by the
				// hierarchy pass), so only teleport their body straight to the transform.
				const HierarchyComponent* hc = reg.try_get<HierarchyComponent>(h);
				const bool isRoot = (hc == nullptr) || hc->Parent == entt::null;
				if (Rigidbody2DComponent* rb = reg.try_get<Rigidbody2DComponent>(h); rb != nullptr) {
					if (isRoot && rb->IsValid()) {
						rb->SetTransform(*tr);
					}
				}
			}
		}
#endif

		for (uint32_t i = 0; i < header.entityCount; ++i) {
			outRuntimeIds[i] = destroyedRoot[i] != 0
				? 0
				: scene->GetEntityPersistentID(handles[i]);
		}

		// Pulse lives outside the guard scope so it isn't double-counted by a later MarkAllDirtyOnce().
		scene->MarkAllDirtyOnce();
		return static_cast<int>(header.entityCount);
	}

	// Boundary guard: Ecb_Playback is reached via [UnmanagedCallersOnly]; an exception unwinding
	// into CoreCLR is UB / fatal. Playback allocates heavily (prefab bake, scratch vectors) and can
	// throw std::bad_alloc, so swallow here and surface as an error code the C# side already handles.
	static int Funke_Ecb_Playback(const uint8_t* buffer, int length,
		uint64_t* outRuntimeIds, int maxOut)
	{
		try {
			return Funke_Ecb_PlaybackImpl(buffer, length, outRuntimeIds, maxOut);
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

	void PopulateEcbBindings(NativeBindings& b)
	{
		b.Component_GetTypeId = FNK_GUARDED_BINDING(Funke_Component_GetTypeId);
		b.Ecb_Playback        = FNK_GUARDED_BINDING(Funke_Ecb_Playback);
	}

} // namespace Funke
