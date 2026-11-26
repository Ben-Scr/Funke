#include "pch.hpp"
#include "Gui/WidgetLocalization.hpp"
#include "Components/UI/LocalizedUIComponent.hpp"
#include "Components/UI/DropdownComponent.hpp"
#include "Components/UI/InputFieldComponent.hpp"
#include "Components/Graphics/TextRendererComponent.hpp"
#include "Localization/GameLocalization.hpp"
#include <entt/entity/registry.hpp>
#include <algorithm>

namespace Funke::WidgetLocalization {
	const std::string& Resolve(const std::string& literal, const std::string& key, bool playing) {
		if (key.empty()) return literal;
		if (playing) return GameLocalization::Get(key);
		return GameLocalization::GetPreviewLanguage().empty() ? literal : GameLocalization::GetPreview(key);
	}

	const std::string& Option(const entt::registry& registry, EntityHandle entity,
		const DropdownComponent& dropdown, size_t index, bool playing) {
		static const std::string empty;
		if (index >= dropdown.Options.size()) return empty;
		const auto* binding = registry.try_get<LocalizedUIComponent>(entity);
		if (!binding || index >= binding->OptionKeys.size()) return dropdown.Options[index];
		return Resolve(dropdown.Options[index], binding->OptionKeys[index], playing);
	}

	DisplayOverrides Collect(const entt::registry& registry, bool playing) {
		DisplayOverrides result;
		if (!playing && GameLocalization::GetPreviewLanguage().empty()) return result;
		for (auto [entity, binding] : registry.view<const LocalizedUIComponent>().each()) {
			const auto label = binding.LabelEntity == entt::null ? entity : binding.LabelEntity;
			if (!binding.LabelKey.empty() && registry.valid(label)) {
				if (const auto* text = registry.try_get<TextRendererComponent>(label))
					result[label] = Resolve(text->Text, binding.LabelKey, playing);
			}
			if (const auto* dropdown = registry.try_get<DropdownComponent>(entity);
				dropdown && !dropdown->Options.empty() && registry.valid(dropdown->LabelEntity)) {
				const auto index = static_cast<size_t>(std::clamp(dropdown->SelectedIndex, 0, static_cast<int>(dropdown->Options.size()) - 1));
				if (index < binding.OptionKeys.size() && !binding.OptionKeys[index].empty())
					result[dropdown->LabelEntity] = Option(registry, entity, *dropdown, index, playing);
			}
			if (const auto* field = registry.try_get<InputFieldComponent>(entity);
				field && field->Text.empty() && (!playing || !field->IsFocused)
				&& !binding.PlaceholderKey.empty() && registry.valid(field->TextEntity))
				result[field->TextEntity] = Resolve(field->PlaceholderText, binding.PlaceholderKey, playing);
		}
		return result;
	}
}
