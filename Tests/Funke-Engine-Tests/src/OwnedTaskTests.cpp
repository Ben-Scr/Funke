#include <doctest/doctest.h>
#include "Utils/OwnedTask.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using namespace Funke;
using namespace std::chrono_literals;

TEST_CASE("OwnedTask stop requests reach a running worker before joining") {
	std::promise<void> started;
	auto entered = started.get_future();
	std::atomic_bool observedStop = false;
	OwnedTask task([&](OwnedTask::StopToken stop) {
		started.set_value();
		const auto deadline = std::chrono::steady_clock::now() + 2s;
		while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		observedStop.store(stop.stop_requested());
	});
	REQUIRE(entered.wait_for(2s) == std::future_status::ready);
	task.RequestStop();
	task.Join();
	CHECK(observedStop.load());
	CHECK_FALSE(task.Joinable());
}

TEST_CASE("OwnedTask destruction requests stop and waits for the final result") {
	std::atomic_bool observedStop = false;
	int result = 0;
	std::promise<void> started;
	auto entered = started.get_future();
	{
		OwnedTask task([&](OwnedTask::StopToken stop) {
			started.set_value();
			const auto deadline = std::chrono::steady_clock::now() + 2s;
			while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline)
				std::this_thread::yield();
			observedStop.store(stop.stop_requested());
			result = 42;
		});
		REQUIRE(entered.wait_for(2s) == std::future_status::ready);
	}
	CHECK(observedStop.load());
	CHECK(result == 42);
}
