#pragma once
#include "Core/Export.hpp"
#include "Serialization/Json.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Funke {

	// Serialized by name ("float", "int", "bool", "trigger").
	enum class AnimatorParameterType : uint8_t {
		Float = 0,
		Int = 1,
		Bool = 2,
		// A bool that a transition consumes when it fires.
		Trigger = 3
	};

	struct FUNKE_API AnimatorParameter {
		std::string Name;
		AnimatorParameterType Type = AnimatorParameterType::Float;
		float DefaultFloat = 0.0f;
		int DefaultInt = 0;
		bool DefaultBool = false;

		bool operator==(const AnimatorParameter&) const = default;
	};

	// Serialized by name ("greater", "less", "equals", "notEquals", "if", "ifNot").
	enum class AnimatorConditionMode : uint8_t {
		Greater = 0,
		Less = 1,
		Equals = 2,
		NotEquals = 3,
		// Bool true / trigger set.
		If = 4,
		IfNot = 5
	};

	struct FUNKE_API AnimatorCondition {
		std::string Parameter;
		AnimatorConditionMode Mode = AnimatorConditionMode::If;
		float Threshold = 0.0f;

		bool operator==(const AnimatorCondition&) const = default;
	};

	struct FUNKE_API AnimatorTransition {
		// State index; k_AnyState for transitions that can leave any state.
		int From = 0;
		int To = 0;
		std::vector<AnimatorCondition> Conditions;
		bool HasExitTime = false;
		// Normalized time of the source state (1 = one clip length).
		float ExitTime = 1.0f;
		// Crossfade length in seconds.
		float Duration = 0.25f;
		// Any State transitions only: may restart the state that is already playing.
		bool CanTransitionToSelf = false;

		static constexpr int k_AnyState = -1;
		bool operator==(const AnimatorTransition&) const = default;
	};

	// Serialized by name ("clip", "loop", "once").
	enum class AnimatorStateLoop : uint8_t {
		FromClip = 0,
		Loop = 1,
		Once = 2
	};

	struct FUNKE_API AnimatorState {
		std::string Name;
		// .anim asset GUID; 0 = an empty state that only waits for transitions.
		uint64_t Clip = 0;
		float Speed = 1.0f;
		AnimatorStateLoop Loop = AnimatorStateLoop::FromClip;
		// Node position in the Animator window.
		glm::vec2 Position{ 0.0f };

		bool operator==(const AnimatorState&) const = default;
	};

	// .animctrl asset (JSON, GUID via its .meta). Transitions reference states by name in the file and by index here.
	struct FUNKE_API AnimatorController {
		std::vector<AnimatorParameter> Parameters;
		std::vector<AnimatorState> States;
		std::vector<AnimatorTransition> Transitions;
		int DefaultState = 0;
		glm::vec2 EntryPosition{ -250.0f, 0.0f };
		glm::vec2 AnyStatePosition{ -250.0f, 120.0f };

		int FindState(const std::string& name) const;
		int FindParameter(const std::string& name) const;
		// Fixes transition indices after removing a state and drops transitions that touched it.
		void RemoveState(int index);

		bool operator==(const AnimatorController&) const = default;
	};

	inline constexpr int k_AnimatorControllerFormatVersion = 1;
	inline constexpr const char* k_AnimatorControllerExtension = ".animctrl";

	FUNKE_API const char* AnimatorParameterTypeToString(AnimatorParameterType type);
	FUNKE_API const char* AnimatorConditionModeToString(AnimatorConditionMode mode);

	FUNKE_API Json::Value AnimatorControllerToJson(const AnimatorController& controller);
	FUNKE_API AnimatorController AnimatorControllerFromJson(const Json::Value& value);

	// Shared live .animctrl instances by asset GUID (main thread); same contract as AnimationClipLibrary.
	class FUNKE_API AnimatorControllerLibrary {
	public:
		AnimatorControllerLibrary() = delete;

		static std::shared_ptr<const AnimatorController> Get(uint64_t guid);

		static bool LoadFromFile(const std::string& path, AnimatorController& outController);
		static bool SaveToFile(const std::string& path, const AnimatorController& controller);

		static bool Save(uint64_t guid, const AnimatorController& controller);
		static void Set(uint64_t guid, const AnimatorController& controller);

		static void Invalidate(uint64_t guid);
		static size_t ReloadChangedFromDisk();
		static void Clear();
	};

}
