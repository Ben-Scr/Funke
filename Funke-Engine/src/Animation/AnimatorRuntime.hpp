#pragma once
#include "Animation/AnimationBinding.hpp"
#include "Animation/AnimationClip.hpp"
#include "Animation/AnimatorController.hpp"
#include "Animation/AnimatorStateMachine.hpp"
#include "Core/Export.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Funke {
	class Scene;
	struct AnimatorComponent;

	// Playback state of one Animator: loaded assets, state machine and resolved track targets.
	class FUNKE_API AnimatorRuntime {
	public:
		// Picks up the component's assets (first use, GUID change, hot reload). False when nothing can play.
		bool Sync(const AnimatorComponent& component);

		// Advances by dt (already scaled by the component speed) and writes the animated values.
		void Update(Scene& scene, Entity owner, float dt, bool fireEvents);
		// Writes the pose of the current state without advancing (after Play from script, editor preview).
		void Apply(Scene& scene, Entity owner);
		// Writes one state's clip at clipTime (clamped to the clip, no looping): the Animation window's scrubbing.
		void ApplyStatePose(Scene& scene, Entity owner, int state, float clipTime);
		// Resolves track targets if needed (first use, asset change, hierarchy change); Apply does this itself.
		void EnsureBindings(Scene& scene, Entity owner);
		const AnimationTargetSet& GetTargets() const { return m_Targets; }
		const AnimationClip* GetStateClip(int state) const;

		AnimatorStateMachine& GetStateMachine() { return m_Machine; }
		const AnimatorStateMachine& GetStateMachine() const { return m_Machine; }
		const AnimatorController* GetController() const { return m_Controller.get(); }
		std::span<const AnimatorStateClipInfo> GetClipInfos() const { return m_ClipInfos; }

		int FindState(const std::string& name) const;
		std::string GetCurrentStateName() const;

		// Parameter access that logs an unknown name once instead of failing silently.
		void SetFloat(const std::string& name, float value);
		void SetInt(const std::string& name, int value);
		void SetBool(const std::string& name, bool value);
		void SetTrigger(const std::string& name);
		void ResetTrigger(const std::string& name);
		bool Play(const std::string& state, float normalizedTime);
		bool CrossFade(const std::string& state, float duration);

		// Track problems of the loaded clips ("Arm/Hand: no child entity ..."), for the editor.
		const std::vector<std::string>& GetBindingErrors() const { return m_BindingErrors; }
		void InvalidateBindings() { m_BindingsDirty = true; }

	private:
		struct TrackBinding {
			int Target = -1;
			int Channel = -1;
		};

		struct Accumulator {
			std::array<float, 4> Sum{};
			std::array<float, 4> Weight{};
			uint8_t ChannelMask = 0;
			float DiscreteWeight = -1.0f;
			const std::string* Discrete = nullptr;
		};

		void Rebind(Scene& scene, Entity owner);
		void WritePose(Scene& scene, const std::vector<AnimatorPlaybackSample>& samples);
		void FireEvents(Scene& scene, Entity owner);
		void WarnOnce(const std::string& message);
		void WarnParameter(const std::string& name, const char* type);

		uint64_t m_ControllerGuid = 0;
		uint64_t m_ClipGuid = 0;
		std::shared_ptr<const AnimatorController> m_Controller;
		std::vector<std::shared_ptr<const AnimationClip>> m_StateClips;
		std::vector<AnimatorStateClipInfo> m_ClipInfos;
		AnimatorStateMachine m_Machine;

		AnimationTargetSet m_Targets;
		std::vector<std::vector<TrackBinding>> m_Bindings;
		std::vector<std::string> m_BindingErrors;
		bool m_BindingsDirty = true;
		uint64_t m_RegistryGeneration = 0;
		EntityHandle m_BoundOwner = entt::null;

		std::vector<AnimatorPlaybackSample> m_Samples;
		std::vector<Accumulator> m_Accumulators;
		std::unordered_set<std::string> m_Warned;
		// Script class -> method name -> argument kind, from the managed side's invokable list.
		std::unordered_map<std::string, std::unordered_map<std::string, uint8_t>> m_EventMethods;
	};

}
