#include "pch.hpp"
#include "Scripting/ScriptBindingGuard.hpp"

#include "Jobs/Job.hpp"
#include "Jobs/JobSystem.hpp"
#include "Jobs/ParallelFor.hpp"
#include "Scripting/ScriptGlue.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace Funke {

	struct JobBindingMap {
		std::mutex Mutex;
		std::unordered_map<uint64_t, JobHandle> Entries;
		std::atomic<uint64_t> NextId{ 1 };
	};

	static JobBindingMap& GetMap() {
		static JobBindingMap map;
		return map;
	}

	template<typename Work>
	static uint64_t ScheduleScriptJob(void* context, void (*releaseContext)(void*), Work work) {
		auto& map = GetMap();
		const uint64_t id = map.NextId.fetch_add(1, std::memory_order_relaxed);
		auto release = [id, releaseContext](void* owned) {
			if (releaseContext) releaseContext(owned);
			auto& map = GetMap();
			std::scoped_lock lock(map.Mutex);
			map.Entries.erase(id);
		};
		// Ownership starts before any allocation; rejected submissions also release exactly once.
		std::shared_ptr<void> owner(context, release);
		auto block = JobInternal::CreateBlock(1);
		{
			std::scoped_lock lock(map.Mutex);
			map.Entries.emplace(id, JobHandle(block));
		}
		JobInternal::Enqueue(block, [owner = std::move(owner), work]() { work(); });
		return id;
	}

	static uint64_t Funke_JobSystem_Enqueue(void (*work)(void*), void* context, void (*releaseContext)(void*)) {
		return ScheduleScriptJob(context, releaseContext, [work, context]() {
			if (work) work(context);
		});
	}

	static uint64_t Funke_JobSystem_ParallelFor(int begin, int end, int batchSize,
		void (*work)(void*, int, int), void* context, void (*releaseContext)(void*)) {
		return ScheduleScriptJob(context, releaseContext, [=]() {
			if (!work || begin >= end) return;
			const size_t length = static_cast<size_t>(static_cast<int64_t>(end) - begin);
			// The owner outlives every partition, including a partition that fails early.
			ParallelFor(0, length, [=](size_t lo, size_t hi) {
				work(context, static_cast<int>(static_cast<int64_t>(begin) + lo),
					static_cast<int>(static_cast<int64_t>(begin) + hi));
			}, static_cast<size_t>(batchSize < 0 ? 0 : batchSize));
		});
	}

	static void Funke_JobSystem_Wait(uint64_t id) {
		JobHandle handle;
		{
			auto& map = GetMap();
			std::scoped_lock lock(map.Mutex);
			auto it = map.Entries.find(id);
			if (it == map.Entries.end()) return;
			handle = it->second;
		}
		Job::Wait(handle);
	}

	static int Funke_JobSystem_IsComplete(uint64_t id) {
		auto& map = GetMap();
		std::scoped_lock lock(map.Mutex);
		return map.Entries.find(id) == map.Entries.end() ? 1 : 0;
	}

	static void Funke_JobSystem_Release(uint64_t id) {
		// Legacy callers may release early; workers retain ownership until completion.
		Funke_JobSystem_Wait(id);
	}

	FUNKE_API size_t GetScriptJobRegistrationCount() {
		auto& map = GetMap();
		std::scoped_lock lock(map.Mutex);
		return map.Entries.size();
	}

	static int Funke_JobSystem_GetWorkerCount()
	{
		return JobSystem::GetWorkerCount();
	}

	static int Funke_JobSystem_GetCallerWorkerIndex()
	{
		return JobSystem::GetWorkerIndex();
	}

	static int Funke_JobSystem_Reconfigure(int workerCount)
	{
		return JobSystem::Reconfigure(workerCount);
	}

	void PopulateJobsBindings(NativeBindings& b)
	{
		b.JobSystem_Enqueue              = FNK_GUARDED_BINDING(Funke_JobSystem_Enqueue);
		b.JobSystem_ParallelFor          = FNK_GUARDED_BINDING(Funke_JobSystem_ParallelFor);
		b.JobSystem_Wait                 = FNK_GUARDED_BINDING(Funke_JobSystem_Wait);
		b.JobSystem_IsComplete           = FNK_GUARDED_BINDING(Funke_JobSystem_IsComplete);
		b.JobSystem_Release              = FNK_GUARDED_BINDING(Funke_JobSystem_Release);
		b.JobSystem_GetWorkerCount       = FNK_GUARDED_BINDING(Funke_JobSystem_GetWorkerCount);
		b.JobSystem_GetCallerWorkerIndex = FNK_GUARDED_BINDING(Funke_JobSystem_GetCallerWorkerIndex);
		b.JobSystem_Reconfigure          = FNK_GUARDED_BINDING(Funke_JobSystem_Reconfigure);
	}

} // namespace Funke
