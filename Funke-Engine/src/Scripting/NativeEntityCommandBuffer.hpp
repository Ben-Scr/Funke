#pragma once

#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"
#include "Scene/Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <new>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace Funke {

	enum class EntityCommandBufferPlaybackMode { Auto = 0, Sequential = 1, Parallel = 2 };

	namespace detail {
		FUNKE_API int PlaybackNativeEntityCommandBuffer(Scene& scene, const uint8_t* bytes, int length,
			uint64_t* identities, int capacity, EntityCommandBufferPlaybackMode mode);
	}

	// Recording is single-threaded; ParallelEntityCommandBuffer provides independent writer slots.
	class FUNKE_API NativeEntityCommandBuffer {
	public:
		struct EntityRef {
			uint32_t Index = UINT32_MAX;
		private:
			uint64_t BufferToken = 0;
			uint64_t Generation = 0;
			EntityHandle Handle = entt::null;
			Entity LiveEntity = Entity::Null;
			friend class NativeEntityCommandBuffer;
		};

		NativeEntityCommandBuffer();
		~NativeEntityCommandBuffer();
		NativeEntityCommandBuffer(const NativeEntityCommandBuffer&) = delete;
		NativeEntityCommandBuffer& operator=(const NativeEntityCommandBuffer&) = delete;
		NativeEntityCommandBuffer(NativeEntityCommandBuffer&& other);
		NativeEntityCommandBuffer& operator=(NativeEntityCommandBuffer&& other) noexcept;

		EntityRef CreateEntity();
		// Live refs can be shared between writers; their scene and entity generation are retained.
		static EntityRef Existing(Entity entity);
		EntityRef Instantiate(uint64_t prefabGuid);
		EntityRef Instantiate(Entity prefabAsset);

		template<typename T> void AddComponent(const EntityRef& entity);
		template<typename T> void AddComponent(const EntityRef& entity, T value);
		template<typename T, typename Configure>
			requires (!std::is_same_v<std::decay_t<Configure>, T> && std::is_invocable_v<std::decay_t<Configure>&, T&>)
		void AddComponent(const EntityRef& entity, Configure&& configure);
		template<typename T> void RemoveComponent(const EntityRef& entity);
		void Destroy(const EntityRef& entity);
		void SetEnabled(const EntityRef& entity, bool enabled);

		template<typename... Ts> EntityRef CreateWith();
		template<typename... Ts> void CreateEntitiesWith(int length, std::span<EntityRef> output);

		// Call on the scene/main thread after producers finish; unsupported parallel commands execute serially.
		int Playback(Scene& scene, EntityCommandBufferPlaybackMode mode = EntityCommandBufferPlaybackMode::Auto);
		void Clear();
		int EntityCount() const { return static_cast<int>(m_EntityCount); }
		int CommandCount() const { return static_cast<int>(m_CommandCount); }
		EntityHandle GetCreatedEntity(int index) const;
		static constexpr std::size_t kInlineStorageBytes = 32;

	private:
		enum class Operation : uint8_t { Custom, DefaultAdd, Remove, Destroy, SetEnabled, Instantiate };
		struct Command {
			uint64_t Target;
			void (*Apply)(void*, Scene&, EntityHandle&);
			void (*DestroyCapture)(void*);
			entt::id_type TypeHash;
			Operation Action;
			bool Live;
			alignas(std::max_align_t) std::byte Storage[kInlineStorageBytes];
		};
		static_assert(sizeof(Command) <= 64, "Command must fit in one cache line");

		Command& AppendCommand(const EntityRef& entity, void (*apply)(void*, Scene&, EntityHandle&),
			Operation action = Operation::Custom, entt::id_type typeHash = 0);
		void ValidateScene(Scene& scene) const;
		static int PlaybackBuffers(Scene& scene, std::span<NativeEntityCommandBuffer*> buffers,
			std::vector<EntityHandle>& handles, std::vector<uint8_t>& wire, std::vector<uint64_t>& identities,
			EntityCommandBufferPlaybackMode mode);

		template<typename T, typename Value>
		void ConstructPayload(Command& command, Value&& value) {
			try { ::new (command.Storage) T(std::forward<Value>(value)); }
			catch (...) { --m_CommandCount; throw; }
			if constexpr (!std::is_trivially_destructible_v<T>)
				command.DestroyCapture = [](void* state) { std::launder(reinterpret_cast<T*>(state))->~T(); };
		}

		std::deque<Command> m_Commands;
		std::vector<EntityHandle> m_CreatedHandles;
		std::vector<uint8_t> m_Wire;
		std::vector<uint64_t> m_Identities;
		Entity m_LiveSceneAnchor = Entity::Null;
		std::size_t m_CommandCount = 0;
		uint32_t m_EntityCount = 0;
		uint64_t m_BufferToken;
		uint64_t m_Generation = 0;
		bool m_HasLiveScene = false;
		bool m_Playing = false;
		friend class ParallelEntityCommandBuffer;
	};

	template<typename T>
	void NativeEntityCommandBuffer::AddComponent(const EntityRef& entity) {
		static_assert(std::is_empty_v<T> || std::is_default_constructible_v<T>, "Default AddComponent requires a default constructor");
		AppendCommand(entity, [](void*, Scene& scene, EntityHandle& handle) {
			auto& registry = scene.GetRegistry();
			if (!registry.all_of<T>(handle)) registry.emplace<T>(handle);
		}, Operation::DefaultAdd, entt::type_hash<T>::value());
	}

	template<typename T>
	void NativeEntityCommandBuffer::AddComponent(const EntityRef& entity, T value) {
		static_assert(!std::is_empty_v<T> && sizeof(T) <= kInlineStorageBytes,
			"Component exceeds inline storage; use a configure lambda with a small capture");
		static_assert(alignof(T) <= alignof(std::max_align_t));
		Command& command = AppendCommand(entity, [](void* state, Scene& scene, EntityHandle& handle) {
			auto& registry = scene.GetRegistry();
			if (!registry.all_of<T>(handle)) registry.emplace<T>(handle, std::move(*std::launder(reinterpret_cast<T*>(state))));
		});
		ConstructPayload<T>(command, std::move(value));
	}

	template<typename T, typename Configure>
		requires (!std::is_same_v<std::decay_t<Configure>, T> && std::is_invocable_v<std::decay_t<Configure>&, T&>)
	void NativeEntityCommandBuffer::AddComponent(const EntityRef& entity, Configure&& configure) {
		using Patch = std::decay_t<Configure>;
		static_assert(sizeof(Patch) <= kInlineStorageBytes, "Capture exceeds inline storage; capture a pointer instead");
		static_assert(alignof(Patch) <= alignof(std::max_align_t));
		static_assert(!std::is_empty_v<T> && std::is_default_constructible_v<T>);
		static_assert(std::is_invocable_v<Patch&, T&>);
		Command& command = AppendCommand(entity, [](void* state, Scene& scene, EntityHandle& handle) {
			auto& registry = scene.GetRegistry();
			if (registry.all_of<T>(handle)) return;
			T& component = registry.emplace<T>(handle);
			(*std::launder(reinterpret_cast<Patch*>(state)))(component);
		});
		ConstructPayload<Patch>(command, std::forward<Configure>(configure));
	}

	template<typename T>
	void NativeEntityCommandBuffer::RemoveComponent(const EntityRef& entity) {
		AppendCommand(entity, [](void*, Scene& scene, EntityHandle& handle) {
			scene.GetRegistry().remove<T>(handle);
		}, Operation::Remove, entt::type_hash<T>::value());
	}

	template<typename... Ts>
	NativeEntityCommandBuffer::EntityRef NativeEntityCommandBuffer::CreateWith() {
		EntityRef entity = CreateEntity();
		(AddComponent<Ts>(entity), ...);
		return entity;
	}

	template<typename... Ts>
	void NativeEntityCommandBuffer::CreateEntitiesWith(int length, std::span<EntityRef> output) {
		if (length < 0 || static_cast<std::size_t>(length) > output.size()) return;
		for (int index = 0; index < length; ++index) output[index] = CreateWith<Ts...>();
	}

}
