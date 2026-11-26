#pragma once

#include "Jobs/JobSystemSpec.hpp"
#include "Core/ThreadingSettings.hpp"

#include <functional>

namespace Funke {

	// Uninitialized Schedule falls back to inline execution rather than crashing.
	class FUNKE_API JobSystem {
	public:
		static void Initialize(const JobSystemSpec& spec = {});
		static void Shutdown();
		static bool IsInitialized();

		static int  Reconfigure(int workerCount);

		static int  GetWorkerCount();
		static void SetParallelWorkloads(uint32_t workloads);
		static uint32_t GetParallelWorkloads();
		static bool CanParallelize(ParallelWorkload workload);
		static bool IsCallerWorker();
		// 0..GetWorkerCount()-1 when called from a worker, -1 elsewhere.
		// Cheap (thread_local read) — fine to call in hot paths.
		static int  GetWorkerIndex();

		// Internal — public for template access; prefer Job::Schedule / ParallelFor / ParallelForAsync.
		static void Enqueue(std::function<void()> work);
		static bool TryPopAndRun();

	private:
		JobSystem() = delete;
	};

}
