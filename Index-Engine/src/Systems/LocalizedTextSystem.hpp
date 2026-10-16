#pragma once
#include "Core/Export.hpp"
#include "Scene/SceneSystem.hpp"

namespace Index {
	struct LocalizedTextComponent;
	struct TextRendererComponent;

	class INDEX_API LocalizedTextSystem : public SceneSystem {
	public:
		// OnPreRender so a language switch from a paused game's menu still shows up.
		void OnPreRender(Scene& scene) override;

		// Writes the translation of component.Key into text when the key or the game language changed since the
		// last write. Returns true when it resolved the key.
		static bool Apply(LocalizedTextComponent& component, TextRendererComponent& text);
	};
}
