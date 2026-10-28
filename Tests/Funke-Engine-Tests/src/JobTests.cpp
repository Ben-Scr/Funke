#include <doctest/doctest.h>

#include "Funke/Core.hpp"
#include "Scripting/ScriptBindings.hpp"
#include "Scripting/ScriptGlue.hpp"

#include <atomic>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace Funke;

namespace {
	struct ScopedJobSystem {
		ScopedJobSystem() {
			JobSystem::Shutdown();

			JobSystemSpec spec;
			spec.WorkerCount = 2;
			JobSystem::Initialize(spec);
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
