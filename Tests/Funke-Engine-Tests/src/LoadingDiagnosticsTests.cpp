#include <doctest/doctest.h>
#include "Diagnostics/LoadingDiagnostics.hpp"
#include <limits>
#include <thread>
using namespace Funke;
TEST_CASE("Loading capture retains every sample independently of profiler gates") {
	LoadingDiagnostics::Reset(); LoadingDiagnostics::SetEnabled(false);
	LoadingDiagnostics::Record("ignored", 1); CHECK(LoadingDiagnostics::Snapshot().empty());
	LoadingDiagnostics::SetEnabled(true);
	for (int i = 1; i <= 100; ++i) LoadingDiagnostics::Record("phase", i, 0, 1024);
	LoadingDiagnostics::Record("phase", std::numeric_limits<double>::quiet_NaN());
	auto result = LoadingDiagnostics::Snapshot(); REQUIRE(result.size() == 1);
	CHECK(result[0].Calls == 100); CHECK(result[0].Units == 102400); CHECK(result[0].TotalMs == 5050);
	CHECK(result[0].P95Ms == 95); CHECK(result[0].P99Ms == 99); CHECK(result[0].MaxMs == 100); CHECK(result[0].OverBudget == 100);
	LoadingDiagnostics::SetEnabled(false); LoadingDiagnostics::Reset();
}
TEST_CASE("Loading capture bounds quantile memory and accepts concurrent worker phases") {
	LoadingDiagnostics::Reset(); LoadingDiagnostics::SetEnabled(true);
	LoadingDiagnostics::Record("worker", 99);
	std::thread first([] { for (int i = 0; i < 5000; ++i) LoadingDiagnostics::Record("worker", 1); });
	std::thread second([] { for (int i = 0; i < 5000; ++i) LoadingDiagnostics::Record("worker", 1); });
	first.join(); second.join();
	const auto result = LoadingDiagnostics::Snapshot(); REQUIRE(result.size() == 1);
	CHECK(result[0].Calls == 10001); CHECK(result[0].WindowSamples == 4096); CHECK(result[0].P99Ms == 1); CHECK(result[0].MaxMs == 99);
	LoadingDiagnostics::SetEnabled(false); LoadingDiagnostics::Reset();
}
