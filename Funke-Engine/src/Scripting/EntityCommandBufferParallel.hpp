#pragma once

#include "Scripting/EntityCommandBufferWire.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/SceneManager.hpp"
#include "Scene/Scene.hpp"
#include "Components/General/EntityMetaDataComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/General/HierarchyComponent.hpp"
#if FUNKE_WITH_2D
#include "Components/General/Transform2DComponent.hpp"
#include "Components/Graphics/SpriteRendererComponent.hpp"
#include "Components/Physics/Rigidbody2DComponent.hpp"
#endif
#include "Core/Log.hpp"
#include "Jobs/JobSystem.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace Funke {

	inline bool EcbEnvironmentFlag(const char* name) {
#ifdef FNK_PLATFORM_WINDOWS
		char* value = nullptr;
		size_t length = 0;
		_dupenv_s(&value, &length, name);
		const bool enabled = value && value[0] == '1';
		std::free(value);
		return enabled;
#else
		const char* value = std::getenv(name);
		return value && value[0] == '1';
#endif
	}

	class EcbExecutor {
	public:
		explicit EcbExecutor(bool parallel) : m_Parallel(parallel) {
			static const bool diagnostics = EcbEnvironmentFlag("FUNKE_ECB_PARALLEL_DIAGNOSTICS");
			static const bool serialKernels = EcbEnvironmentFlag("FUNKE_ECB_SERIAL_KERNELS");
			m_Diagnostics = diagnostics;
			m_Parallel &= !serialKernels;
		}

		template<typename Kernel>
		void operator()(size_t count, Kernel&& kernel) noexcept {
			Run(count, std::forward<Kernel>(kernel), 2048);
		}

		template<typename Kernel>
		void RunTasks(size_t count, Kernel&& kernel) noexcept {
			Run(count, std::forward<Kernel>(kernel), 1);
		}

		template<typename Kernel>
		void Run(size_t count, Kernel&& kernel, size_t grain) noexcept {
			static_assert(std::is_nothrow_invocable_v<Kernel&, size_t>);
			const size_t workers = m_Parallel && JobSystem::IsInitialized() && !JobSystem::IsCallerWorker()
				? std::min(static_cast<size_t>(JobSystem::GetWorkerCount()), count / grain) : 0;
			if (workers == 0) {
				for (size_t index = 0; index < count; ++index) kernel(index);
				return;
			}
			std::atomic<size_t> next{ 0 };
			std::shared_ptr<std::atomic<size_t>> pending;
			try { pending = std::make_shared<std::atomic<size_t>>(0); }
			catch (...) {
				for (size_t index = 0; index < count; ++index) kernel(index);
				return;
			}
			auto run = [&]() noexcept {
				const int worker = JobSystem::GetWorkerIndex();
				for (;;) {
					const size_t begin = next.fetch_add(grain, std::memory_order_relaxed);
					if (begin >= count) break;
					const size_t end = std::min(count, begin + grain);
					if (m_Diagnostics) {
						const size_t active = m_Active.fetch_add(1, std::memory_order_relaxed) + 1;
						size_t maximum = m_MaxActive.load(std::memory_order_relaxed);
						while (maximum < active && !m_MaxActive.compare_exchange_weak(maximum, active, std::memory_order_relaxed)) {}
						if (worker >= 0) {
							m_WorkerItems.fetch_add(end - begin, std::memory_order_relaxed);
							if (grain == 1) m_WorkerPoolTasks.fetch_add(end - begin, std::memory_order_relaxed);
							m_WorkerMask.fetch_or(uint64_t{ 1 } << worker, std::memory_order_relaxed);
						}
					}
					for (size_t index = begin; index < end; ++index) kernel(index);
					if (m_Diagnostics) m_Active.fetch_sub(1, std::memory_order_relaxed);
				}
			};
			for (size_t worker = 0; worker < workers; ++worker) {
				pending->fetch_add(1, std::memory_order_relaxed);
				try {
					JobSystem::Enqueue([&, pending]() noexcept {
						run();
						pending->fetch_sub(1, std::memory_order_release);
						pending->notify_all();
					});
				}
				catch (...) {
					pending->fetch_sub(1, std::memory_order_release);
					break;
				}
			}
			run();
			// Waiting must not steal unrelated jobs while component objects are being constructed.
			for (size_t remaining = pending->load(std::memory_order_acquire); remaining != 0;
				remaining = pending->load(std::memory_order_acquire)) pending->wait(remaining, std::memory_order_acquire);
		}

		void Report(size_t entities, size_t commands) const {
			if (m_Diagnostics) FNK_CORE_INFO_TAG("EcbParallel", "entities={} commands={} worker_items={} worker_pool_tasks={} worker_mask={} max_active={}",
				entities, commands, m_WorkerItems.load(), m_WorkerPoolTasks.load(), m_WorkerMask.load(), m_MaxActive.load());
		}

	private:
		bool m_Parallel;
		bool m_Diagnostics;
		std::atomic<size_t> m_WorkerItems{ 0 };
		std::atomic<size_t> m_WorkerPoolTasks{ 0 };
		std::atomic<uint64_t> m_WorkerMask{ 0 };
		std::atomic<size_t> m_Active{ 0 };
		std::atomic<size_t> m_MaxActive{ 0 };
	};

	struct EcbComponentBatch {
		const ComponentInfo* Info = nullptr;
		std::vector<EntityHandle> Handles;
		std::vector<const uint8_t*> Payloads;
	};

	struct EcbSerialExecutor {
		template<typename Kernel>
		void operator()(size_t count, Kernel&& kernel) const noexcept {
			for (size_t index = 0; index < count; ++index) kernel(index);
		}
	};

	template<typename T>
	bool EcbCertifiedComponent(const ComponentInfo& info, uint8_t opcode) {
		if (info.typeId != typeid(T) || info.storageHash != entt::type_hash<T>::value()
			|| info.isDynamic || info.onAdd || !info.dependsOn.empty()
			|| !info.has.template IsNative<T>()) return false;
		if (opcode == Ecb_RemoveComponent) return info.remove.template IsNative<T>();
		if (opcode == Ecb_DefaultConstructComponent) {
			return info.defaultEmplace.template IsNative<T>() && info.add.template IsNative<T>();
		}
		return info.emplaceFromBytes.template IsNative<T>(info.bytePayloadSize)
			&& info.bytePayloadSize != 0 && info.bytePayloadSize <= sizeof(T);
	}

	template<typename T, typename Executor>
	void EcbConstructBatch(Scene& scene, EcbComponentBatch& batch,
		entt::registry::storage_for_type<T>& storage, Executor& execute) {
		if (batch.Handles.empty()) return;
		storage.bulk_insert(batch.Handles.begin(), batch.Handles.end(), execute,
			[&](T* destination, size_t index) noexcept {
				std::construct_at(destination);
				if (const auto* payload = batch.Payloads[index]) std::memcpy(destination, payload, batch.Info->bytePayloadSize);
#if FUNKE_WITH_2D
				if constexpr (std::is_same_v<T, Transform2DComponent>) destination->BindOwner(&scene, batch.Handles[index]);
#endif
			});
	}

	template<typename T>
	void EcbNotifyConstructed(Scene& scene, const EcbComponentBatch& batch) {
		if (batch.Handles.empty()) return;
		scene.NotifyParallelEcbComponentsConstructed(batch.Info->storageHash, batch.Handles);
	}

	template<typename T>
	void EcbInsertBatch(Scene& scene, EcbComponentBatch& batch, EcbExecutor& execute) {
		if (batch.Handles.empty()) return;
		EcbConstructBatch<T>(scene, batch, scene.GetRegistry().storage<T>(), execute);
		EcbNotifyConstructed<T>(scene, batch);
	}

	template<typename T>
	void EcbRemoveBatch(Scene& scene, EcbComponentBatch& batch, EcbExecutor& execute, bool notify) {
		if (batch.Handles.empty()) return;
		auto& storage = scene.GetRegistry().storage<T>();
		auto plan = storage.prepare_bulk_remove(batch.Handles.begin(), batch.Handles.end());
		if (notify) {
			std::vector<uint64_t> identities;
			identities.reserve(batch.Handles.size());
			for (const EntityHandle handle : batch.Handles) {
				identities.push_back(scene.GetEntityPersistentID(handle));
			}
			scene.NotifyParallelEcbComponentsDestroying(batch.Info->storageHash, batch.Handles);
			ScriptEngine::NotifyComponentsRemoved(identities.data(), static_cast<int>(identities.size()), batch.Info->typeIdU32);
		}
		storage.bulk_remove(plan, execute);
	}

	inline bool EcbTryParallelPlayback(Scene& scene, const EcbHeader& header, const uint8_t* cursor,
		uint64_t* output, int mode) {
		constexpr size_t k_AutomaticMinimum = 512;
		if (mode == 1 || JobSystem::IsCallerWorker()
			|| (mode == 0 && std::max(header.entityCount, header.commandCount) < k_AutomaticMinimum)) return false;
		const bool create = header.entityCount != 0;
		if (create && (!scene.CanUseParallelEcbComponent(entt::type_hash<EntityHandle>::value())
			|| !scene.CanUseParallelEcbComponent(entt::type_hash<EntityMetaDataComponent>::value())
			|| !scene.CanUseParallelEcbComponent(entt::type_hash<UUIDComponent>::value()))) return false;

		enum class Action { None, Add, Remove, Destroy } action = Action::None;
		struct ComponentPlan {
			const entt::registry::common_type* Storage = nullptr;
			std::vector<const entt::registry::common_type*> Conflicts;
			uint8_t CertifiedOpcodes = 0;
		};
		struct PlaybackScratch {
			EcbComponentBatch Transforms, Sprites;
			ComponentPlan TransformPlan, SpritePlan;
			std::vector<EntityHandle> Destroyed;
			std::vector<uint32_t> Seen;
			uint32_t Stamp = 0;
			bool InUse = false;
		};
		static thread_local PlaybackScratch t_Scratch;
		PlaybackScratch localScratch;
		auto& scratch = t_Scratch.InUse ? localScratch : t_Scratch;
		struct ScratchScope {
			PlaybackScratch& Scratch;
			~ScratchScope() { Scratch.InUse = false; }
		} scratchScope{ scratch };
		scratch.InUse = true;
		auto& transforms = scratch.Transforms;
		auto& sprites = scratch.Sprites;
		auto& destroyed = scratch.Destroyed;
		for (auto* batch : { &transforms, &sprites }) {
			batch->Info = nullptr;
			batch->Handles.clear();
			batch->Payloads.clear();
		}
		for (auto* plan : { &scratch.TransformPlan, &scratch.SpritePlan }) {
			plan->Storage = nullptr;
			plan->Conflicts.clear();
			plan->CertifiedOpcodes = 0;
		}
		destroyed.clear();
		const size_t capacity = create ? header.entityCount : scene.GetRegistry().storage<EntityHandle>().size();
		auto& seen = scratch.Seen;
		if (seen.size() < capacity) seen.resize(capacity, 0);
		scratch.Stamp += 8;
		if (scratch.Stamp == 0) {
			std::fill(seen.begin(), seen.end(), 0);
			scratch.Stamp = 8;
		}
		const size_t maximumBatch = std::min(capacity, static_cast<size_t>(header.commandCount));
		const auto& components = SceneManager::GetComponentRegistry();
		const auto& sourceRegistry = std::as_const(scene.GetRegistry());
#if FUNKE_WITH_2D
		const auto* hierarchy = sourceRegistry.storage<HierarchyComponent>();
		const auto* rigidbodies = sourceRegistry.storage<Rigidbody2DComponent>();
#endif
		for (uint32_t command = 0; command < header.commandCount; ++command) {
			const uint8_t opcode = cursor[0];
			const uint32_t entityIndex = EcbReadLE<uint32_t>(cursor + 1);
			const uint32_t typeId = EcbReadLE<uint32_t>(cursor + 5);
			const uint16_t payloadSize = EcbReadLE<uint16_t>(cursor + 9);
			const uint8_t* payload = cursor + 11;
			cursor = payload + payloadSize;
			const Action next = opcode == Ecb_AddComponent || opcode == Ecb_DefaultConstructComponent ? Action::Add
				: opcode == Ecb_RemoveComponent ? Action::Remove : opcode == Ecb_DestroyEntity ? Action::Destroy : Action::None;
			if (next == Action::None || (action != Action::None && action != next)
				|| (create && next != Action::Add) || (create == (entityIndex == kEcbNoName))) return false;
			action = next;
			EntityHandle handle = static_cast<EntityHandle>(entityIndex);
			if (!create) {
				if (!scene.TryResolveEntityRef(EcbReadLE<uint64_t>(payload), handle)) return false;
				payload += sizeof(uint64_t);
			}
			const size_t slot = create ? entityIndex : entt::to_entity(handle);
			if (slot >= capacity) return false;
			uint32_t& mark = seen[slot];
			if ((mark & ~uint32_t{ 7 }) != scratch.Stamp) mark = scratch.Stamp;
			if (action == Action::Destroy) {
				if (mark & 4) return false;
				mark |= 4;
				if (destroyed.empty()) destroyed.reserve(maximumBatch);
				destroyed.push_back(handle);
				continue;
			}
			EcbComponentBatch* batch = nullptr;
			ComponentPlan* plan = nullptr;
			uint8_t bit = 0;
			if (transforms.Info && transforms.Info->typeIdU32 == typeId) { batch = &transforms; plan = &scratch.TransformPlan; bit = 1; }
			else if (sprites.Info && sprites.Info->typeIdU32 == typeId) { batch = &sprites; plan = &scratch.SpritePlan; bit = 2; }
			const ComponentInfo* info = batch ? batch->Info : components.GetByTypeId(typeId);
			if (!info) return false;
			const uint8_t opcodeBit = static_cast<uint8_t>(1u << opcode);
			if (!plan || !(plan->CertifiedOpcodes & opcodeBit)) {
#if FUNKE_WITH_2D
				if (EcbCertifiedComponent<Transform2DComponent>(*info, opcode)) { batch = &transforms; plan = &scratch.TransformPlan; bit = 1; }
				else if (EcbCertifiedComponent<SpriteRendererComponent>(*info, opcode)) { batch = &sprites; plan = &scratch.SpritePlan; bit = 2; }
				else return false;
#else
				return false;
#endif
				plan->CertifiedOpcodes |= opcodeBit;
			}
			if (mark & bit) return false;
			mark |= bit;
			if (!batch->Info) {
				if (!scene.CanUseParallelEcbComponent(info->storageHash)) return false;
				batch->Info = info;
				batch->Handles.reserve(maximumBatch);
				if (action == Action::Add) batch->Payloads.reserve(maximumBatch);
				if (!create) {
					plan->Storage = sourceRegistry.storage(info->storageHash);
					if (action == Action::Add) {
						for (const auto& conflict : info->conflictsWith) {
							const auto found = components.All().find(conflict);
							if (found == components.All().end() || !found->second.has) continue;
							if (const auto* storage = sourceRegistry.storage(found->second.storageHash)) plan->Conflicts.push_back(storage);
						}
					}
				}
			}
			if (!create) {
				const bool present = plan->Storage && plan->Storage->contains(handle);
				if (present != (action == Action::Remove)) return false;
				for (const auto* conflict : plan->Conflicts) if (conflict->contains(handle)) return false;
#if FUNKE_WITH_2D
				if (action == Action::Add && bit == 1
					&& ((hierarchy && hierarchy->contains(handle)) || (rigidbodies && rigidbodies->contains(handle)))) return false;
#endif
			}
			batch->Handles.push_back(handle);
			if (action == Action::Add) batch->Payloads.push_back(opcode == Ecb_DefaultConstructComponent ? nullptr : payload);
		}
		if (action == Action::Remove && !ScriptEngine::CanBatchComponentInvalidation()) return false;
		if (action == Action::Destroy && !scene.CanUseParallelEcbDestroy(destroyed)) return false;
#if FUNKE_WITH_2D
		if (action == Action::Add && transforms.Info && sprites.Info
			&& (std::find(transforms.Info->conflictsWith.begin(), transforms.Info->conflictsWith.end(), sprites.Info->typeId) != transforms.Info->conflictsWith.end()
				|| std::find(sprites.Info->conflictsWith.begin(), sprites.Info->conflictsWith.end(), transforms.Info->typeId) != sprites.Info->conflictsWith.end())) return false;
#endif
		EcbExecutor execute(mode == 2 || std::max(header.entityCount, header.commandCount) >= 8192);
		std::vector<EntityHandle> handles;
		struct CreatedRollback {
			Scene& Owner;
			std::vector<EntityHandle>& Handles;
			bool Active = false;
			~CreatedRollback() { if (Active) Owner.RollbackParallelEcbCreatedEntities(Handles); }
		} rollback{ scene, handles };
		if (create) {
			std::vector<uint64_t> identities(header.entityCount);
			std::exception_ptr generationError;
			std::mutex errorMutex;
			execute(identities.size(), [&](size_t index) noexcept {
				try { identities[index] = static_cast<uint64_t>(UUID()); }
				catch (...) { std::lock_guard lock(errorMutex); if (!generationError) generationError = std::current_exception(); }
			});
			if (generationError) std::rethrow_exception(generationError);
			handles.resize(header.entityCount);
			scene.ReserveForLoadRuntime(handles.size(), {});
			scene.CreateEntitiesBulk(handles.size(), handles);
			rollback.Active = true;
			for (auto* batch : { &transforms, &sprites }) for (EntityHandle& handle : batch->Handles) handle = handles[entt::to_integral(handle)];
			auto& metadataPool = scene.GetRegistry().storage<EntityMetaDataComponent>();
			auto& identityPool = scene.GetRegistry().storage<UUIDComponent>();
#if FUNKE_WITH_2D
			auto& transformPool = scene.GetRegistry().storage<Transform2DComponent>();
			auto& spritePool = scene.GetRegistry().storage<SpriteRendererComponent>();
#endif
			EcbSerialExecutor serial;
			execute.RunTasks(4, [&](size_t task) noexcept {
				try {
					if (task == 0) metadataPool.bulk_insert(handles.begin(), handles.end(), serial,
						[&](EntityMetaDataComponent* destination, size_t index) noexcept {
							std::construct_at(destination, EntityMetaDataComponent{ EntityMetaData{ identities[index], AssetGUID(0), AssetGUID(0), EntityOrigin::Runtime } });
						});
					else if (task == 1) identityPool.bulk_insert(handles.begin(), handles.end(), serial,
						[&](UUIDComponent* destination, size_t index) noexcept { std::construct_at(destination, identities[index]); });
#if FUNKE_WITH_2D
					else if (task == 2) EcbConstructBatch<Transform2DComponent>(scene, transforms, transformPool, serial);
					else if (task == 3) EcbConstructBatch<SpriteRendererComponent>(scene, sprites, spritePool, serial);
#endif
				}
				catch (...) { std::lock_guard lock(errorMutex); if (!generationError) generationError = std::current_exception(); }
			});
			if (generationError) std::rethrow_exception(generationError);
			scene.CommitRuntimeEntityIdentities(handles, identities);
#if FUNKE_WITH_2D
			EcbNotifyConstructed<Transform2DComponent>(scene, transforms);
			EcbNotifyConstructed<SpriteRendererComponent>(scene, sprites);
#endif
			std::copy(identities.begin(), identities.end(), output);
		}
#if FUNKE_WITH_2D
		if (action == Action::Add && !create) {
			EcbInsertBatch<Transform2DComponent>(scene, transforms, execute);
			EcbInsertBatch<SpriteRendererComponent>(scene, sprites, execute);
		}
		else if (action == Action::Remove) {
			EcbRemoveBatch<Transform2DComponent>(scene, transforms, execute, true);
			EcbRemoveBatch<SpriteRendererComponent>(scene, sprites, execute, true);
		}
#endif
		if (action == Action::Destroy) {
			auto& registry = scene.GetRegistry();
			EcbComponentBatch metadata, ids;
			for (const EntityHandle handle : destroyed) {
#if FUNKE_WITH_2D
				if (registry.all_of<Transform2DComponent>(handle)) transforms.Handles.push_back(handle);
				if (registry.all_of<SpriteRendererComponent>(handle)) sprites.Handles.push_back(handle);
#endif
				if (registry.all_of<EntityMetaDataComponent>(handle)) metadata.Handles.push_back(handle);
				if (registry.all_of<UUIDComponent>(handle)) ids.Handles.push_back(handle);
			}
#if FUNKE_WITH_2D
			auto& transformPool = registry.storage<Transform2DComponent>();
			auto& spritePool = registry.storage<SpriteRendererComponent>();
			auto transformPlan = transformPool.prepare_bulk_remove(transforms.Handles.begin(), transforms.Handles.end());
			auto spritePlan = spritePool.prepare_bulk_remove(sprites.Handles.begin(), sprites.Handles.end());
#endif
			auto& metadataPool = registry.storage<EntityMetaDataComponent>();
			auto& idPool = registry.storage<UUIDComponent>();
			auto metadataPlan = metadataPool.prepare_bulk_remove(metadata.Handles.begin(), metadata.Handles.end());
			auto idPlan = idPool.prepare_bulk_remove(ids.Handles.begin(), ids.Handles.end());
			scene.PrepareParallelEcbDestroy(destroyed);
			EcbSerialExecutor serial;
			execute.RunTasks(4, [&](size_t task) noexcept {
#if FUNKE_WITH_2D
				if (task == 0) transformPool.bulk_remove(transformPlan, serial);
				else if (task == 1) spritePool.bulk_remove(spritePlan, serial);
#endif
				if (task == 2) metadataPool.bulk_remove(metadataPlan, serial);
				else if (task == 3) idPool.bulk_remove(idPlan, serial);
			});
			for (const EntityHandle handle : destroyed) scene.FinalizeParallelEcbDestroy(handle);
		}
		scene.MarkAllDirtyOnce();
		execute.Report(header.entityCount, header.commandCount);
		rollback.Active = false;
		return true;
	}

}
