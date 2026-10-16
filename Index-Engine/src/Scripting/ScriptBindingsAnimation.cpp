#include "pch.hpp"
#include "Scripting/ScriptGlue.hpp"
#include "Scripting/ScriptBindingGuard.hpp"

#include "Animation/AnimatorRuntime.hpp"
#include "Components/General/AnimatorComponent.hpp"
#include "Scene/Scene.hpp"

#include <algorithm>
#include <cstring>
#include <string>

// Animator for C# (Animator.SetTrigger & co.). Calls go to the runtime of the entity's Animator; parameters set before the
// first update apply on it.
namespace Index {
	bool ResolveEntityReference(uint64_t entityID, Scene*& outScene, EntityHandle& outHandle);

	namespace {
		enum class ParameterOp : int { Float = 0, Int = 1, Bool = 2, Trigger = 3, ResetTrigger = 4 };

		AnimatorComponent* FindAnimator(uint64_t entityID) {
			Scene* scene = nullptr;
			EntityHandle handle = entt::null;
			if (!ResolveEntityReference(entityID, scene, handle) || !scene->IsValid(handle)) return nullptr;
			return scene->GetRegistry().try_get<AnimatorComponent>(handle);
		}

		// Runtime with the component's assets loaded, or nullptr when nothing can play.
		AnimatorRuntime* SyncedRuntime(uint64_t entityID) {
			AnimatorComponent* animator = FindAnimator(entityID);
			if (!animator) return nullptr;
			AnimatorRuntime& runtime = animator->Runtime.GetOrCreate();
			if (!runtime.Sync(*animator)) return nullptr;
			if (!runtime.GetStateMachine().IsInitialized()) runtime.GetStateMachine().Reset(*runtime.GetController());
			return &runtime;
		}

		int CopyToBuffer(const std::string& value, char* outBuffer, int capacity) {
			const int required = static_cast<int>(value.size());
			if (outBuffer && capacity > 0) {
				const int count = std::min(required, capacity - 1);
				if (count > 0) std::memcpy(outBuffer, value.data(), static_cast<size_t>(count));
				outBuffer[count] = '\0';
			}
			return required;
		}

		int Index_Animator_SetParameter(uint64_t entityID, const char* name, int op, float value) {
			AnimatorRuntime* runtime = SyncedRuntime(entityID);
			if (!runtime || !name) return 0;
			switch (static_cast<ParameterOp>(op)) {
			case ParameterOp::Float: runtime->SetFloat(name, value); break;
			case ParameterOp::Int: runtime->SetInt(name, static_cast<int>(value)); break;
			case ParameterOp::Bool: runtime->SetBool(name, value != 0.0f); break;
			case ParameterOp::Trigger: runtime->SetTrigger(name); break;
			case ParameterOp::ResetTrigger: runtime->ResetTrigger(name); break;
			default: return 0;
			}
			return 1;
		}

		float Index_Animator_GetParameter(uint64_t entityID, const char* name, int op) {
			AnimatorRuntime* runtime = SyncedRuntime(entityID);
			if (!runtime || !name) return 0.0f;
			const AnimatorStateMachine& machine = runtime->GetStateMachine();
			switch (static_cast<ParameterOp>(op)) {
			case ParameterOp::Float: return machine.GetFloat(name);
			case ParameterOp::Int: return static_cast<float>(machine.GetInt(name));
			default: return machine.GetBool(name) ? 1.0f : 0.0f;
			}
		}

		int Index_Animator_HasParameter(uint64_t entityID, const char* name) {
			AnimatorRuntime* runtime = SyncedRuntime(entityID);
			return runtime && name && runtime->GetStateMachine().HasParameter(name) ? 1 : 0;
		}

		// crossFade < 0 jumps (Play), otherwise blends over that many seconds.
		int Index_Animator_Play(uint64_t entityID, const char* state, float normalizedTime, float crossFade) {
			AnimatorRuntime* runtime = SyncedRuntime(entityID);
			if (!runtime || !state) return 0;
			return (crossFade < 0.0f ? runtime->Play(state, normalizedTime) : runtime->CrossFade(state, crossFade)) ? 1 : 0;
		}

		// which: 0 = current state, 1 = crossfade target (empty when none).
		int Index_Animator_GetStateBuffer(uint64_t entityID, int which, char* outBuffer, int capacity) {
			AnimatorRuntime* runtime = SyncedRuntime(entityID);
			std::string name;
			if (runtime) {
				if (which == 0) {
					name = runtime->GetCurrentStateName();
				}
				else {
					const int next = runtime->GetStateMachine().GetNextState();
					const AnimatorController* controller = runtime->GetController();
					if (controller && next >= 0 && next < static_cast<int>(controller->States.size())) name = controller->States[static_cast<size_t>(next)].Name;
				}
			}
			return CopyToBuffer(name, outBuffer, capacity);
		}

		// values: normalizedTime, inTransition (0/1), transitionProgress.
		void Index_Animator_GetPlayback(uint64_t entityID, float* values, int count) {
			if (!values) return;
			float packed[3] = { 0.0f, 0.0f, 0.0f };
			if (AnimatorRuntime* runtime = SyncedRuntime(entityID)) {
				const AnimatorStateMachine& machine = runtime->GetStateMachine();
				packed[0] = machine.GetCurrentNormalizedTime(runtime->GetClipInfos());
				packed[1] = machine.IsInTransition() ? 1.0f : 0.0f;
				packed[2] = machine.GetTransitionProgress();
			}
			for (int i = 0; i < count && i < 3; ++i) values[i] = packed[i];
		}

		// which: 0 = Controller, 1 = Clip.
		uint64_t Index_Animator_GetAsset(uint64_t entityID, int which) {
			const AnimatorComponent* animator = FindAnimator(entityID);
			if (!animator) return 0;
			return which == 0 ? animator->Controller : animator->Clip;
		}

		void Index_Animator_SetAsset(uint64_t entityID, int which, uint64_t guid) {
			AnimatorComponent* animator = FindAnimator(entityID);
			if (!animator) return;
			(which == 0 ? animator->Controller : animator->Clip) = guid;
		}

		float Index_Animator_GetSpeed(uint64_t entityID) {
			const AnimatorComponent* animator = FindAnimator(entityID);
			return animator ? animator->Speed : 0.0f;
		}

		void Index_Animator_SetSpeed(uint64_t entityID, float speed) {
			if (AnimatorComponent* animator = FindAnimator(entityID)) animator->Speed = speed;
		}
	}

	void PopulateAnimationBindings(NativeBindings& b) {
		b.Animator_SetParameter = IDX_GUARDED_BINDING(Index_Animator_SetParameter);
		b.Animator_GetParameter = IDX_GUARDED_BINDING(Index_Animator_GetParameter);
		b.Animator_HasParameter = IDX_GUARDED_BINDING(Index_Animator_HasParameter);
		b.Animator_Play = IDX_GUARDED_BINDING(Index_Animator_Play);
		b.Animator_GetStateBuffer = IDX_GUARDED_BINDING(Index_Animator_GetStateBuffer);
		b.Animator_GetPlayback = IDX_GUARDED_BINDING(Index_Animator_GetPlayback);
		b.Animator_GetAsset = IDX_GUARDED_BINDING(Index_Animator_GetAsset);
		b.Animator_SetAsset = IDX_GUARDED_BINDING(Index_Animator_SetAsset);
		b.Animator_GetSpeed = IDX_GUARDED_BINDING(Index_Animator_GetSpeed);
		b.Animator_SetSpeed = IDX_GUARDED_BINDING(Index_Animator_SetSpeed);
	}

}
