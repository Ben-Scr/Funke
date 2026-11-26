#pragma once
#include "Core/Export.hpp"
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace Funke {

	struct LoadingPhaseStatistics {
		std::string Name;
		uint64_t Calls = 0, Units = 0, OverBudget = 0;
		double TotalMs = 0, MaxMs = 0, P95Ms = 0, P99Ms = 0;
		double LastBudgetMs = -1;
		size_t WindowSamples = 0;
	};

	// Explicit bounded capture, independent of the profiler's Play/panel/sampling gates.
	// Durations measure CPU wall time, including driver submission; never GPU execution.
	class FUNKE_API LoadingDiagnostics {
	public:
		static void SetEnabled(bool enabled);
		static bool IsEnabled();
		static void Reset();
		static void Record(const char* phase, double milliseconds, double budgetMs = -1, uint64_t units = 1);
		static std::vector<LoadingPhaseStatistics> Snapshot();
	};

	class FUNKE_API LoadingScope {
	public:
		explicit LoadingScope(const char* phase, double budgetMs = -1, uint64_t units = 1);
		~LoadingScope();
		LoadingScope(const LoadingScope&) = delete;
		LoadingScope& operator=(const LoadingScope&) = delete;
	private:
		const char* m_Phase;
		double m_Budget;
		uint64_t m_Units;
		std::chrono::steady_clock::time_point m_Start;
	};
}
