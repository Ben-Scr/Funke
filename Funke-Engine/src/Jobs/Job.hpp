#pragma once

#include "Core/Export.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

namespace Funke {

	struct JobControlBlock;

	// Default-constructed handle is invalid; IsComplete() returns true for it ("no job" = "already done").
	class JobHandle {
	public:
		JobHandle() = default;

		FUNKE_API bool IsValid()    const;
		FUNKE_API bool IsComplete() const;

		explicit JobHandle(std::shared_ptr<JobControlBlock> block) noexcept
			: m_Block(std::move(block)) {
		}

		const std::shared_ptr<JobControlBlock>& GetBlock() const noexcept { return m_Block; }

	private:
		std::shared_ptr<JobControlBlock> m_Block;
	};

	class FUNKE_API Job {
	public:
		static JobHandle Schedule(std::function<void()> work);
		// Work-steals the queue while waiting, so nested sub-job waits cannot deadlock.
		static void Wait(const JobHandle& handle);

		// Non-blocking completion check. Cheap atomic load.
		static bool IsComplete(const JobHandle& handle);

	private:
		Job() = delete;
	};

	namespace JobInternal {

		FUNKE_API std::shared_ptr<JobControlBlock> CreateBlock(int pending);
		FUNKE_API void NotifyOne(const std::shared_ptr<JobControlBlock>& block);
		// Completion is published after the callable and its captures are destroyed.
		FUNKE_API void Enqueue(const std::shared_ptr<JobControlBlock>& block, std::function<void()> work);

	}

} // namespace Funke
