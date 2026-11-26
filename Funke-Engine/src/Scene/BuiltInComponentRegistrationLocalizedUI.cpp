#include "pch.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"
#include "Components/UI/LocalizedUIComponent.hpp"

namespace Funke::BuiltInComponentRegistrationInternal {
	void RegisterLocalizedUI(ComponentRegistry& registry) {
		ComponentInfo info{"LocalizedUI", "UI", ComponentCategory::Component};
		info.serializedName = "LocalizedUI";
		info.properties = {
			MakeUiEntityRef<LocalizedUIComponent>("LabelEntity", "Label Entity", &LocalizedUIComponent::LabelEntity),
			Properties::Make("LabelKey", "Label Key", &LocalizedUIComponent::LabelKey),
			Properties::Make("PlaceholderKey", "Placeholder Key", &LocalizedUIComponent::PlaceholderKey),
			Properties::MakeStringList("OptionKeys", "Option Keys",
				[](const Entity& entity) { return entity.GetComponent<LocalizedUIComponent>().OptionKeys; },
				[](Entity& entity, const std::vector<std::string>& keys) { entity.GetComponent<LocalizedUIComponent>().OptionKeys = keys; })
		};
		info.serialize = [](Entity entity) {
			const auto& binding = entity.GetComponent<LocalizedUIComponent>();
			Json::Value value = Json::Value::MakeObject();
			value.AddMember("labelEntity", UIEntityHandleToJson(entity, binding.LabelEntity));
			value.AddMember("labelKey", binding.LabelKey);
			value.AddMember("placeholderKey", binding.PlaceholderKey);
			Json::Value keys = Json::Value::MakeArray();
			for (const auto& key : binding.OptionKeys) keys.Append(Json::Value(key));
			value.AddMember("optionKeys", std::move(keys));
			return value;
		};
		info.deserialize = [](Entity entity, const Json::Value& value) {
			auto& binding = entity.GetComponent<LocalizedUIComponent>();
			if (const auto* v = value.FindMember("labelEntity")) UIEntityHandleFromJson(entity, *v, binding.LabelEntity);
			if (const auto* v = value.FindMember("labelKey")) binding.LabelKey = v->AsStringOr();
			if (const auto* v = value.FindMember("placeholderKey")) binding.PlaceholderKey = v->AsStringOr();
			if (const auto* v = value.FindMember("optionKeys"); v && v->IsArray()) {
				binding.OptionKeys.clear();
				for (const auto& key : v->GetArray()) binding.OptionKeys.push_back(key.AsStringOr());
			}
		};
		registry.Register<LocalizedUIComponent>(info);
	}
}
