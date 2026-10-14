#pragma once
#include "Core/Export.hpp"
#include "Collections/Vec2.hpp"
#include "Scene/EntityHandle.hpp"
#include "Scene/SceneSystem.hpp"

namespace Index {

	struct InputFieldComponent;

	class UIEventSystem : public SceneSystem {
	public:
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

	private:
		EntityHandle m_PressedEntity = entt::null;
		EntityHandle m_HoveredEntity = entt::null;
		Vec2 m_HoveredAtMouse{ -1.0f, -1.0f };
		// Field whose cut/copy/paste toolbar is showing (phones).
		EntityHandle m_TextActionsField = entt::null;
	};

}
