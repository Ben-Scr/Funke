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
		const bool playing = Application::GetIsPlaying();
		const bool preview = !playing && !GameLocalization::GetPreviewLanguage().empty();
		for (auto [entity, text] : scene.GetRegistry().view<TextRendererComponent>().each()) {
			const auto* localized = scene.GetRegistry().try_get<LocalizedTextComponent>(entity);
			if (preview && localized && !localized->Key.empty()) {
				const std::string& translated = GameLocalization::GetPreview(localized->Key);
				if (!text.PreviewText || *text.PreviewText != translated) {
					text.PreviewText = translated;
					scene.MarkUIDirty();
				}
			}
			else if (text.PreviewText) {
				text.PreviewText.reset();
				scene.MarkUIDirty();
			}
		}
		if (!playing) return;
		FUNKE_PROFILE_SCOPE("LocalizedText");
		// Disabled entities too, so they show the right text the frame they are enabled.
		auto view = scene.GetRegistry().view<LocalizedTextComponent, TextRendererComponent>();
		for (auto [entity, localized, text] : view.each()) {
			Apply(localized, text);
		}
	}

}
