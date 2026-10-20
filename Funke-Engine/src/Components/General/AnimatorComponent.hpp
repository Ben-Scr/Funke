#pragma once
#include "Animation/AnimationCulling.hpp"
#include "Core/Export.hpp"

#include <cstdint>
#include <memory>

namespace Funke {
	class AnimatorRuntime;

	// Serialized as its integer value; append only.
	enum class AnimatorUpdateMode : uint8_t {
		Normal = 0,
		// Ignores Time.TimeScale (pause menus).
		UnscaledTime = 1,
		// Steps in FixedUpdate, in sync with physics.
		Fixed = 2
	};

	// Copying yields an empty handle, so clones and prefab instances get their own playback state while moves (entt
	// pool growth) keep it.
	class FUNKE_API AnimatorRuntimeHandle {
	public:
		AnimatorRuntimeHandle() = default;
		AnimatorRuntimeHandle(const AnimatorRuntimeHandle&) {}
		AnimatorRuntimeHandle& operator=(const AnimatorRuntimeHandle&) { m_Runtime.reset(); return *this; }
		AnimatorRuntimeHandle(AnimatorRuntimeHandle&&) noexcept = default;
		AnimatorRuntimeHandle& operator=(AnimatorRuntimeHandle&&) noexcept = default;

		AnimatorRuntime* Get() const { return m_Runtime.get(); }
		AnimatorRuntime& GetOrCreate();
		// Keeps the state alive while code that may remove the component (animation events) runs.
		std::shared_ptr<AnimatorRuntime> Share() { GetOrCreate(); return m_Runtime; }
		void Reset() { m_Runtime.reset(); }

	private:
		std::shared_ptr<AnimatorRuntime> m_Runtime;
	};

	// Plays .anim clips on this entity and its children, driven by an Animator Controller (.animctrl) state machine or a
	// single looping clip.
	struct FUNKE_API AnimatorComponent {
		// .animctrl asset GUID; wins over Clip.
		uint64_t Controller = 0;
		// .anim asset GUID played on its own when there is no Controller.
		uint64_t Clip = 0;
		float Speed = 1.0f;
		AnimatorUpdateMode UpdateMode = AnimatorUpdateMode::Normal;
		// Editor only: plays the animation in edit mode as a preview; the authored values come back afterwards.
		bool ApplyInEditMode = false;
		// Off screen (renderers below the Animator outside the game camera's view): keep animating, keep only the
		// clock, or pause.
		AnimationCullingMode CullingMode = AnimationCullingMode::AlwaysAnimate;
		// Fewer updates far from the camera (every 2nd / 4th frame); ignored in Fixed update mode.
		AnimationDistanceRate DistanceRate = AnimationDistanceRate::ProjectSetting;
		// With DistanceRate Custom: world units from the camera from which it updates every 2nd / 4th frame.
		float HalfRateDistance = 20.0f;
		float QuarterRateDistance = 40.0f;

		// Runtime state; not serialized, so every Play session starts in the default state.
		AnimatorRuntimeHandle Runtime;

		AnimationCullingSettings GetCulling() const { return { CullingMode, DistanceRate, HalfRateDistance, QuarterRateDistance }; }
	};
}
