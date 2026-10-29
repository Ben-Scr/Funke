#include "pch.hpp"
#include "Scripting/ParallelEntityCommandBuffer.hpp"

#include "Jobs/JobSystem.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Funke {

	ParallelEntityCommandBuffer::ParallelEntityCommandBuffer()
		: ParallelEntityCommandBuffer(static_cast<size_t>(std::max(0, JobSystem::GetWorkerCount())) + 1) {}

	ParallelEntityCommandBuffer::ParallelEntityCommandBuffer(size_t writerCount)
		: m_Subs(writerCount), m_OwnerThread(std::this_thread::get_id()) {
		if (writerCount == 0) throw std::invalid_argument("Parallel ECB requires at least one writer");
	}

	NativeEntityCommandBuffer& ParallelEntityCommandBuffer::AsWriter() {
		const int worker = JobSystem::GetWorkerIndex();
		if (worker >= 0 && static_cast<size_t>(worker) + 1 < m_Subs.size())
			return m_Subs[static_cast<size_t>(worker)];
		if (std::this_thread::get_id() != m_OwnerThread)
			throw std::logic_error("External threads must use distinct explicit ECB writer slots");
		return m_Subs.back();
	}

	NativeEntityCommandBuffer& ParallelEntityCommandBuffer::AsWriter(size_t index) {
		if (index >= m_Subs.size()) throw std::out_of_range("Parallel ECB writer index out of range");
		return m_Subs[index];
	}

	int ParallelEntityCommandBuffer::EntityCount() const {
		size_t total = 0;
		for (const auto& writer : m_Subs) total += writer.m_EntityCount;
		return total > INT_MAX ? INT_MAX : static_cast<int>(total);
	}

	int ParallelEntityCommandBuffer::CommandCount() const {
		size_t total = 0;
		for (const auto& writer : m_Subs) total += writer.m_CommandCount;
		return total > INT_MAX ? INT_MAX : static_cast<int>(total);
	}

	int ParallelEntityCommandBuffer::Playback(Scene& scene, EntityCommandBufferPlaybackMode mode) {
		std::vector<NativeEntityCommandBuffer*> writers;
		writers.reserve(m_Subs.size());
		for (auto& writer : m_Subs) writers.push_back(&writer);
		return NativeEntityCommandBuffer::PlaybackBuffers(scene, writers, m_CreatedHandles, m_Wire, m_Identities, mode);
	}

	void ParallelEntityCommandBuffer::Clear() {
		for (auto& writer : m_Subs) writer.Clear();
		m_CreatedHandles.clear();
	}

	EntityHandle ParallelEntityCommandBuffer::GetCreatedEntity(int index) const {
		return index < 0 || static_cast<size_t>(index) >= m_CreatedHandles.size()
			? entt::null : m_CreatedHandles[static_cast<size_t>(index)];
	}

}
