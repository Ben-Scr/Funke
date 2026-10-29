#include "pch.hpp"
#include "Scripting/NativeEntityCommandBuffer.hpp"

#include "Core/Application.hpp"
#include "Jobs/JobSystem.hpp"
#include "Scene/SceneExecutionContext.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/EntityCommandBufferWire.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <atomic>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Funke {
	namespace {
		std::atomic<uint64_t> g_NativeEcbToken{1};
		uint64_t NextToken() { return g_NativeEcbToken.fetch_add(1, std::memory_order_relaxed); }
	}

	NativeEntityCommandBuffer::NativeEntityCommandBuffer() : m_BufferToken(NextToken()) {}

	NativeEntityCommandBuffer::NativeEntityCommandBuffer(NativeEntityCommandBuffer&& other)
		: NativeEntityCommandBuffer() { *this = std::move(other); }

	NativeEntityCommandBuffer& NativeEntityCommandBuffer::operator=(NativeEntityCommandBuffer&& other) noexcept {
		if (this != &other) {
			Clear();
			m_Commands.swap(other.m_Commands);
			m_CreatedHandles = std::move(other.m_CreatedHandles);
			m_Wire = std::move(other.m_Wire);
			m_Identities = std::move(other.m_Identities);
			m_LiveSceneAnchor = std::exchange(other.m_LiveSceneAnchor, Entity::Null);
			m_HasLiveScene = std::exchange(other.m_HasLiveScene, false);
			m_CommandCount = std::exchange(other.m_CommandCount, 0);
			m_EntityCount = std::exchange(other.m_EntityCount, 0);
			m_BufferToken = std::exchange(other.m_BufferToken, NextToken());
			m_Generation = std::exchange(other.m_Generation, 0);
		}
		return *this;
	}

	NativeEntityCommandBuffer::~NativeEntityCommandBuffer() { Clear(); }

	NativeEntityCommandBuffer::EntityRef NativeEntityCommandBuffer::CreateEntity() {
		if (m_Playing) throw std::logic_error("Cannot record into a playing entity command buffer");
		if (m_EntityCount == UINT32_MAX) throw std::length_error("Native ECB entity limit exceeded");
		EntityRef result;
		result.Index = m_EntityCount++;
		result.BufferToken = m_BufferToken;
		result.Generation = m_Generation;
		return result;
	}

	NativeEntityCommandBuffer::EntityRef NativeEntityCommandBuffer::Existing(Entity entity) {
		if (!entity.IsValid()) throw std::invalid_argument("Native ECB target must be a live scene entity");
		EntityRef result;
		result.Handle = entity.GetHandle();
		result.LiveEntity = entity;
		return result;
	}

	NativeEntityCommandBuffer::EntityRef NativeEntityCommandBuffer::Instantiate(uint64_t prefabGuid) {
		EntityRef result = CreateEntity();
		auto& command = AppendCommand(result, [](void* state, Scene& scene, EntityHandle& handle) {
			const auto guid = *std::launder(reinterpret_cast<uint64_t*>(state));
			if (scene.IsValid(handle)) scene.DestroyEntity(handle);
			handle = SceneSerializer::InstantiatePrefab(scene, guid);
		}, Operation::Instantiate);
		::new (command.Storage) uint64_t(prefabGuid);
		return result;
	}

	NativeEntityCommandBuffer::EntityRef NativeEntityCommandBuffer::Instantiate(Entity prefabAsset) {
		if (!prefabAsset.IsValid()) return {};
		const auto guid = static_cast<uint64_t>(prefabAsset.GetPrefabGUID());
		return guid ? Instantiate(guid) : EntityRef{};
	}

	NativeEntityCommandBuffer::Command& NativeEntityCommandBuffer::AppendCommand(const EntityRef& entity,
		void (*apply)(void*, Scene&, EntityHandle&), Operation action, entt::id_type typeHash) {
		if (m_Playing) throw std::logic_error("Cannot record into a playing entity command buffer");
		const bool live = entity.Index == UINT32_MAX;
		if (live) {
			const Scene* scene = entity.LiveEntity.GetScene();
			if (!scene || entity.Handle == entt::null) throw std::invalid_argument("Native ECB target scene expired");
			if (m_HasLiveScene && m_LiveSceneAnchor.GetScene() != scene)
				throw std::invalid_argument("Native ECB cannot mix entities from different scenes");
			if (!m_HasLiveScene) { m_LiveSceneAnchor = entity.LiveEntity; m_HasLiveScene = true; }
		}
		else if (entity.BufferToken != m_BufferToken || entity.Generation != m_Generation || entity.Index >= m_EntityCount) {
			throw std::invalid_argument("Native ECB local entity reference belongs to another recording");
		}
		if (m_CommandCount == m_Commands.size()) m_Commands.emplace_back();
		auto& command = m_Commands[m_CommandCount++];
		command.Target = live ? static_cast<uint64_t>(entt::to_integral(entity.Handle)) : entity.Index;
		command.Apply = apply;
		command.DestroyCapture = nullptr;
		command.TypeHash = typeHash;
		command.Action = action;
		command.Live = live;
		return command;
	}

	void NativeEntityCommandBuffer::Destroy(const EntityRef& entity) {
		AppendCommand(entity, [](void*, Scene& scene, EntityHandle& handle) {
			scene.DestroyEntity(handle);
			handle = entt::null;
		}, Operation::Destroy);
	}

	void NativeEntityCommandBuffer::SetEnabled(const EntityRef& entity, bool enabled) {
		auto& command = AppendCommand(entity, [](void* state, Scene& scene, EntityHandle& handle) {
			scene.GetEntity(handle).SetEnabled(*std::launder(reinterpret_cast<bool*>(state)));
		}, Operation::SetEnabled);
		::new (command.Storage) bool(enabled);
	}

	void NativeEntityCommandBuffer::ValidateScene(Scene& scene) const {
		if (m_Playing) throw std::logic_error("Entity command buffer playback is already active");
		if (m_HasLiveScene && m_LiveSceneAnchor.GetScene() != &scene)
			throw std::invalid_argument("Native ECB playback scene differs from its live targets");
	}

	int NativeEntityCommandBuffer::Playback(Scene& scene, EntityCommandBufferPlaybackMode mode) {
		NativeEntityCommandBuffer* self = this;
		return PlaybackBuffers(scene, {&self, 1}, m_CreatedHandles, m_Wire, m_Identities, mode);
	}

	int NativeEntityCommandBuffer::PlaybackBuffers(Scene& scene, std::span<NativeEntityCommandBuffer*> buffers,
		std::vector<EntityHandle>& handles, std::vector<uint8_t>& wire, std::vector<uint64_t>& identities,
		EntityCommandBufferPlaybackMode mode) {
		if (!Application::IsMainThread() || JobSystem::IsCallerWorker())
			throw std::logic_error("Native ECB playback must run on the scene/main thread after recording completes");
		if (mode < EntityCommandBufferPlaybackMode::Auto || mode > EntityCommandBufferPlaybackMode::Parallel)
			return kEcbErrorInvalidMode;
		uint64_t total = 0;
		uint64_t commandCount = 0;
		for (const auto* buffer : buffers) {
			buffer->ValidateScene(scene);
			total += buffer->m_EntityCount;
			commandCount += buffer->m_CommandCount;
		}
		if (total > INT_MAX || commandCount > UINT32_MAX) return kEcbErrorEntityCapExceeded;
		struct PlayingGuard {
			std::span<NativeEntityCommandBuffer*> Buffers;
			explicit PlayingGuard(std::span<NativeEntityCommandBuffer*> buffers) : Buffers(buffers) {
				for (auto* buffer : Buffers) buffer->m_Playing = true;
			}
			~PlayingGuard() { for (auto* buffer : Buffers) buffer->m_Playing = false; }
		} playing(buffers);
		SceneExecutionScope executionScope(scene);
		handles.clear();
		if (total == 0 && commandCount == 0) return 0;

		const Application* app = Application::GetInstance();
		const ComponentRegistry* components = app && app->GetSceneManager() ? &SceneManager::GetComponentRegistry() : nullptr;
		std::vector<std::pair<entt::id_type, uint32_t>> typeIds;
		auto resolveType = [&](entt::id_type hash) {
			if (!components) return uint32_t{0};
			for (const auto& entry : typeIds) if (entry.first == hash) return entry.second;
			uint32_t result = 0;
			components->ForEachComponentInfo([&](const auto&, const ComponentInfo& info) {
				if (info.storageHash == hash && !info.isDynamic) result = info.typeIdU32;
			});
			typeIds.emplace_back(hash, result);
			return result;
		};
		bool canUseWire = components != nullptr;
		for (const auto* buffer : buffers) {
			if (!canUseWire) break;
			for (size_t index = 0; index < buffer->m_CommandCount; ++index) {
				const auto& command = buffer->m_Commands[index];
				if (command.Action == Operation::Custom) { canUseWire = false; break; }
				if (command.Action == Operation::DefaultAdd || command.Action == Operation::Remove) {
					const auto* info = components->GetByTypeId(resolveType(command.TypeHash));
					if (!info || (command.Action == Operation::DefaultAdd ? !info->defaultEmplace : !info->remove)) {
						canUseWire = false; break;
					}
				}
			}
			if (!canUseWire) break;
		}
		if (canUseWire) {
			const uint64_t maximumBytes = sizeof(EcbHeader) + total * sizeof(uint32_t) + commandCount * 20;
			if (maximumBytes > INT_MAX) return kEcbErrorTruncated;
			wire.resize(static_cast<size_t>(maximumBytes));
			uint8_t* cursor = wire.data();
			auto write = [&](auto value) { std::memcpy(cursor, &value, sizeof(value)); cursor += sizeof(value); };
			write(static_cast<uint32_t>(total));
			write(uint32_t{0});
			for (uint32_t index = 0; index < total; ++index) write(kEcbNoName);
			uint32_t base = 0;
			uint32_t emitted = 0;
			for (const auto* buffer : buffers) {
				for (size_t index = 0; index < buffer->m_CommandCount; ++index) {
					const auto& command = buffer->m_Commands[index];
					uint64_t identity = 0;
					if (command.Live) {
						const auto handle = static_cast<EntityHandle>(command.Target);
						if (!scene.IsValid(handle)) continue;
						identity = scene.GetEntityPersistentID(handle);
						if (identity == 0) continue;
					}
					const auto opcode = command.Action == Operation::DefaultAdd ? Ecb_DefaultConstructComponent
						: command.Action == Operation::Remove ? Ecb_RemoveComponent
						: command.Action == Operation::Destroy ? Ecb_DestroyEntity
						: command.Action == Operation::SetEnabled ? Ecb_SetEntityEnabled : Ecb_InstantiatePrefab;
					const auto type = command.Action == Operation::DefaultAdd || command.Action == Operation::Remove
						? resolveType(command.TypeHash) : 0;
					uint16_t payload = command.Live ? sizeof(uint64_t) : 0;
					if (command.Action == Operation::SetEnabled) ++payload;
					if (command.Action == Operation::Instantiate) payload = sizeof(uint64_t);
					write(static_cast<uint8_t>(opcode));
					write(command.Live ? kEcbNoName : base + static_cast<uint32_t>(command.Target));
					write(type);
					write(payload);
					if (command.Action == Operation::SetEnabled) write(static_cast<uint8_t>(*std::launder(reinterpret_cast<const bool*>(command.Storage))));
					if (command.Live) write(identity);
					if (command.Action == Operation::Instantiate) write(*std::launder(reinterpret_cast<const uint64_t*>(command.Storage)));
					++emitted;
				}
				base += buffer->m_EntityCount;
			}
			std::memcpy(wire.data() + sizeof(uint32_t), &emitted, sizeof(emitted));
			wire.resize(static_cast<size_t>(cursor - wire.data()));
			identities.resize(static_cast<size_t>(total));
			const int result = detail::PlaybackNativeEntityCommandBuffer(scene, wire.data(), static_cast<int>(wire.size()),
				identities.data(), static_cast<int>(total), mode);
			if (result < 0) return result;
			handles.resize(static_cast<size_t>(total), entt::null);
			for (size_t index = 0; index < total; ++index)
				if (identities[index]) scene.TryResolveEntityRef(identities[index], handles[index]);
		}
		else {
			using Traits = entt::entt_traits<EntityHandle>;
			if (scene.GetEntityCount() + total > Traits::entity_mask) return kEcbErrorEntityCapExceeded;
			scene.ReserveForLoadRuntime(static_cast<size_t>(total), {});
			handles.resize(static_cast<size_t>(total), entt::null);
			scene.CreateEntitiesBulk(static_cast<size_t>(total), handles);
			Scene::LoadGuard guard(scene);
			scene.SetRuntimeEntityMetaDataBulk(handles);
			uint32_t base = 0;
			for (auto* buffer : buffers) {
				for (size_t index = 0; index < buffer->m_CommandCount; ++index) {
					auto& command = buffer->m_Commands[index];
					EntityHandle liveHandle = command.Live ? static_cast<EntityHandle>(command.Target) : entt::null;
					EntityHandle& handle = command.Live ? liveHandle : handles[base + static_cast<uint32_t>(command.Target)];
					if (!scene.IsValid(handle)) continue;
					const auto* info = components && (command.Action == Operation::DefaultAdd || command.Action == Operation::Remove)
						? components->GetByTypeId(resolveType(command.TypeHash)) : nullptr;
					if (info) {
						if (!info->IsModuleActive()) continue;
						if (command.Action == Operation::Remove) ScriptBindings::RemoveComponent(scene.GetEntity(handle), *info);
						else ScriptBindings::AddComponent(scene.GetEntity(handle), *info);
					}
					else command.Apply(command.Storage, scene, handle);
				}
				base += buffer->m_EntityCount;
			}
			scene.MarkAllDirtyOnce();
		}
		uint32_t base = 0;
		for (auto* buffer : buffers) {
			if (&buffer->m_CreatedHandles != &handles)
				buffer->m_CreatedHandles.assign(handles.begin() + base, handles.begin() + base + buffer->m_EntityCount);
			base += buffer->m_EntityCount;
		}
		return static_cast<int>(total);
	}

	void NativeEntityCommandBuffer::Clear() {
		if (m_Playing) throw std::logic_error("Cannot clear a playing entity command buffer");
		for (size_t index = 0; index < m_CommandCount; ++index) {
			auto& command = m_Commands[index];
			if (command.DestroyCapture) command.DestroyCapture(command.Storage);
		}
		m_CommandCount = 0;
		m_EntityCount = 0;
		m_CreatedHandles.clear();
		m_LiveSceneAnchor = Entity::Null;
		m_HasLiveScene = false;
		++m_Generation;
	}

	EntityHandle NativeEntityCommandBuffer::GetCreatedEntity(int index) const {
		return index < 0 || static_cast<size_t>(index) >= m_CreatedHandles.size()
			? entt::null : m_CreatedHandles[static_cast<size_t>(index)];
	}
}
