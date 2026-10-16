#include "pch.hpp"
#include "Animation/AnimatorStateMachine.hpp"

#include "Animation/AnimationClip.hpp"

#include <algorithm>
#include <cmath>

namespace Index {
	namespace {
		constexpr float k_ConditionEpsilon = 1e-5f;
		// Bounds the work of a hitch or a very short looping clip: events of at most this many cycles fire per frame.
		constexpr int k_MaxEventCyclesPerWindow = 4;

		const AnimatorStateClipInfo& InfoFor(std::span<const AnimatorStateClipInfo> clips, int state) {
			static const AnimatorStateClipInfo s_Empty{};
			return state >= 0 && state < static_cast<int>(clips.size()) ? clips[static_cast<size_t>(state)] : s_Empty;
		}

		float NormalizedLength(const AnimatorStateClipInfo& info) {
			return info.Length > 0.0f ? info.Length : 1.0f;
		}

		float StateSpeed(const AnimatorController& controller, int state) {
			return state >= 0 && state < static_cast<int>(controller.States.size()) ? controller.States[static_cast<size_t>(state)].Speed : 1.0f;
		}
	}

	void AnimatorStateMachine::Reset(const AnimatorController& controller) {
		m_Parameters.clear();
		m_Parameters.reserve(controller.Parameters.size());
		for (const AnimatorParameter& p : controller.Parameters) {
			ParameterValue v;
			v.Name = p.Name;
			v.Type = p.Type;
			v.Float = p.DefaultFloat;
			v.Int = p.DefaultInt;
			v.Bool = p.Type == AnimatorParameterType::Trigger ? false : p.DefaultBool;
			m_Parameters.push_back(std::move(v));
		}
		m_InTransition = false;
		m_Next = {};
		m_Windows.clear();
		const int state = controller.States.empty() ? -1 : std::clamp(controller.DefaultState, 0, static_cast<int>(controller.States.size()) - 1);
		EnterState(state, 0.0f);
		m_Initialized = true;
	}

	void AnimatorStateMachine::Rebind(const AnimatorController& previous, const AnimatorController& controller) {
		std::vector<ParameterValue> old = std::move(m_Parameters);
		const std::string currentName = m_Current.State >= 0 && m_Current.State < static_cast<int>(previous.States.size())
			? previous.States[static_cast<size_t>(m_Current.State)].Name : std::string{};
		const float currentTime = m_Current.Time;

		Reset(controller);
		for (ParameterValue& p : m_Parameters) {
			for (const ParameterValue& o : old) {
				if (o.Name == p.Name && o.Type == p.Type) {
					p.Float = o.Float;
					p.Int = o.Int;
					p.Bool = o.Bool;
				}
			}
		}
		if (const int state = controller.FindState(currentName); state >= 0) {
			m_Current.State = state;
			m_Current.Time = currentTime;
			m_CurrentJustEntered = false;
		}
	}

	int AnimatorStateMachine::FindParameter(const std::string& name) const {
		for (size_t i = 0; i < m_Parameters.size(); ++i) {
			if (m_Parameters[i].Name == name) return static_cast<int>(i);
		}
		return -1;
	}

	bool AnimatorStateMachine::SetFloat(const std::string& name, float value) {
		const int i = FindParameter(name);
		if (i < 0 || m_Parameters[static_cast<size_t>(i)].Type != AnimatorParameterType::Float) return false;
		m_Parameters[static_cast<size_t>(i)].Float = value;
		return true;
	}

	bool AnimatorStateMachine::SetInt(const std::string& name, int value) {
		const int i = FindParameter(name);
		if (i < 0 || m_Parameters[static_cast<size_t>(i)].Type != AnimatorParameterType::Int) return false;
		m_Parameters[static_cast<size_t>(i)].Int = value;
		return true;
	}

	bool AnimatorStateMachine::SetBool(const std::string& name, bool value) {
		const int i = FindParameter(name);
		if (i < 0 || m_Parameters[static_cast<size_t>(i)].Type != AnimatorParameterType::Bool) return false;
		m_Parameters[static_cast<size_t>(i)].Bool = value;
		return true;
	}

	bool AnimatorStateMachine::SetTrigger(const std::string& name) {
		const int i = FindParameter(name);
		if (i < 0 || m_Parameters[static_cast<size_t>(i)].Type != AnimatorParameterType::Trigger) return false;
		m_Parameters[static_cast<size_t>(i)].Bool = true;
		return true;
	}

	bool AnimatorStateMachine::ResetTrigger(const std::string& name) {
		const int i = FindParameter(name);
		if (i < 0 || m_Parameters[static_cast<size_t>(i)].Type != AnimatorParameterType::Trigger) return false;
		m_Parameters[static_cast<size_t>(i)].Bool = false;
		return true;
	}

	float AnimatorStateMachine::GetFloat(const std::string& name) const {
		const int i = FindParameter(name);
		return i >= 0 ? m_Parameters[static_cast<size_t>(i)].Float : 0.0f;
	}

	int AnimatorStateMachine::GetInt(const std::string& name) const {
		const int i = FindParameter(name);
		return i >= 0 ? m_Parameters[static_cast<size_t>(i)].Int : 0;
	}

	bool AnimatorStateMachine::GetBool(const std::string& name) const {
		const int i = FindParameter(name);
		return i >= 0 ? m_Parameters[static_cast<size_t>(i)].Bool : false;
	}

	bool AnimatorStateMachine::ConditionHolds(const AnimatorCondition& condition) const {
		const int i = FindParameter(condition.Parameter);
		if (i < 0) return false;
		const ParameterValue& p = m_Parameters[static_cast<size_t>(i)];
		float value = 0.0f;
		switch (p.Type) {
		case AnimatorParameterType::Float: value = p.Float; break;
		case AnimatorParameterType::Int: value = static_cast<float>(p.Int); break;
		case AnimatorParameterType::Bool:
		case AnimatorParameterType::Trigger: value = p.Bool ? 1.0f : 0.0f; break;
		}
		switch (condition.Mode) {
		case AnimatorConditionMode::Greater: return value > condition.Threshold;
		case AnimatorConditionMode::Less: return value < condition.Threshold;
		case AnimatorConditionMode::Equals: return std::abs(value - condition.Threshold) <= k_ConditionEpsilon;
		case AnimatorConditionMode::NotEquals: return std::abs(value - condition.Threshold) > k_ConditionEpsilon;
		case AnimatorConditionMode::If: return value != 0.0f;
		case AnimatorConditionMode::IfNot: return value == 0.0f;
		}
		return false;
	}

	bool AnimatorStateMachine::TransitionReady(const AnimatorTransition& transition, std::span<const AnimatorStateClipInfo> clips, float previousTime) const {
		if (transition.HasExitTime) {
			const AnimatorStateClipInfo& info = InfoFor(clips, m_Current.State);
			const float length = NormalizedLength(info);
			const float previous = previousTime / length;
			const float current = m_Current.Time / length;
			const float exit = transition.ExitTime;
			// Looping states leave at the exit point of whichever cycle crosses it; others once it is reached.
			if (info.Loop && exit < 1.0f) {
				if (!(std::floor(current - exit) > std::floor(previous - exit))) return false;
			}
			else if (current < exit) {
				return false;
			}
		}
		for (const AnimatorCondition& condition : transition.Conditions) {
			if (!ConditionHolds(condition)) return false;
		}
		return true;
	}

	void AnimatorStateMachine::EnterState(int state, float time) {
		m_Current.State = state;
		m_Current.Time = time;
		m_CurrentJustEntered = true;
	}

	void AnimatorStateMachine::StartTransition(const AnimatorController& controller, const AnimatorTransition& transition) {
		for (const AnimatorCondition& condition : transition.Conditions) {
			const int i = FindParameter(condition.Parameter);
			if (i >= 0 && m_Parameters[static_cast<size_t>(i)].Type == AnimatorParameterType::Trigger) m_Parameters[static_cast<size_t>(i)].Bool = false;
		}
		(void)controller;
		if (transition.Duration <= 0.0f) {
			m_InTransition = false;
			EnterState(transition.To, 0.0f);
			return;
		}
		m_InTransition = true;
		m_Next.State = transition.To;
		m_Next.Time = 0.0f;
		m_NextJustEntered = true;
		m_TransitionElapsed = 0.0f;
		m_TransitionDuration = transition.Duration;
	}

	void AnimatorStateMachine::Update(const AnimatorController& controller, std::span<const AnimatorStateClipInfo> clips, float dt) {
		if (!m_Initialized) Reset(controller);
		m_Windows.clear();
		if (m_Current.State < 0) return;

		const float currentFrom = m_Current.Time;
		m_Current.Time += dt * StateSpeed(controller, m_Current.State);
		m_Windows.push_back({ m_Current.State, currentFrom, m_Current.Time, m_CurrentJustEntered });
		m_CurrentJustEntered = false;

		if (m_InTransition) {
			const float nextFrom = m_Next.Time;
			m_Next.Time += dt * StateSpeed(controller, m_Next.State);
			m_Windows.push_back({ m_Next.State, nextFrom, m_Next.Time, m_NextJustEntered });
			m_NextJustEntered = false;
			m_TransitionElapsed += dt;
			if (m_TransitionElapsed >= m_TransitionDuration) {
				m_Current = m_Next;
				m_CurrentJustEntered = false;
				m_InTransition = false;
				m_Next = {};
			}
			return;
		}

		// Any State transitions win over the current state's own, as in Unity.
		for (int pass = 0; pass < 2; ++pass) {
			for (const AnimatorTransition& transition : controller.Transitions) {
				const bool fromAny = transition.From == AnimatorTransition::k_AnyState;
				if (pass == 0 ? !fromAny : transition.From != m_Current.State) continue;
				if (transition.To < 0 || transition.To >= static_cast<int>(controller.States.size())) continue;
				if (fromAny && transition.To == m_Current.State && !transition.CanTransitionToSelf) continue;
				if (!TransitionReady(transition, clips, currentFrom)) continue;
				StartTransition(controller, transition);
				return;
			}
		}
	}

	bool AnimatorStateMachine::Play(int state, float normalizedTime, std::span<const AnimatorStateClipInfo> clips) {
		if (state < 0 || state >= static_cast<int>(clips.size())) return false;
		m_InTransition = false;
		m_Next = {};
		EnterState(state, normalizedTime * NormalizedLength(clips[static_cast<size_t>(state)]));
		return true;
	}

	bool AnimatorStateMachine::CrossFade(int state, float duration) {
		if (state < 0 || m_Current.State < 0) return false;
		AnimatorTransition transition;
		transition.To = state;
		transition.Duration = duration;
		StartTransition(AnimatorController{}, transition);
		return true;
	}

	float AnimatorStateMachine::GetTransitionProgress() const {
		if (!m_InTransition || m_TransitionDuration <= 0.0f) return 0.0f;
		return std::clamp(m_TransitionElapsed / m_TransitionDuration, 0.0f, 1.0f);
	}

	float AnimatorStateMachine::GetCurrentNormalizedTime(std::span<const AnimatorStateClipInfo> clips) const {
		if (m_Current.State < 0) return 0.0f;
		return m_Current.Time / NormalizedLength(InfoFor(clips, m_Current.State));
	}

	float AnimatorStateMachine::ToClipTime(float stateTime, const AnimatorStateClipInfo& info) {
		if (info.Length <= 0.0f) return 0.0f;
		if (info.Loop) {
			const float t = std::fmod(stateTime, info.Length);
			return t < 0.0f ? t + info.Length : t;
		}
		return std::clamp(stateTime, 0.0f, info.Length);
	}

	void AnimatorStateMachine::GetSamples(std::span<const AnimatorStateClipInfo> clips, std::vector<AnimatorPlaybackSample>& out) const {
		out.clear();
		if (m_Current.State < 0) return;
		const float progress = GetTransitionProgress();
		out.push_back({ m_Current.State, ToClipTime(m_Current.Time, InfoFor(clips, m_Current.State)), m_InTransition ? 1.0f - progress : 1.0f });
		if (m_InTransition) out.push_back({ m_Next.State, ToClipTime(m_Next.Time, InfoFor(clips, m_Next.State)), progress });
	}

	void AnimatorStateMachine::ForEachEventInWindow(const AnimationClip& clip, bool loop, const AnimatorPlaybackWindow& window,
		const std::function<void(const AnimationEvent&)>& fn) {
		if (clip.Events.empty() || window.To <= window.From && !window.IncludesStart) return;
		const float length = clip.Length;

		auto fireRange = [&](float lo, float hi, bool includeLo) {
			for (const AnimationEvent& e : clip.Events) {
				if (e.Time > hi) break;
				if (e.Time > lo || (includeLo && e.Time >= lo)) fn(e);
			}
		};

		if (!loop || length <= 0.0f) {
			const float lo = std::clamp(window.From, 0.0f, std::max(length, 0.0f));
			const float hi = length > 0.0f ? std::clamp(window.To, 0.0f, length) : window.To;
			if (hi > lo || window.IncludesStart) fireRange(lo, hi, window.IncludesStart);
			return;
		}

		const int firstCycle = static_cast<int>(std::floor(window.From / length));
		const int lastCycle = std::min(static_cast<int>(std::floor(window.To / length)), firstCycle + k_MaxEventCyclesPerWindow - 1);
		for (int cycle = firstCycle; cycle <= lastCycle; ++cycle) {
			const float cycleStart = static_cast<float>(cycle) * length;
			const float lo = std::max(window.From, cycleStart) - cycleStart;
			const float hi = std::min(window.To, cycleStart + length) - cycleStart;
			// A cycle boundary inside the window counts as a fresh start, so events at time 0 fire on every loop.
			const bool includeLo = cycle > firstCycle || window.IncludesStart;
			if (hi > lo || includeLo) fireRange(lo, hi, includeLo);
		}
	}

}
