#pragma once
#include "Core/Export.hpp"
#include "Collections/Vec2.hpp"
#include "Core/Input.hpp"
#include "Scene/EntityHandle.hpp"
#include "Scene/SceneSystem.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <vector>

namespace Index {

	struct InputFieldComponent;

	class UIEventSystem : public SceneSystem {
	public:
		static constexpr int64_t k_NoFinger = std::numeric_limits<int64_t>::min();

		// One finger this frame, already mapped to UI space.
		struct FingerSample {
			int64_t Id = 0;
			TouchPhase Phase = TouchPhase::Began;
			Vec2 Ui{};
			bool OverSurface = false;
		};
		// An Interactable held by a finger the mouse does not follow.
		struct FingerPress {
			int64_t FingerId = 0;
			EntityHandle Entity = entt::null;
		};

		void Update(Scene& scene) override;
		// OnPreRender keeps slider visuals in sync with inspector edits in edit mode (Update doesn't run outside play).
		void OnPreRender(Scene& scene) override;
		void OnDestroy(Scene&) override;

		// Front-most Interactable entity under the pointer, or entt::null. Already gated on an enabled
		// InteractableComponent; surfaced to C# via UIPointer.HoveredEntity. Re-picked when the pointer moved
		// since this frame's Update, which scripts (running earlier) would otherwise see from last frame.
		EntityHandle GetHoveredEntity(Scene& scene) const;

		// True when a press on `pressed` may turn into a drag of `scrollRect`: it sits inside the scroll rect and is
		// not a widget that drags on its own (slider, scrollbar, input field, nested scroll rect).
		INDEX_API static bool CanScrollRectTakeOverPress(const entt::registry& registry, EntityHandle pressed, EntityHandle scrollRect);

		// Long-press selection: the word around `byte`, or nothing on a space / punctuation. Bytes >= 0x80 count
		// as word characters, so UTF-8 letters stay whole.
		INDEX_API static void SelectWordAt(InputFieldComponent& field, int byte);

		// The scroll rect the mouse wheel moves: the nearest vertical one around `hovered` (itself included),
		// else the nearest horizontal one. Null over a multiline input field, which scrolls its own text.
		INDEX_API static EntityHandle WheelScrollTarget(const entt::registry& registry, EntityHandle hovered);
		// True when point is over the slider's thumb, which grabs the slider even outside the track
		// and without an Interactable of its own.
		INDEX_API static bool SliderHandleContains(entt::registry& registry, EntityHandle slider, const Vec2& point);

		// The finger SDL's mouse emulation follows: kept while it is down, else the first finger that began
		// this frame, else none.
		INDEX_API static int64_t TrackMouseFinger(int64_t current, std::span<const FingerSample> fingers);
		// Press, hold and release of Interactables by every finger except mouseFinger, so two buttons can be held
		// at once. Widgets that drag (sliders, scrollbars, scroll rects, input fields, dropdowns) stay with the
		// mouse finger. Sets the Interactable edge flags; `clicked` gets entities released over themselves.
		INDEX_API static void UpdateFingerPresses(entt::registry& registry, std::span<const FingerSample> fingers,
			int64_t mouseFinger, EntityHandle mousePressed, const std::function<EntityHandle(Vec2)>& pick,
			std::vector<FingerPress>& presses, std::vector<EntityHandle>& clicked);

	private:
		EntityHandle m_PressedEntity = entt::null;
		int64_t m_MouseFinger = k_NoFinger;
		std::vector<FingerPress> m_FingerPresses;
		EntityHandle m_HoveredEntity = entt::null;
		Vec2 m_HoveredAtMouse{ -1.0f, -1.0f };
		// Field whose cut/copy/paste toolbar is showing (phones).
		EntityHandle m_TextActionsField = entt::null;
	};

}
