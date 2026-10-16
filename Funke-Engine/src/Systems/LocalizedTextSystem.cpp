#include "pch.hpp"
#include "Systems/LocalizedTextSystem.hpp"

#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/UI/LocalizedTextComponent.hpp"
#include "Core/Application.hpp"
#include "Localization/GameLocalization.hpp"
#include "Profiling/Profiler.hpp"
#include "Scene/Scene.hpp"

namespace Funke {

	bool LocalizedTextSystem::Apply(LocalizedTextComponent& component, TextRendererComponent& text) {
		if (component.Key.empty()) return false;
		const std::uint32_t revision = GameLocalization::GetRevision();
		if (component.AppliedRevision == revision && component.AppliedKey == component.Key) return false;

		component.AppliedRevision = revision;
		component.AppliedKey = component.Key;
		const std::string& translated = GameLocalization::Get(component.Key);
		if (text.Text != translated) text.Text = translated;
		return true;
	}

	void LocalizedTextSystem::OnPreRender(Scene& scene) {
		// Edit mode keeps the authored text: writing it there would leak the preview into saved scenes and prefabs.
		if (!Application::GetIsPlaying()) return;
		FUNKE_PROFILE_SCOPE("LocalizedText");
		// Disabled entities too, so they show the right text the frame they are enabled.
		auto view = scene.GetRegistry().view<LocalizedTextComponent, TextRendererComponent>();
		for (auto [entity, localized, text] : view.each()) {
			Apply(localized, text);
		}
	}

}
