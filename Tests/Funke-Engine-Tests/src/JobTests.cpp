#include <doctest/doctest.h>

#include "Funke/Core.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptGlue.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace Funke;

namespace {
	struct ScopedJobSystem {
		explicit ScopedJobSystem(int workerCount = 2) {
			JobSystem::Shutdown();

			if (workerCount > 0) {
				JobSystemSpec spec;
				spec.WorkerCount = workerCount;
				JobSystem::Initialize(spec);
			}
		}

		~ScopedJobSystem() {
			JobSystem::Shutdown();
		}
	};
}

TEST_CASE("Job API schedules work and completes handles") {
	ScopedJobSystem jobs;

	std::atomic<int> value{ 0 };
	JobHandle handle = Job::Schedule([&]() {
		value.store(42, std::memory_order_release);
	});

	CHECK(handle.IsValid());

	Job::Wait(handle);

	CHECK(Job::IsComplete(handle));
	CHECK(value.load(std::memory_order_acquire) == 42);
}

TEST_CASE("Job API completes handles when work throws") {
	ScopedJobSystem jobs;

	JobHandle handle = Job::Schedule([]() {
		throw std::runtime_error("expected test failure");
	});

	Job::Wait(handle);
	CHECK(handle.IsComplete());
}

TEST_CASE("Job completion releases callback captures before Wait returns") {
	for (int workerCount : { 0, 2 }) {
		for (bool throws : { false, true }) {
			CAPTURE(workerCount);
			CAPTURE(throws);
			ScopedJobSystem jobs(workerCount);
			const bool hasWorkers = JobSystem::GetWorkerCount() > 0;
			auto owned = std::make_shared<int>(73);
			std::weak_ptr<int> lifetime = owned;
			std::promise<void> started;
			auto startedFuture = started.get_future();
			std::promise<void> release;
			auto releaseFuture = release.get_future().share();
			if (!hasWorkers) release.set_value();
			std::atomic<bool> ranOnWorker{ false };
			std::atomic<int> value{ 0 };
			JobHandle handle = Job::Schedule([captured = std::move(owned), &started, releaseFuture, &ranOnWorker, &value, throws] {
				value.store(*captured);
				ranOnWorker.store(JobSystem::IsCallerWorker());
				started.set_value();
				releaseFuture.wait();
				if (throws) throw std::runtime_error("expected capture lifetime failure");
			});
			const auto status = startedFuture.wait_for(std::chrono::seconds(5));
			if (hasWorkers) release.set_value();
			Job::Wait(handle);
			REQUIRE(status == std::future_status::ready);
			CHECK(handle.IsComplete());
			CHECK(value.load() == 73);
			CHECK(ranOnWorker.load() == hasWorkers);
			CHECK(lifetime.expired());
		}
	}
}

TEST_CASE("ParallelFor completion releases its shared callback before Wait returns") {
	for (int workerCount : { 0, 2 }) {
		for (bool throws : { false, true }) {
			CAPTURE(workerCount);
			CAPTURE(throws);
			ScopedJobSystem jobs(workerCount);
			const bool hasWorkers = JobSystem::GetWorkerCount() > 0;
			auto owned = std::make_shared<int>(29);
			std::weak_ptr<int> lifetime = owned;
			std::promise<void> started;
			auto startedFuture = started.get_future();
			std::promise<void> release;
			auto releaseFuture = release.get_future().share();
			if (!hasWorkers) release.set_value();
			std::atomic<int> entered{ 0 };
			std::atomic<int> visited{ 0 };
			std::atomic<bool> firstRanOnWorker{ false };
			JobHandle handle = ParallelForAsync(0, 64,
				[captured = std::move(owned), &started, releaseFuture, &entered, &visited, &firstRanOnWorker, throws](size_t lo, size_t hi) {
					if (entered.fetch_add(1) == 0) {
						firstRanOnWorker.store(JobSystem::IsCallerWorker());
						started.set_value();
					}
					releaseFuture.wait();
					visited.fetch_add(static_cast<int>(hi - lo) * *captured);
					if (throws && lo == 0) throw std::runtime_error("expected parallel capture lifetime failure");
				}, 4);
			const auto status = startedFuture.wait_for(std::chrono::seconds(5));
			if (hasWorkers) release.set_value();
			Job::Wait(handle);
			REQUIRE(status == std::future_status::ready);
			CHECK(handle.IsComplete());
			CHECK(visited.load() == 64 * 29);
			CHECK(firstRanOnWorker.load() == hasWorkers);
			CHECK(lifetime.expired());
		}
	}
}

TEST_CASE("ParallelFor handles complete when a chunk throws") {
	ScopedJobSystem jobs;

	JobHandle handle = ParallelForAsync(0, 64, [](size_t i) {
		if (i == 13) {
			throw std::runtime_error("expected chunk failure");
		}
	}, 1);

	Job::Wait(handle);
	CHECK(handle.IsComplete());
}

TEST_CASE("Nested jobs can wait without starving the worker queue") {
	ScopedJobSystem jobs;

	std::atomic<int> visits{ 0 };
	JobHandle outer = Job::Schedule([&]() {
		ParallelFor(0, 128, [&](size_t) {
			visits.fetch_add(1, std::memory_order_acq_rel);
		}, 1);
	});

	Job::Wait(outer);

	CHECK(outer.IsComplete());
	CHECK(visits.load(std::memory_order_acquire) == 128);
}

TEST_CASE("Shutdown completes every pending job handle") {
	JobSystem::Shutdown();
	JobSystemSpec spec;
	spec.WorkerCount = 2;
	JobSystem::Initialize(spec);

	std::atomic<int> ran{ 0 };
	std::vector<JobHandle> handles;
	for (int i = 0; i < 256; ++i) {
		handles.push_back(Job::Schedule([&]() {
			ran.fetch_add(1, std::memory_order_acq_rel);
		}));
	}

	JobSystem::Shutdown();

	for (const JobHandle& handle : handles) {
		CHECK(handle.IsComplete());
	}
	CHECK(ran.load(std::memory_order_acquire) == 256);
}

TEST_CASE("Jobs scheduled after Shutdown run inline and complete") {
	JobSystem::Shutdown();

	std::atomic<int> value{ 0 };
	JobHandle handle = Job::Schedule([&]() {
		value.store(7, std::memory_order_release);
	});

	CHECK(handle.IsComplete());
	CHECK(value.load(std::memory_order_acquire) == 7);
}

TEST_CASE("Managed parallel range bindings preserve signed bounds") {
	ScopedJobSystem jobs;
	NativeBindings bindings{};
	ScriptBindings::PopulateNativeBindings(bindings);
	const int ranges[][2] = {
		{ -2, 2 }, { -8, -3 }, { 3, 8 },
		{ std::numeric_limits<int>::min(), std::numeric_limits<int>::min() + 3 },
		{ std::numeric_limits<int>::max() - 3, std::numeric_limits<int>::max() }
	};
	for (const auto& range : ranges) {
		struct Counts {
			std::atomic<int> Visits{ 0 };
			std::atomic<int64_t> Sum{ 0 };
		} counts;
		const uint64_t handle = bindings.JobSystem_ParallelFor(range[0], range[1], 1,
			[](void* context, int lo, int hi) {
				auto& result = *static_cast<Counts*>(context);
				for (int i = lo; i < hi; ++i) {
					result.Visits.fetch_add(1);
					result.Sum.fetch_add(i);
				}
			}, &counts, nullptr);
		bindings.JobSystem_Wait(handle);
		bindings.JobSystem_Release(handle);
		const int64_t length = static_cast<int64_t>(range[1]) - range[0];
		CHECK(counts.Visits.load() == length);
		CHECK(counts.Sum.load() == length * (static_cast<int64_t>(range[0]) + range[1] - 1) / 2);
	}
}

TEST_CASE("An OnLog listener can log without deadlocking") {
	std::atomic<int> calls{ 0 };
	const EventId id = Log::OnLog.Add([&](const Log::Entry&) {
		calls.fetch_add(1, std::memory_order_acq_rel);
		FNK_CORE_INFO_TAG("Tests", "logged from an OnLog listener");
	});
	struct Unsubscribe {
		EventId Id;
		~Unsubscribe() { Log::OnLog.Remove(Id); }
	} unsubscribe{ id };

	FNK_INFO_TAG("Tests", "outer log");

	// The nested log reaches stdout/file but is not re-dispatched to listeners.
	CHECK(calls.load(std::memory_order_acquire) == 1);
}
