#include <doctest/doctest.h>

#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/UI/LocalizedTextComponent.hpp"
#include "Localization/GameLocalization.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Scene.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Systems/LocalizedTextSystem.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using namespace Funke;

namespace {
	// A temp Assets/Localization folder; leaves GameLocalization unloaded and without a provider afterwards.
	struct LanguageFolder {
		std::filesystem::path Directory;

		LanguageFolder() {
			const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Directory = std::filesystem::temp_directory_path() / ("FunkeGameLocalization_" + std::to_string(suffix)) / "Localization";
			std::filesystem::create_directories(Directory);
		}

		~LanguageFolder() {
			GameLocalization::SetSavedLanguageProvider(nullptr);
			GameLocalization::Unload();
			std::error_code error;
			std::filesystem::remove_all(Directory.parent_path(), error);
		}

		void Write(const std::string& name, const std::string& content) const {
			std::ofstream(Directory / name, std::ios::binary | std::ios::trunc) << content;
		}

		void WriteLanguage(const std::string& code, const std::string& displayName, const std::string& strings) const {
			Write(code + ".json", R"({ "meta": { "code": ")" + code + R"(", "displayName": ")" + displayName
				+ R"(", "fallback": "en", "schemaVersion": 1 }, "strings": )" + strings + " }");
		}

		void WriteEnglishAndGerman() const {
			WriteLanguage("en", "English", R"({ "menu.play": "Play", "menu.quit": "Quit", "hud.score": "Score: {0}", "broken": "Value {0" })");
			WriteLanguage("de", "Deutsch", R"({ "menu.play": "Spielen", "hud.score": "Punkte: {0}" })");
		}
	};
}

TEST_CASE("GameLocalization: lookups use the current language, then the fallback, then the key") {
	LanguageFolder folder;
	folder.WriteEnglishAndGerman();
	folder.Write("broken.json", "{ not json");
	folder.Write("notes.txt", "not a language");

	REQUIRE(GameLocalization::Load(folder.Directory));
	const auto& languages = GameLocalization::GetAvailableLanguages();
	REQUIRE(languages.size() == 2);
	CHECK(languages[0].Code == "en");
	CHECK(languages[1].Code == "de");
	CHECK(languages[1].DisplayName == "Deutsch");
	CHECK(GameLocalization::GetFallbackLanguage() == "en");

	REQUIRE(GameLocalization::SetLanguage("de"));
	CHECK(GameLocalization::GetCurrentLanguage() == "de");
	CHECK(GameLocalization::Get("menu.play") == "Spielen");
	CHECK(GameLocalization::Get("menu.quit") == "Quit");
	CHECK(GameLocalization::Get("menu.missing") == "menu.missing");
	CHECK(GameLocalization::Has("menu.quit"));
	CHECK_FALSE(GameLocalization::Has("menu.missing"));
	CHECK(GameLocalization::Format("hud.score", 42) == "Punkte: 42");
	CHECK(GameLocalization::Format("broken", 1) == "Value {0");

	REQUIRE(GameLocalization::SetLanguage("en"));
	CHECK(GameLocalization::Get("menu.play") == "Play");
}

TEST_CASE("GameLocalization: switching notifies once and unknown languages are refused") {
	LanguageFolder folder;
	folder.WriteEnglishAndGerman();
	REQUIRE(GameLocalization::Load(folder.Directory));
	REQUIRE(GameLocalization::SetLanguage("en"));

	int calls = 0;
	const auto handle = GameLocalization::RegisterChangeCallback([&] { ++calls; });
	const std::uint32_t revision = GameLocalization::GetRevision();

	CHECK(GameLocalization::SetLanguage("de"));
	CHECK(calls == 1);
	CHECK(GameLocalization::GetRevision() != revision);
	CHECK(GameLocalization::SetLanguage("de"));
	CHECK(calls == 1);
	CHECK_FALSE(GameLocalization::SetLanguage("fr"));
	CHECK(GameLocalization::GetCurrentLanguage() == "de");
	CHECK(calls == 1);

	GameLocalization::UnregisterChangeCallback(handle);
	CHECK(GameLocalization::SetLanguage("en"));
	CHECK(calls == 1);
}

TEST_CASE("GameLocalization: reload keeps the language and notifies only on real changes") {
	LanguageFolder folder;
	folder.WriteEnglishAndGerman();
	REQUIRE(GameLocalization::Load(folder.Directory));
	REQUIRE(GameLocalization::SetLanguage("de"));

	int calls = 0;
	const auto handle = GameLocalization::RegisterChangeCallback([&] { ++calls; });

	CHECK(GameLocalization::Reload());
	CHECK(calls == 0);

	folder.WriteLanguage("de", "Deutsch", R"({ "menu.play": "Los" })");
	CHECK(GameLocalization::Reload());
	CHECK(calls == 1);
	CHECK(GameLocalization::GetCurrentLanguage() == "de");
	CHECK(GameLocalization::Get("menu.play") == "Los");

	std::filesystem::remove(folder.Directory / "de.json");
	CHECK(GameLocalization::Reload());
	CHECK(calls == 2);
	CHECK(GameLocalization::GetCurrentLanguage() == "en");
	CHECK_FALSE(GameLocalization::IsLanguageAvailable("de"));

	GameLocalization::UnregisterChangeCallback(handle);
	GameLocalization::Unload();
	CHECK_FALSE(GameLocalization::IsLoaded());
	CHECK(GameLocalization::GetAvailableLanguages().empty());
	CHECK(GameLocalization::Get("menu.play") == "menu.play");
}

TEST_CASE("GameLocalization: without en.json the first language is the fallback; codes default to file names") {
	LanguageFolder folder;
	folder.Write("fr.json", R"({ "strings": { "a": "A-fr" } })");
	folder.Write("de.json", R"({ "meta": { "code": "de", "displayName": "Deutsch" }, "strings": { "a": "A-de", "b": "B-de" } })");

	REQUIRE(GameLocalization::Load(folder.Directory));
	CHECK(GameLocalization::GetFallbackLanguage() == "de");
	const auto& languages = GameLocalization::GetAvailableLanguages();
	REQUIRE(languages.size() == 2);
	CHECK(languages[0].Code == "de");
	CHECK(languages[1].Code == "fr");
	CHECK(languages[1].DisplayName == "fr");

	REQUIRE(GameLocalization::SetLanguage("fr"));
	CHECK(GameLocalization::Get("a") == "A-fr");
	CHECK(GameLocalization::Get("b") == "B-de");
}

TEST_CASE("GameLocalization: the start language is the saved one, then the OS language, then the fallback") {
	const std::vector<GameLocalization::LanguageInfo> languages{ { "en", "English" }, { "de", "Deutsch" }, { "pt-BR", "Português" } };
	CHECK(GameLocalization::ChooseStartLanguage("de", "en-US", languages, "en") == "de");
	CHECK(GameLocalization::ChooseStartLanguage("xx", "de-DE", languages, "en") == "de");
	CHECK(GameLocalization::ChooseStartLanguage("", "de_AT.UTF-8", languages, "en") == "de");
	CHECK(GameLocalization::ChooseStartLanguage("", "pt_BR", languages, "en") == "pt-BR");
	CHECK(GameLocalization::ChooseStartLanguage("", "pt-PT", languages, "en") == "pt-BR");
	CHECK(GameLocalization::ChooseStartLanguage("", "ja-JP", languages, "en") == "en");
	CHECK(GameLocalization::ChooseStartLanguage("", "", languages, "en") == "en");
	CHECK(GameLocalization::MatchLanguage("EN-gb", languages) == "en");
}

TEST_CASE("GameLocalization: the saved-language provider decides the start language") {
	LanguageFolder folder;
	folder.WriteEnglishAndGerman();
	REQUIRE(GameLocalization::Load(folder.Directory));
	REQUIRE(GameLocalization::SetLanguage("en"));

	GameLocalization::SetSavedLanguageProvider([] { return std::string("de"); });
	CHECK(GameLocalization::GetCurrentLanguage() == "de");

	REQUIRE(GameLocalization::SetLanguage("en"));
	REQUIRE(GameLocalization::Load(folder.Directory));
	CHECK(GameLocalization::GetCurrentLanguage() == "de");

	GameLocalization::SetSavedLanguageProvider([] { return std::string("fr"); });
	CHECK(GameLocalization::GetCurrentLanguage() == "de");
}

TEST_CASE("Localized Text writes the translation once and again after a language switch") {
	LanguageFolder folder;
	folder.WriteEnglishAndGerman();
	REQUIRE(GameLocalization::Load(folder.Directory));
	REQUIRE(GameLocalization::SetLanguage("en"));

	LocalizedTextComponent localized;
	localized.Key = "menu.play";
	TextRendererComponent text;
	text.Text = "placeholder";

	CHECK(LocalizedTextSystem::Apply(localized, text));
	CHECK(text.Text == "Play");

	text.Text = "Set by a script";
	CHECK_FALSE(LocalizedTextSystem::Apply(localized, text));
	CHECK(text.Text == "Set by a script");

	REQUIRE(GameLocalization::SetLanguage("de"));
	CHECK(LocalizedTextSystem::Apply(localized, text));
	CHECK(text.Text == "Spielen");

	localized.Key = "menu.quit";
	CHECK(LocalizedTextSystem::Apply(localized, text));
	CHECK(text.Text == "Quit");

	localized.Key.clear();
	text.Text = "free";
	CHECK_FALSE(LocalizedTextSystem::Apply(localized, text));
	CHECK(text.Text == "free");
}

TEST_CASE("Localized Text key round-trips through the serializer; a missing key resets it") {
	auto scene = Scene::CreateDetachedScene("RoundTrip LocalizedText");
	Entity label = scene->CreateEntity("Label");
	label.AddComponent<TextRendererComponent>();
	label.AddComponent<LocalizedTextComponent>().Key = "menu.play";

	const Json::Value full = SceneSerializer::SerializeEntityFull(*scene, label.GetHandle());
	const EntityHandle clone = SceneSerializer::DeserializeEntityFromValue(*scene, full);
	REQUIRE(clone != entt::null);
	REQUIRE(scene->HasComponent<LocalizedTextComponent>(clone));
	CHECK(scene->GetComponent<LocalizedTextComponent>(clone).Key == "menu.play");

	const Json::Value component = SceneSerializer::SerializeComponent(*scene, label.GetHandle(), "LocalizedText");
	REQUIRE(component.IsObject());
	const Json::Value* key = component.FindMember("key");
	REQUIRE(key);
	CHECK(key->AsStringOr() == "menu.play");

	Entity other = scene->CreateEntity("Other");
	other.AddComponent<TextRendererComponent>();
	other.AddComponent<LocalizedTextComponent>().AppliedRevision = 7;
	REQUIRE(SceneSerializer::DeserializeComponent(*scene, other.GetHandle(), "LocalizedText", component));
	CHECK(other.GetComponent<LocalizedTextComponent>().Key == "menu.play");
	CHECK(other.GetComponent<LocalizedTextComponent>().AppliedRevision == 0);

	REQUIRE(SceneSerializer::DeserializeComponent(*scene, other.GetHandle(), "LocalizedText", Json::Value::MakeObject()));
	CHECK(other.GetComponent<LocalizedTextComponent>().Key.empty());
}
