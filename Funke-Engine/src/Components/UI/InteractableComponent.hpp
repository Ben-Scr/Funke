#pragma once
#include "Scene/EntityHandle.hpp"

namespace Funke {

	struct InteractableComponent {
		bool Interactable = true;

		bool IsHovered = false;
		bool IsMouseDown = false;
		bool IsClicked = false;
		bool IsMouseUp = false;
		bool IsPressed = false;

		// Opt-in: when true, UIFocusSystem includes this widget in
		// keyboard / controller navigation. Default false preserves
		// mouse-only behaviour for scenes that don't want navigation.
		bool Focusable = false;

		// Opt-in directional navigation; Tab remains linear. Links override geometry when eligible.
		bool SpatialNavigation = false;
		bool NavigationWrap = false;
		EntityHandle NavigationLeft = entt::null;
		EntityHandle NavigationRight = entt::null;
		EntityHandle NavigationUp = entt::null;
		EntityHandle NavigationDown = entt::null;

		bool IsFocused = false;

		// Set by UIFocusSystem on keyboard/gamepad Activate; UIEventSystem synthesises mouse-click flags from it so widgets need no focus-awareness.
		bool ActivatedThisFrame = false;
	};

}
