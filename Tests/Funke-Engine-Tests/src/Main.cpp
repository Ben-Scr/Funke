#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Core/Memory.hpp"
#include "Serialization/Path.hpp"
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <vector>

TEST_CASE("AllocatorShutdownProbe" * doctest::skip()) {
	Funke::Allocator::Init();
	std::vector<void*> late;
	for (int i = 0; i < 1000; ++i) {
		void* block = Funke::Allocator::Allocate(64, "probe");
		Funke::Allocator::Free(block);
	}
	for (int i = 0; i < 32; ++i) late.push_back(Funke::Allocator::Allocate(32, "late"));
	std::atomic<bool> start{ false };
	std::vector<std::thread> racers;
	for (int i = 0; i < 4; ++i) racers.emplace_back([&] {
		while (!start.load()) std::this_thread::yield();
		for (int n = 0; n < 1000; ++n) {
			auto* block = Funke::Allocator::Allocate(16, "racer");
			Funke::Allocator::Free(block);
			(void)Funke::Allocator::GetAllocationStats();
		}
	});
	start.store(true);
	Funke::Allocator::Shutdown();
	for (auto& racer : racers) racer.join();
	for (void* block : late) Funke::Allocator::Free(block);
	for (int i = 0; i < 50; ++i) {
		Funke::Allocator::Init();
		Funke::Allocator::Free(Funke::Allocator::Allocate(64));
		Funke::Allocator::Shutdown();
		CHECK(Funke::Allocator::GetAllocationStats().empty());
	}
	MESSAGE("Static process-lifetime allocator storage: ", sizeof(Funke::AllocatorData), " bytes; tracking maps cleared");
}

TEST_CASE("Allocator lifecycle runs in an isolated process") {
#ifdef _WIN32
	const auto name = "Funke-Engine-Tests.exe";
#else
	const auto name = "Funke-Engine-Tests";
#endif
	const auto executable = std::filesystem::path(Funke::Path::ExecutableDir()) / name;
	std::string command = "\"" + executable.string() + "\" --no-skip=true --test-case=\"AllocatorShutdownProbe\"";
#ifdef _WIN32
	command = "\"" + command + "\"";
#endif
	CHECK(std::system(command.c_str()) == 0);
}
