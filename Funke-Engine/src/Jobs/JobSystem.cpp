#include "pch.hpp"

#include "Jobs/JobSystem.hpp"
#include "Core/Platform.hpp"

#include "Profiling/Profiler.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include <blockingconcurrentqueue.h>


namespace Funke {

	namespace {

		// MPMC lock-free queue; Enqueue wait-free in common path. Replaces prior mutex+deque+condvar that serialized all operations and thundering-herded workers.
		moodycamel::BlockingConcurrentQueue<std::function<void()>> s_Queue;

		// Single PoolState atomic: previously two independent atomics let Shutdown flip Running between the Enqueue load and the actual insert, stranding jobs.
		enum class PoolState : uint8_t { Uninit = 0, Running = 1, ShuttingDown = 2 };
		std::atomic<PoolState>           s_State{ PoolState::Uninit };
		std::vector<std::thread>         s_Workers;

		// Hot-path readers (GetWorkerCount on the render thread, ParallelFor) must
		// NOT touch s_Workers — a concurrent Reconfigure/Shutdown mutates the vector.
		// Mirror the size into an atomic instead.
		std::atomic<int>                 s_WorkerCount{ 0 };

		// Serializes the multi-step structural transitions (Initialize/Shutdown/
		// Reconfigure) against each other. Reconfigure re-enters Shutdown+Initialize,
		// so this is recursive. Hot-path readers stay lock-free via s_State/s_WorkerCount.
		std::recursive_mutex             s_LifecycleMutex;

		thread_local int                 t_WorkerIndex = -1;

		int ResolveAutoWorkerCount(int requested) {
			if (requested > 0) {
				return std::min(requested, 32);
			}

			unsigned int hw = std::thread::hardware_concurrency();
			if (hw == 0) hw = 4; // hardware_concurrency may return 0 on unusual platforms.

			int autoCount = static_cast<int>(hw) - 1;
#if FUNKE_WITH_SCRIPTING
			// Cap at hw-2: leaves headroom for the .NET ThreadPool (Funke.Jobs) and main thread on 4-core hosts.
			autoCount = std::min(autoCount, static_cast<int>(hw) - 2);
#endif
			if (autoCount < 2)  autoCount = 2;
			if (autoCount > 16) autoCount = 16;
			return autoCount;
		}

		void SetWorkerThreadName(int workerIndex) {
			char name[32];
			std::snprintf(name, sizeof(name), "Funke Worker %d", workerIndex);
			Platform::SetCurrentThreadName(name);
			FUNKE_PROFILE_THREAD_NAME(name);
		}

		void ExecuteJob(std::function<void()>&& job) {
			FUNKE_PROFILE_SCOPE("JobSystem::Job");
			try {
				if (job) job();
			}
			catch (const std::exception& e) {
				FNK_CORE_ERROR_TAG("JobSystem", "Job failed: {}", e.what());
			}
			catch (...) {
				FNK_CORE_ERROR_TAG("JobSystem", "Job failed: unknown exception");
			}
		}

		void WorkerMain(int workerIndex) {
			t_WorkerIndex = workerIndex;
			SetWorkerThreadName(workerIndex);

			std::function<void()> job;
			while (s_State.load(std::memory_order_acquire) == PoolState::Running) {
				if (s_Queue.wait_dequeue_timed(job, std::chrono::milliseconds(50))) {
					ExecuteJob(std::move(job));
				}
			}

			while (s_Queue.try_dequeue(job)) {
				ExecuteJob(std::move(job));
			}
		}

	} // namespace

	void JobSystem::Initialize(const JobSystemSpec& spec) {
		std::lock_guard<std::recursive_mutex> lifecycle(s_LifecycleMutex);
		if (s_State.load(std::memory_order_acquire) != PoolState::Uninit) {
			FNK_CORE_WARN_TAG("JobSystem", "Initialize called twice — ignoring second call");
			return;
		}

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
		// Browser builds without SharedArrayBuffer have no threads; Enqueue runs jobs inline while the pool is down.
		(void)spec;
		FNK_CORE_INFO_TAG("JobSystem", "No thread support on this platform; jobs run inline");
		return;
#endif
		const int workerCount = ResolveAutoWorkerCount(spec.WorkerCount);

		s_Workers.reserve(static_cast<size_t>(workerCount));
		// Flip state to Running BEFORE spawning workers so the first thing
		// each WorkerMain sees is the live state, not Uninit.
		s_State.store(PoolState::Running, std::memory_order_release);
		for (int i = 0; i < workerCount; ++i) {
			s_Workers.emplace_back(WorkerMain, i);
		}
		s_WorkerCount.store(workerCount, std::memory_order_release);

		FNK_CORE_INFO_TAG("JobSystem", "Started with {} worker thread(s)", workerCount);
	}

	void JobSystem::Shutdown() {
		// A worker joining itself throws resource_deadlock_would_occur and would wedge the pool in ShuttingDown.
		if (IsCallerWorker()) {
			FNK_CORE_ERROR_TAG("JobSystem", "Shutdown/Reconfigure cannot be called from a job worker thread");
			return;
		}
		std::lock_guard<std::recursive_mutex> lifecycle(s_LifecycleMutex);
		if (s_State.load(std::memory_order_acquire) != PoolState::Running) {
			return;
		}

		s_State.store(PoolState::ShuttingDown, std::memory_order_release);

		// Poison-pill wake: N empty jobs unblock workers so the Running check fires promptly; genuine work that slipped in is drained by WorkerMain's post-loop.
		const size_t workerCount = s_Workers.size();
		for (size_t i = 0; i < workerCount; ++i) {
			s_Queue.enqueue(std::function<void()>{});
		}

		for (auto& worker : s_Workers) {
			if (worker.joinable()) {
				worker.join();
			}
		}
		s_Workers.clear();
		s_WorkerCount.store(0, std::memory_order_release);

		// Run (not discard) anything that slipped in after the workers drained: a dropped job never signals its handle, so Job::Wait would spin forever.
		// Jobs enqueued from here on see ShuttingDown and run inline.
		std::function<void()> leftover;
		while (s_Queue.try_dequeue(leftover)) {
			ExecuteJob(std::move(leftover));
		}

		s_State.store(PoolState::Uninit, std::memory_order_release);
		FNK_CORE_INFO_TAG("JobSystem", "Stopped");
	}

	int JobSystem::Reconfigure(int workerCount) {
		if (IsCallerWorker()) {
			FNK_CORE_ERROR_TAG("JobSystem", "Reconfigure cannot be called from a job worker thread");
			return GetWorkerCount();
		}
		std::lock_guard<std::recursive_mutex> lifecycle(s_LifecycleMutex);
		Shutdown();
		JobSystemSpec spec;
		spec.WorkerCount = workerCount;
		Initialize(spec);
		return GetWorkerCount();
	}

	bool JobSystem::IsInitialized() {
		return s_State.load(std::memory_order_acquire) == PoolState::Running;
	}

	int JobSystem::GetWorkerCount() {
		// Lock-free read of the mirrored count; never touches s_Workers (which a
		// concurrent Reconfigure/Shutdown may be mutating).
		return s_WorkerCount.load(std::memory_order_acquire);
	}

	bool JobSystem::IsCallerWorker() {
		return t_WorkerIndex >= 0;
	}

	int JobSystem::GetWorkerIndex() {
		return t_WorkerIndex;
	}

	void JobSystem::Enqueue(std::function<void()> work) {
		if (!work) return;

		// Run inline if the pool is not up; the second state check below drains anything that raced with Shutdown.
		if (s_State.load(std::memory_order_acquire) != PoolState::Running) {
			ExecuteJob(std::move(work));
			return;
		}
		if (!s_Queue.enqueue(std::move(work))) {
			ExecuteJob(std::move(work));
			return;
		}

		// Re-check: Shutdown may have flipped after the first load; workers' drain loop may have already exited, so drain inline here to avoid losing the enqueue.
		if (s_State.load(std::memory_order_acquire) != PoolState::Running) {
			std::function<void()> drained;
			while (s_Queue.try_dequeue(drained)) {
				if (drained) ExecuteJob(std::move(drained));
			}
		}
	}

	bool JobSystem::TryPopAndRun() {
		std::function<void()> job;
		if (!s_Queue.try_dequeue(job)) {
			return false;
		}
		ExecuteJob(std::move(job));
		return true;
	}

} // namespace Funke
