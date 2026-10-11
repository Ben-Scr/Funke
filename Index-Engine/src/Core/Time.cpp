#include "pch.hpp"
#include "Time.hpp"
#include <Math/Common.hpp>

namespace Index {
	Time* Time::s_Current = nullptr;

	Time* Time::TryGetCurrent() {
		return s_Current;
	}

	void Time::SetCurrent(Time* time) {
		s_Current = time;
	}

	float Time::GetDeltaTime() {
		const Time* time = TryGetCurrent();
		return time ? time->m_DeltaTime * time->m_TimeScale : 0.0f;
	}

	float Time::GetFrameRate() {
		const Time* time = TryGetCurrent();
		return time && time->m_DeltaTime > 0.0f ? 1.0f / time->m_DeltaTime : 0.0f;
	}

	float Time::GetDeltaTimeUnscaled() {
		const Time* time = TryGetCurrent();
		return time ? time->m_DeltaTime : 0.0f;
	}

	float Time::GetUnscaledDeltaTime() {
		return GetDeltaTimeUnscaled();
	}

	float Time::GetTargetFramerate() {
		const Time* time = TryGetCurrent();
		return time ? time->m_TargetFPS : 0.0f;
	}

	void Time::SetTargetFramerate(float framerate) {
		Time* time = TryGetCurrent();
		if (!time) return;
		time->m_TargetFPS = std::max(0.0f, framerate);
	}

	void Time::SetTimeScale(float scale) {
		Time* time = TryGetCurrent();
		// Upper bound keeps the fixed-step accumulator from demanding hundreds of substeps per frame. max-then-min also maps NaN to 0.
		constexpr float k_MaxTimeScale = 100.0f;
		if (time) time->m_TimeScale = std::min(std::max(0.0f, scale), k_MaxTimeScale);
	}

	void Time::SetFixedDeltaTime(float step) {
		Time* time = TryGetCurrent();
		if (!time) return;

		// 0 silently disables FixedUpdate (Application::Run zeroes the accumulator
		// and skips dispatch when fixedDt <= 0). Clamp to a sane minimum and warn
		// so misconfigured callers don't get mysteriously dead physics + scripts.
		constexpr float k_MinFixedStep = 1.f / 240.f;
		if (step <= 0.f) {
			IDX_CORE_WARN_TAG("Time", "SetFixedDeltaTime({}) is non-positive; clamping to {} ({} Hz). FixedUpdate would otherwise be disabled.", step, k_MinFixedStep, 1.f / k_MinFixedStep);
			step = k_MinFixedStep;
		}
		step = Clamp(step, k_MinFixedStep, 1.f);
		time->m_FixedDeltaTime = step;
	}

	float Time::GetFixedDeltaTime() {
		const Time* time = TryGetCurrent();
		return time ? time->m_FixedDeltaTime : 1.0f / 50.0f;
	}

	float Time::GetUnscaledFixedDeltaTime() {
		return GetFixedDeltaTime();
	}

	float Time::GetTimeScale() {
		const Time* time = TryGetCurrent();
		return time ? time->m_TimeScale : 1.0f;
	}

	float Time::GetElapsedTime() {
		const Time* time = TryGetCurrent();
		if (!time) return 0.0f;
		std::chrono::duration<float> elapsed = Clock::now() - time->m_StartTime;
		return elapsed.count();
	}

	float Time::GetSimulatedElapsedTime() {
		const Time* time = TryGetCurrent();
		return time ? time->m_SimulatedElapsedTime : 0.0f;
	}

	float Time::GetTimeSinceStartup() {
		const Time* time = TryGetCurrent();
		return time ? time->m_GameSimulatedElapsedTime : 0.0f;
	}

	float Time::GetRealtimeSinceStartup() {
		const Time* time = TryGetCurrent();
		if (!time || !time->m_GameStarted) return 0.0f;
		std::chrono::duration<float> elapsed = Clock::now() - time->m_GameStartTime;
		return elapsed.count();
	}

	int Time::GetFrameCount() {
		const Time* time = TryGetCurrent();
		return time ? time->m_FrameCount : 0;
	}

	void Time::MarkGameStart() {
		m_GameStartTime = Clock::now();
		m_GameSimulatedElapsedTime = 0.0f;
		m_GameStarted = true;
	}

	void Time::Update(float deltaTime) {
		m_DeltaTime = deltaTime;
		m_SimulatedElapsedTime += m_DeltaTime * m_TimeScale;
		if (m_GameStarted) {
			m_GameSimulatedElapsedTime += m_DeltaTime * m_TimeScale;
		}
	}
}
