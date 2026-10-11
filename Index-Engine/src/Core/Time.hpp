#pragma once
#include <chrono>
#include "Core/Export.hpp"

namespace Index {
	class Application;
	class ApplicationEditorAccess;

	class INDEX_API Time {
	public:
		static float GetDeltaTime();
		static float GetFrameRate();
		static float GetDeltaTimeUnscaled();
		static float GetUnscaledDeltaTime();
		static float GetTargetFramerate();
		static void SetTargetFramerate(float fps);

		// Always returns the unscaled fixed step. TimeScale changes step *frequency*
		// (more/fewer FixedUpdate calls per real second), not the per-call dt — so
		// integration done in FixedUpdate scales correctly without compounding.
		static float GetFixedDeltaTime();
		static void SetFixedDeltaTime(float step);

		static float GetUnscaledFixedDeltaTime();

		static float GetTimeScale();
		static void SetTimeScale(float scale);

		// Note: Realtime elapsed time
		static float GetElapsedTime();
		// Note: Elapsed time based on timescale
		static float GetSimulatedElapsedTime();

		// Time since the game started (engine init excluded). Scales with TimeScale —
		// at 2x scale the value advances twice as fast as wallclock. Reset by
		// MarkGameStart(); zero before the first call.
		static float GetTimeSinceStartup();

		// Time since the game started (engine init excluded). Wallclock — ignores
		// TimeScale and pause. Reset by MarkGameStart(); zero before the first call.
		static float GetRealtimeSinceStartup();

		static int GetFrameCount();

	private:
		using Clock = std::chrono::steady_clock;

		Time() = default;
		Time(const Time&) = delete;
		Time& operator=(const Time&) = delete;

		static Time* TryGetCurrent();
		static void SetCurrent(Time* time);

		void Update(float deltaTime);
		void AdvanceFrameCount() { m_FrameCount++; }

		// Called after RaiseApplicationStart (so engine-init is excluded) and on each editor play-mode entry.
		void MarkGameStart();

		float m_DeltaTime = 0.0f;
		float m_TargetFPS = 144.f;
		float m_TimeScale = 1.f;
		float m_FixedDeltaTime = 1.0f / 50.f;
		float m_SimulatedElapsedTime = 0.0f;
		float m_GameSimulatedElapsedTime = 0.0f;
		bool  m_GameStarted = false;
		int m_FrameCount = 0;

		Clock::time_point m_StartTime = Clock::now();
		Clock::time_point m_GameStartTime = Clock::now();

		static Time* s_Current;

		friend class Application;
		friend class ApplicationEditorAccess;
	};
}
