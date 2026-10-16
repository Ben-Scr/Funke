#pragma once
#include "Animation/AnimatorController.hpp"
#include "Core/Export.hpp"

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace Funke {
	struct AnimationClip;
	struct AnimationEvent;

	// What the state machine needs to know about each controller state's clip.
	struct AnimatorStateClipInfo {
		// Seconds; 0 for empty states (their normalized time then runs at one unit per second).
		float Length = 0.0f;
		bool Loop = true;
	};

	// One state contributing to the pose this frame.
	struct AnimatorPlaybackSample {
		int State = -1;
		// Seconds into the clip after looping/clamping.
		float ClipTime = 0.0f;
		float Weight = 1.0f;
	};

	// Playback time a state covered during the last Update, in unwrapped state seconds; drives animation events.
	struct AnimatorPlaybackWindow {
		int State = -1;
		float From = 0.0f;
		float To = 0.0f;
		// True on the frame the state was entered, so events at time 0 fire once.
		bool IncludesStart = false;
	};

	// Parameters, current state and crossfade of one Animator, independent of any scene (unit-testable).
	class FUNKE_API AnimatorStateMachine {
	public:
		// Parameters back to their defaults, default state from time 0.
		void Reset(const AnimatorController& controller);
		// After a controller hot reload: keeps parameter values and the current state where names still match.
		void Rebind(const AnimatorController& previous, const AnimatorController& controller);
		bool IsInitialized() const { return m_Initialized; }

		// False (and no change) if the parameter does not exist or has another type.
		bool SetFloat(const std::string& name, float value);
		bool SetInt(const std::string& name, int value);
		bool SetBool(const std::string& name, bool value);
		bool SetTrigger(const std::string& name);
		bool ResetTrigger(const std::string& name);
		float GetFloat(const std::string& name) const;
		int GetInt(const std::string& name) const;
		bool GetBool(const std::string& name) const;
		bool HasParameter(const std::string& name) const { return FindParameter(name) >= 0; }

		// Advances the playing states by dt (seconds, before state speed) and fires at most one transition.
		void Update(const AnimatorController& controller, std::span<const AnimatorStateClipInfo> clips, float dt);

		// Jumps to state without blending. normalizedTime 0..1 = start..end of its clip.
		bool Play(int state, float normalizedTime, std::span<const AnimatorStateClipInfo> clips);
		// Blends from the current pose to state over duration seconds.
		bool CrossFade(int state, float duration);

		int GetCurrentState() const { return m_Current.State; }
		// Target of the running crossfade; -1 when none.
		int GetNextState() const { return m_InTransition ? m_Next.State : -1; }
		bool IsInTransition() const { return m_InTransition; }
		float GetTransitionProgress() const;
		float GetCurrentNormalizedTime(std::span<const AnimatorStateClipInfo> clips) const;

		// Current (and crossfade target) state with clip time and blend weight.
		void GetSamples(std::span<const AnimatorStateClipInfo> clips, std::vector<AnimatorPlaybackSample>& out) const;
		const std::vector<AnimatorPlaybackWindow>& GetLastWindows() const { return m_Windows; }

		// Calls fn for every event of clip whose time lies in the window (loops unrolled, at most a few cycles per call).
		static void ForEachEventInWindow(const AnimationClip& clip, bool loop, const AnimatorPlaybackWindow& window,
			const std::function<void(const AnimationEvent&)>& fn);
		static float ToClipTime(float stateTime, const AnimatorStateClipInfo& info);

	private:
		struct ParameterValue {
			std::string Name;
			AnimatorParameterType Type = AnimatorParameterType::Float;
			float Float = 0.0f;
			int Int = 0;
			bool Bool = false;
		};

		struct Playback {
			int State = -1;
			float Time = 0.0f;
		};

		int FindParameter(const std::string& name) const;
		bool ConditionHolds(const AnimatorCondition& condition) const;
		bool TransitionReady(const AnimatorTransition& transition, std::span<const AnimatorStateClipInfo> clips, float previousTime) const;
		void StartTransition(const AnimatorController& controller, const AnimatorTransition& transition);
		void EnterState(int state, float time);

		std::vector<ParameterValue> m_Parameters;
		Playback m_Current;
		Playback m_Next;
		bool m_InTransition = false;
		float m_TransitionElapsed = 0.0f;
		float m_TransitionDuration = 0.0f;
		bool m_CurrentJustEntered = false;
		bool m_NextJustEntered = false;
		bool m_Initialized = false;
		std::vector<AnimatorPlaybackWindow> m_Windows;
	};

}
