#pragma once

#include "Core/Export.hpp"
#include "Scene/EntityHandle.hpp"
#include "Scripting/NativeEntityCommandBuffer.hpp"

#include <thread>
#include <vector>

namespace Funke {

	class FUNKE_API ParallelEntityCommandBuffer {
	public:
		ParallelEntityCommandBuffer();
		explicit ParallelEntityCommandBuffer(size_t writerCount);
		ParallelEntityCommandBuffer(const ParallelEntityCommandBuffer&) = delete;
		ParallelEntityCommandBuffer& operator=(const ParallelEntityCommandBuffer&) = delete;
		ParallelEntityCommandBuffer(ParallelEntityCommandBuffer&&) noexcept = default;
		ParallelEntityCommandBuffer& operator=(ParallelEntityCommandBuffer&&) noexcept = default;

		// JobSystem workers have distinct slots; the creating thread owns the last slot.
		NativeEntityCommandBuffer& AsWriter();
		// Each explicit slot has one producer; join every producer before playback or Clear().
		NativeEntityCommandBuffer& AsWriter(size_t index);
		// Call on the scene/main thread after producers finish; commands merge in writer-slot order.
		int Playback(Scene& scene, EntityCommandBufferPlaybackMode mode = EntityCommandBufferPlaybackMode::Auto);
		void Clear();
		int EntityCount() const;
		int CommandCount() const;
		EntityHandle GetCreatedEntity(int index) const;
		size_t WriterCount() const { return m_Subs.size(); }

	private:
		std::vector<NativeEntityCommandBuffer> m_Subs;
		std::vector<EntityHandle> m_CreatedHandles;
		std::vector<uint8_t> m_Wire;
		std::vector<uint64_t> m_Identities;
		std::thread::id m_OwnerThread;
	};

}
