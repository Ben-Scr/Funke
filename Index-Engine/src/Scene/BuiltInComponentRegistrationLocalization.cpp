#include "pch.hpp"
#include "Scene/BuiltInComponentRegistrationShared.hpp"

#include "Components/UI/LocalizedTextComponent.hpp"
#include "Localization/GameLocalization.hpp"

#include <string>

// Localized Text: fills a TextRenderer from the game's Assets/Localization tables.
namespace Index::BuiltInComponentRegistrationInternal {
	void RegisterLocalization(ComponentRegistry& registry) {
		ComponentInfo info{ "Localized Text", "UI", ComponentCategory::Component };
		info.serializedName = "LocalizedText";
		info.properties = {
			Properties::Make("Key", "Key", &LocalizedTextComponent::Key,
				Properties::Meta::Tooltip("Key in Assets/Localization/<language>.json. During play the Text Renderer shows "
					"its translation and follows language switches.")),
			Properties::MakeWith<std::string>("Preview", "Translation",
				[](const Entity& e) -> std::string {
					const std::string& key = e.GetComponent<LocalizedTextComponent>().Key;
					if (key.empty()) return {};
					if (!GameLocalization::IsLoaded()) return "(no files in Assets/Localization)";
					// Has() first: Get() would log every half-typed key as missing.
					return GameLocalization::Has(key) ? GameLocalization::Get(key) : "(missing key)";
				},
				[](Entity&, const std::string&) {},
				Properties::Meta::ReadOnly().WithTooltip("The translation in the current game language.")),
		};
		info.serialize = [](Entity e) -> Json::Value {
			const auto& c = e.GetComponent<LocalizedTextComponent>();
			Json::Value v = Json::Value::MakeObject();
			v.AddMember("key", Json::Value(c.Key));
			return v;
		};
		info.deserialize = [](Entity e, const Json::Value& v) {
			auto& c = e.GetComponent<LocalizedTextComponent>();
			const LocalizedTextComponent d{};
			const Json::Value* m = v.FindMember("key");
			c.Key = m ? m->AsStringOr(d.Key) : d.Key;
			// Resolve again after a paste or reset.
			c.AppliedKey.clear();
			c.AppliedRevision = 0;
		};
		registry.Register<LocalizedTextComponent>(info);
	}
}
