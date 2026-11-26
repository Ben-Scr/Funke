#include "pch.hpp"
#include "Diagnostics/LoadingDiagnostics.hpp"
#include "Core/Log.hpp"
#include "Profiling/Profiler.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace Funke {
	namespace {
		constexpr size_t k_Window = 4096;
		struct Entry {
			LoadingPhaseStatistics Stats;
			std::array<double, k_Window> Samples{};
			size_t Next = 0;
			std::chrono::steady_clock::time_point LastWarning{};
		};
		std::atomic<bool> g_Enabled = false;
		std::mutex g_Mutex;
		std::unordered_map<std::string, Entry> g_Entries;
	}

	void LoadingDiagnostics::SetEnabled(bool enabled) { g_Enabled.store(enabled); }
	bool LoadingDiagnostics::IsEnabled() { return g_Enabled.load(std::memory_order_relaxed); }
	void LoadingDiagnostics::Reset() { std::scoped_lock lock(g_Mutex); g_Entries.clear(); }

	void LoadingDiagnostics::Record(const char* phase, double ms, double budget, uint64_t units) {
		if (!IsEnabled() || !phase || !std::isfinite(ms) || ms < 0) return;
		bool warn = false;
		{
			std::scoped_lock lock(g_Mutex);
			// Fixed production names; protect diagnostics from unbounded caller-generated labels.
			if (g_Entries.size() >= 64 && !g_Entries.contains(phase)) return;
			auto& entry = g_Entries[phase];
			auto& stats = entry.Stats;
			stats.Name = phase;
			++stats.Calls;
			stats.Units += units;
			stats.TotalMs += ms;
			stats.MaxMs = std::max(stats.MaxMs, ms);
			stats.LastBudgetMs = budget;
			entry.Samples[entry.Next++ % k_Window] = ms;
			stats.WindowSamples = std::min<size_t>(stats.Calls, k_Window);
			if (budget >= 0 && ms > budget) {
				++stats.OverBudget;
				const auto now = std::chrono::steady_clock::now();
				if (budget > 0 && now - entry.LastWarning >= std::chrono::seconds(1)) {
					entry.LastWarning = now;
					warn = true;
				}
			}
		}
		if (warn) FNK_CORE_WARN_TAG("Loading", "{} took {:.3f} ms (cooperative budget {:.3f} ms)", phase, ms, budget);
	}

	std::vector<LoadingPhaseStatistics> LoadingDiagnostics::Snapshot() {
		std::vector<LoadingPhaseStatistics> result;
		std::scoped_lock lock(g_Mutex);
		for (const auto& [name, entry] : g_Entries) {
			auto stats = entry.Stats;
			std::vector<double> sorted(entry.Samples.begin(), entry.Samples.begin() + stats.WindowSamples);
			std::sort(sorted.begin(), sorted.end());
			if (!sorted.empty()) {
				stats.P95Ms = sorted[static_cast<size_t>(std::ceil(sorted.size() * .95)) - 1];
				stats.P99Ms = sorted[static_cast<size_t>(std::ceil(sorted.size() * .99)) - 1];
			}
			result.push_back(std::move(stats));
		}
		std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.Name < b.Name; });
		return result;
	}

	LoadingScope::LoadingScope(const char* phase, double budget, uint64_t units)
		: m_Phase(LoadingDiagnostics::IsEnabled() || Profiler::IsArmed() ? phase : nullptr), m_Budget(budget), m_Units(units) {
		if (m_Phase) m_Start = std::chrono::steady_clock::now();
	}
	LoadingScope::~LoadingScope() {
		if (!m_Phase) return;
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_Start).count();
		Profiler::PushSample(m_Phase, static_cast<float>(ms));
		LoadingDiagnostics::Record(m_Phase, ms, m_Budget, m_Units);
	}
}
