#pragma once
#include "Core/Export.hpp"
#include "Scene/SceneSystem.hpp"

namespace Funke {

	// Free function (not member) so UILayoutSystem and UIRenderer can both call it; FUNKE_API for editor gizmo use.
	FUNKE_API void ComputeUILayout(Scene& scene);
	// ComputeUILayout, skipped in the player when neither the UI surface nor the layout epoch changed since
	// the scene's last pass. Per-frame callers use this; one-off callers (tests, editor tools) use ComputeUILayout.
	FUNKE_API void UpdateUILayout(Scene& scene);
	// Bumps the layout epoch; call after UI state changes outside the scene systems (fonts, global scripts).
	FUNKE_API void InvalidateUILayout();

	// MUST run before UIEventSystem so hit-tests use up-to-date resolved rects.
	class UILayoutSystem : public SceneSystem {
	public:
		void Update(Scene& scene) override;
	};

}
