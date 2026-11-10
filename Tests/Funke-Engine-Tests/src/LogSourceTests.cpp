#include <doctest/doctest.h>
#include "Core/Log.hpp"
#include <thread>
#include <vector>

TEST_CASE("Log source metadata survives formatting and worker lifetime") {
	std::vector<Funke::Log::Entry> entries;
	const auto subscription = Funke::Log::OnLog.Add([&](const Funke::Log::Entry& entry) { entries.push_back(entry); });
	const int first = __LINE__ + 1;
	FNK_INFO("same {}", "message");
	const int second = __LINE__ + 1;
	FNK_TRACE_TAG("Test", "same message");
	std::thread worker([] { FNK_CONSOLE_LOG_WARN("worker"); });
	worker.join();
	const int coreLine = __LINE__ + 1;
	FNK_CORE_INFO("core metadata");
	Funke::Log::OnLog.Remove(subscription);
	REQUIRE(entries.size() == 4);
	CHECK(entries[0].SourceFile == __FILE__);
	CHECK(entries[0].SourceLine == first);
	CHECK(entries[1].SourceLine == second);
	CHECK(entries[2].SourceFile == __FILE__);
	CHECK(entries[2].SourceLine > 0);
	CHECK(entries[3].Source == Funke::Log::Type::Core);
	CHECK(entries[3].SourceFile == __FILE__);
	CHECK(entries[3].SourceLine == coreLine);
}
