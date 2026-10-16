#include "pch.hpp"
#include "Animation/AnimatorRuntime.hpp"

#include "Components/General/AnimatorComponent.hpp"
#include "Components/General/NameComponent.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/InspectorEventDispatch.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"

#include <algorithm>
#include <charconv>

namespace Funke {
	namespace {
		const std::string* DiscreteValueAt(const AnimationTrack& track, float time) {
			const auto& keys = track.DiscreteKeys;
			if (keys.empty()) return nullptr;
			const auto next = std::upper_bound(keys.begin(), keys.end(), time,
				[](float t, const DiscreteKeyframe& key) { return t < key.Time; });
			return next == keys.begin() ? &keys.front().Value : &(next - 1)->Value;
		}

		bool StateLoops(const AnimatorState& state, const AnimationClip* clip) {
			switch (state.Loop) {
			case AnimatorStateLoop::Loop: return true;
			case AnimatorStateLoop::Once: return false;
			case AnimatorStateLoop::FromClip:
			default: return clip ? clip->Loop : true;
			}
		}
	}

	AnimatorRuntime& AnimatorRuntimeHandle::GetOrCreate() {
		if (!m_Runtime) m_Runtime = std::make_shared<AnimatorRuntime>();
		return *m_Runtime;
	}

	bool AnimatorRuntime::Sync(const AnimatorComponent& component) {
		std::shared_ptr<const AnimatorController> controller;
		if (component.Controller != 0) {
			controller = AnimatorControllerLibrary::Get(component.Controller);
		}
		else if (component.Clip != 0) {
			// A lone clip plays as a one-state controller, so both paths share playback and events.
			if (!m_Controller || m_ControllerGuid != 0 || m_ClipGuid != component.Clip) {
				AnimatorController single;
				AnimatorState state;
				state.Name = "Default";
				state.Clip = component.Clip;
				single.States.push_back(state);
				controller = std::make_shared<const AnimatorController>(std::move(single));
			}
			else {
				controller = m_Controller;
			}
		}

		if (controller != m_Controller) {
			if (m_Controller && controller && m_ControllerGuid == component.Controller && m_ClipGuid == component.Clip) {
				m_Machine.Rebind(*m_Controller, *controller);
			}
			else if (controller) {
				m_Machine.Reset(*controller);
			}
			m_Controller = controller;
			m_ControllerGuid = component.Controller;
			m_ClipGuid = component.Clip;
			m_BindingsDirty = true;
		}
		if (!m_Controller) {
			m_StateClips.clear();
			m_ClipInfos.clear();
			return false;
		}

		const size_t stateCount = m_Controller->States.size();
		if (m_StateClips.size() != stateCount) {
			m_StateClips.assign(stateCount, nullptr);
			m_BindingsDirty = true;
		}
		m_ClipInfos.resize(stateCount);
		for (size_t i = 0; i < stateCount; ++i) {
			const AnimatorState& state = m_Controller->States[i];
			std::shared_ptr<const AnimationClip> clip = AnimationClipLibrary::Get(state.Clip);
			if (clip != m_StateClips[i]) {
				m_StateClips[i] = std::move(clip);
				m_BindingsDirty = true;
			}
			const AnimationClip* loaded = m_StateClips[i].get();
			m_ClipInfos[i].Length = loaded ? loaded->Length : 0.0f;
			m_ClipInfos[i].Loop = StateLoops(state, loaded);
		}
		return stateCount > 0;
	}

	void AnimatorRuntime::Rebind(Scene& scene, Entity owner) {
		m_Targets.Clear();
		m_Bindings.assign(m_StateClips.size(), {});
		m_BindingErrors.clear();
		for (size_t s = 0; s < m_StateClips.size(); ++s) {
			const AnimationClip* clip = m_StateClips[s].get();
			if (!clip) continue;
			m_Bindings[s].resize(clip->Tracks.size());
			for (size_t t = 0; t < clip->Tracks.size(); ++t) {
				const AnimationTrack& track = clip->Tracks[t];
				std::string error;
				int channel = -1;
				const int target = m_Targets.Resolve(scene, owner, track, channel, error);
				if (target < 0) {
					const std::string where = (track.Path.empty() ? std::string{} : track.Path + ": ") + track.Component + "." + track.Property;
					m_BindingErrors.push_back(where + " - " + error);
					WarnOnce("Animator on '" + owner.GetName() + "' skips track " + where + " (" + error + ")");
					continue;
				}
				const PropertyType type = m_Targets.GetType(target);
				if (!track.IsDiscrete() && !IsCurveAnimatable(type)) {
					const std::string where = track.Component + "." + track.Property;
					m_BindingErrors.push_back(where + " - needs discrete keys");
					WarnOnce("Animator on '" + owner.GetName() + "' skips curve track " + where + " (property only takes discrete keys)");
					continue;
				}
				m_Bindings[s][t] = { target, std::max(channel, 0) };
			}
		}
		m_Accumulators.assign(m_Targets.GetTargets().size(), {});
		m_RegistryGeneration = SceneManager::GetComponentRegistryOrBuiltIn().GetGeneration();
		m_BoundOwner = owner.GetHandle();
		m_BindingsDirty = false;
	}

	void AnimatorRuntime::Update(Scene& scene, Entity owner, float dt, bool fireEvents) {
		if (!m_Controller) return;
		m_Machine.Update(*m_Controller, m_ClipInfos, dt);
		Apply(scene, owner);
		if (fireEvents) FireEvents(scene, owner);
	}

	void AnimatorRuntime::EnsureBindings(Scene& scene, Entity owner) {
		if (m_BindingsDirty || m_BoundOwner != owner.GetHandle()
			|| m_RegistryGeneration != SceneManager::GetComponentRegistryOrBuiltIn().GetGeneration()) {
			Rebind(scene, owner);
		}
	}

	void AnimatorRuntime::Apply(Scene& scene, Entity owner) {
		if (!m_Controller) return;
		if (!m_Machine.IsInitialized()) m_Machine.Reset(*m_Controller);
		EnsureBindings(scene, owner);
		m_Machine.GetSamples(m_ClipInfos, m_Samples);
		WritePose(scene, m_Samples);
	}

	void AnimatorRuntime::ApplyStatePose(Scene& scene, Entity owner, int state, float clipTime) {
		if (!m_Controller || state < 0 || state >= static_cast<int>(m_StateClips.size())) return;
		EnsureBindings(scene, owner);
		const AnimationClip* clip = m_StateClips[static_cast<size_t>(state)].get();
		const float length = clip ? clip->Length : 0.0f;
		m_Samples.assign(1, { state, std::clamp(clipTime, 0.0f, std::max(length, 0.0f)), 1.0f });
		WritePose(scene, m_Samples);
	}

	const AnimationClip* AnimatorRuntime::GetStateClip(int state) const {
		return state >= 0 && state < static_cast<int>(m_StateClips.size()) ? m_StateClips[static_cast<size_t>(state)].get() : nullptr;
	}

	void AnimatorRuntime::WritePose(Scene& scene, const std::vector<AnimatorPlaybackSample>& samples) {
		for (Accumulator& acc : m_Accumulators) acc = {};

		for (const AnimatorPlaybackSample& sample : samples) {
			if (sample.State < 0 || sample.State >= static_cast<int>(m_StateClips.size())) continue;
			const AnimationClip* clip = m_StateClips[static_cast<size_t>(sample.State)].get();
			if (!clip || sample.Weight <= 0.0f) continue;
			const auto& bindings = m_Bindings[static_cast<size_t>(sample.State)];
			for (size_t t = 0; t < clip->Tracks.size() && t < bindings.size(); ++t) {
				const TrackBinding& binding = bindings[t];
				if (binding.Target < 0) continue;
				Accumulator& acc = m_Accumulators[static_cast<size_t>(binding.Target)];
				const AnimationTrack& track = clip->Tracks[t];
				if (track.IsDiscrete()) {
					// Discrete values cannot blend; the heavier state wins.
					if (sample.Weight > acc.DiscreteWeight) {
						acc.DiscreteWeight = sample.Weight;
						acc.Discrete = DiscreteValueAt(track, sample.ClipTime);
					}
					continue;
				}
				const int channel = std::clamp(binding.Channel, 0, 3);
				acc.Sum[static_cast<size_t>(channel)] += track.Curve.Evaluate(sample.ClipTime) * sample.Weight;
				acc.Weight[static_cast<size_t>(channel)] += sample.Weight;
				acc.ChannelMask |= static_cast<uint8_t>(1u << channel);
			}
		}

		for (size_t i = 0; i < m_Accumulators.size(); ++i) {
			const Accumulator& acc = m_Accumulators[i];
			if (acc.ChannelMask == 0 && !acc.Discrete) continue;
			const int target = static_cast<int>(i);
			PropertyValue value;
			if (!m_Targets.Read(scene, target, value)) {
				m_BindingsDirty = true;
				continue;
			}
			if (acc.Discrete) {
				PropertyValue parsed = PropertyValue::FromString(m_Targets.GetType(target), *acc.Discrete);
				parsed.Type = m_Targets.GetType(target);
				// Re-setting an unchanged key would rerun setter side effects every frame (texture load, .meta read).
				if (acc.ChannelMask == 0 && parsed == value) continue;
				value = std::move(parsed);
			}
			for (int channel = 0; channel < 4; ++channel) {
				if (!(acc.ChannelMask & (1u << channel))) continue;
				const float weight = acc.Weight[static_cast<size_t>(channel)];
				float result = acc.Sum[static_cast<size_t>(channel)];
				// A property only one side of a crossfade animates blends against its present value.
				if (weight < 1.0f) result += (1.0f - weight) * ReadAnimationChannel(value, channel);
				WriteAnimationChannel(value, channel, result);
			}
			if (!m_Targets.Write(scene, target, value)) m_BindingsDirty = true;
		}
	}

	void AnimatorRuntime::FireEvents(Scene& scene, Entity owner) {
		const auto& windows = m_Machine.GetLastWindows();
		if (windows.empty()) return;
		auto& registry = scene.GetRegistry();
		const EntityHandle handle = owner.GetHandle();

		for (const AnimatorPlaybackWindow& window : windows) {
			if (window.State < 0 || window.State >= static_cast<int>(m_StateClips.size())) continue;
			const AnimationClip* clip = m_StateClips[static_cast<size_t>(window.State)].get();
			if (!clip || clip->Events.empty()) continue;
			const bool loop = m_ClipInfos[static_cast<size_t>(window.State)].Loop;

			AnimatorStateMachine::ForEachEventInWindow(*clip, loop, window, [&](const AnimationEvent& e) {
				const auto& callbacks = ScriptEngine::GetCallbacks();
				if (!callbacks.InvokeScriptMethodByName) return;
				ScriptComponent* scripts = registry.valid(handle) ? registry.try_get<ScriptComponent>(handle) : nullptr;
				if (!scripts) return;
				bool handled = false;
				// Index loop: the event may add or remove scripts.
				for (size_t i = 0; i < scripts->Scripts.size(); ++i) {
					const ScriptInstance& script = scripts->Scripts[i];
					if (!script.HasManagedInstance() || !script.HasEnabled()) continue;
					const std::string className = script.GetClassName();
					const int32_t gcHandle = static_cast<int32_t>(script.GetGCHandle());

					auto [it, inserted] = m_EventMethods.try_emplace(className);
					if (inserted) {
						for (const std::string& entry : InspectorEvents::GetInvokableMethods(className)) {
							const size_t colon = entry.rfind(':');
							if (colon == std::string::npos) continue;
							int kind = 0;
							std::from_chars(entry.data() + colon + 1, entry.data() + entry.size(), kind);
							it->second[entry.substr(0, colon)] = static_cast<uint8_t>(kind);
						}
					}
					const auto method = it->second.find(e.Function);
					if (method == it->second.end()) continue;
					callbacks.InvokeScriptMethodByName(gcHandle, e.Function.c_str(), method->second, e.Argument.c_str());
					handled = true;
					scripts = registry.valid(handle) ? registry.try_get<ScriptComponent>(handle) : nullptr;
					if (!scripts) return;
				}
				if (!handled) WarnOnce("Animation event '" + e.Function + "' on '" + owner.GetName() + "' found no public method on its scripts");
			});
		}
	}

	int AnimatorRuntime::FindState(const std::string& name) const {
		return m_Controller ? m_Controller->FindState(name) : -1;
	}

	std::string AnimatorRuntime::GetCurrentStateName() const {
		const int state = m_Machine.GetCurrentState();
		if (!m_Controller || state < 0 || state >= static_cast<int>(m_Controller->States.size())) return {};
		return m_Controller->States[static_cast<size_t>(state)].Name;
	}

	void AnimatorRuntime::WarnOnce(const std::string& message) {
		if (m_Warned.insert(message).second) FNK_CORE_WARN_TAG("Animator", "{}", message);
	}

	void AnimatorRuntime::WarnParameter(const std::string& name, const char* type) {
		WarnOnce(std::string("Animator has no ") + type + " parameter '" + name + "'");
	}

	void AnimatorRuntime::SetFloat(const std::string& name, float value) {
		if (!m_Machine.SetFloat(name, value)) WarnParameter(name, "Float");
	}

	void AnimatorRuntime::SetInt(const std::string& name, int value) {
		if (!m_Machine.SetInt(name, value)) WarnParameter(name, "Int");
	}

	void AnimatorRuntime::SetBool(const std::string& name, bool value) {
		if (!m_Machine.SetBool(name, value)) WarnParameter(name, "Bool");
	}

	void AnimatorRuntime::SetTrigger(const std::string& name) {
		if (!m_Machine.SetTrigger(name)) WarnParameter(name, "Trigger");
	}

	void AnimatorRuntime::ResetTrigger(const std::string& name) {
		if (!m_Machine.ResetTrigger(name)) WarnParameter(name, "Trigger");
	}

	bool AnimatorRuntime::Play(const std::string& state, float normalizedTime) {
		const int index = FindState(state);
		if (index < 0) {
			WarnOnce("Animator has no state '" + state + "'");
			return false;
		}
		return m_Machine.Play(index, normalizedTime, m_ClipInfos);
	}

	bool AnimatorRuntime::CrossFade(const std::string& state, float duration) {
		const int index = FindState(state);
		if (index < 0) {
			WarnOnce("Animator has no state '" + state + "'");
			return false;
		}
		return m_Machine.CrossFade(index, duration);
	}

}
