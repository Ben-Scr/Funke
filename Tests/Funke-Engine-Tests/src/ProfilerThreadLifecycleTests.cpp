#include <doctest/doctest.h>

#if defined(FUNKE_PROFILER_ENABLED) && defined(_WIN32)
#include "Scripting/ScriptProcessTask.hpp"
#include <common/TracyAlloc.hpp>
#include <windows.h>
#include <psapi.h>
#include <array>
#include <cstring>

namespace {

size_t PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    REQUIRE(K32GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)));
    return counters.PrivateUsage;
}

void RunEmptyCompileTask() {
    auto task = Funke::ScriptProcessTasks::LaunchTask([] {
        Funke::Process::Result result;
        result.ExitCode = 0;
        return result;
    });
    // Join before sampling: Completed alone precedes the thread's TLS cleanup.
    task->Worker.join();
    Funke::Process::Result result;
    REQUIRE(Funke::ScriptProcessTasks::TryTakeTaskResult(task, result));
    REQUIRE(result.Succeeded());
}

void AllocateOnRetiredThreads() {
    std::array<void*, 8> blocks{};
    std::array<std::thread, 8> workers;
    for (size_t i = 0; i < workers.size(); ++i) {
        workers[i] = std::thread([&, i] {
            FUNKE_PROFILE_THREAD_NAME("ProfilerLifecycleTest");
            blocks[i] = tracy::tracy_malloc(1024);
            std::memset(blocks[i], static_cast<int>(i), 1024);
        });
    }
    for (auto& worker : workers) worker.join();
    // A retired heap must remain usable for outstanding cross-thread frees.
    for (size_t i = 0; i < blocks.size(); ++i) {
        REQUIRE(blocks[i] != nullptr);
        CHECK(static_cast<unsigned char*>(blocks[i])[1023] == i);
        tracy::tracy_free(blocks[i]);
    }
}

} // namespace

TEST_CASE("Profiler releases allocator heaps after short-lived script tasks") {
    for (int i = 0; i < 16; ++i) RunEmptyCompileTask();
    const size_t before = PrivateBytes();
    for (int i = 0; i < 64; ++i) RunEmptyCompileTask();
    const size_t after = PrivateBytes();
    INFO("private bytes before=" << before << " after=" << after);
    // Old Windows Tracy integration retained roughly 4 MiB per finished worker.
    CHECK(after <= before + 16 * 1024 * 1024);
}

TEST_CASE("Profiler reuses retired heaps and preserves cross-thread allocations") {
    for (int i = 0; i < 4; ++i) AllocateOnRetiredThreads();
    const size_t before = PrivateBytes();
    for (int i = 0; i < 8; ++i) AllocateOnRetiredThreads();
    const size_t after = PrivateBytes();
    INFO("private bytes before=" << before << " after=" << after);
    CHECK(after <= before + 16 * 1024 * 1024);
}
#endif
