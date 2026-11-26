#include <doctest/doctest.h>

#include "Components/Graphics/TextRendererComponent.hpp"
#include "Components/UI/LocalizedTextComponent.hpp"
#include "Components/UI/LocalizedUIComponent.hpp"
#include "Components/UI/DropdownComponent.hpp"
#include "Components/UI/InputFieldComponent.hpp"
#include "Gui/WidgetLocalization.hpp"
#include "Gui/InputFieldEditing.hpp"
#include "Scene/ComponentRegistry.hpp"
#include "Scene/SceneManager.hpp"
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

TEST_CASE("Localized UI bindings preserve literal options, selection and user input across language switches") {
	const auto directory = std::filesystem::temp_directory_path() / ("FunkeWidgetLocalization_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(directory);
	struct Cleanup { std::filesystem::path Path; ~Cleanup() { GameLocalization::Unload(); std::error_code error; std::filesystem::remove_all(Path, error); } } cleanup{directory};
	std::ofstream(directory / "en.json") << R"({"meta":{"code":"en","displayName":"English","fallback":"en","schemaVersion":1},"strings":{"play":"Play","prompt":"Your name","empty":""}})";
	std::ofstream(directory / "de.json") << R"({"meta":{"code":"de","displayName":"Deutsch","fallback":"en","schemaVersion":1},"strings":{"play":"Spielen","prompt":"Dein Name","empty":""}})";
	REQUIRE(GameLocalization::Load(directory));
	auto scene = Scene::CreateDetachedScene("Mixed UI");
	auto label = scene->CreateEntity("Button label");
	label.AddComponent<TextRendererComponent>().Text = "Literal button";
	label.AddComponent<LocalizedUIComponent>().LabelKey = "play";
	auto dropdown = scene->CreateEntity("Dropdown");
	auto dropdownLabel = scene->CreateEntity("Dropdown label");
	dropdownLabel.AddComponent<TextRendererComponent>();
	auto& dd = dropdown.AddComponent<DropdownComponent>();
	dd.LabelEntity = dropdownLabel.GetHandle(); dd.Options = {"Literal A", "Literal B", "Literal C"}; dd.SelectedIndex = 1;
	dropdown.AddComponent<LocalizedUIComponent>().OptionKeys = {"", "play", "missing"};
	auto input = scene->CreateEntity("Input");
	auto inputLabel = scene->CreateEntity("Input label");
	inputLabel.AddComponent<TextRendererComponent>();
	auto& field = input.AddComponent<InputFieldComponent>();
	field.TextEntity = inputLabel.GetHandle(); field.PlaceholderText = "Literal placeholder";
	input.AddComponent<LocalizedUIComponent>().PlaceholderKey = "prompt";
	for (const auto& language : {"de", "en", "de"}) {
		REQUIRE(GameLocalization::SetLanguage(language));
		const auto overrides = WidgetLocalization::Collect(scene->GetRegistry(), true);
		CHECK(overrides.at(label.GetHandle()) == GameLocalization::Get("play"));
		CHECK(overrides.at(dropdownLabel.GetHandle()) == GameLocalization::Get("play"));
		CHECK(overrides.at(inputLabel.GetHandle()) == GameLocalization::Get("prompt"));
		CHECK(dd.SelectedIndex == 1);
		CHECK(dd.Options[1] == "Literal B");
		CHECK(WidgetLocalization::Option(scene->GetRegistry(), dropdown.GetHandle(), dd, 0, true) == "Literal A");
		CHECK(WidgetLocalization::Option(scene->GetRegistry(), dropdown.GetHandle(), dd, 2, true) == "missing");
		CHECK(WidgetLocalization::Option(scene->GetRegistry(), dropdown.GetHandle(), dd, 99, true).empty());
	}
	field.Text = "user input"; field.CaretBytePos = 4; field.SelectionAnchorBytePos = 1;
	CHECK_FALSE(WidgetLocalization::Collect(scene->GetRegistry(), true).contains(inputLabel.GetHandle()));
	CHECK(field.Text == "user input"); CHECK(field.CaretBytePos == 4); CHECK(field.SelectionAnchorBytePos == 1);
	InputFieldEditing::InsertAtCaret(field, "X");
	CHECK(field.Text == "uX input");
	label.GetComponent<LocalizedUIComponent>().LabelKey = "empty";
	CHECK(WidgetLocalization::Collect(scene->GetRegistry(), true).at(label.GetHandle()).empty());
	label.GetComponent<LocalizedUIComponent>().LabelKey.clear();
	CHECK_FALSE(WidgetLocalization::Collect(scene->GetRegistry(), true).contains(label.GetHandle()));
	CHECK(label.GetComponent<TextRendererComponent>().Text == "Literal button");
	CHECK(field.PlaceholderText == "Literal placeholder");
	CHECK(WidgetLocalization::Collect(scene->GetRegistry(), false).empty());
}

TEST_CASE("Localized UI binding serialization remaps its label and retains empty option keys") {
	auto& registry = SceneManager::GetComponentRegistryOrBuiltIn();
	REQUIRE(registry.GetInfo<LocalizedUIComponent>() != nullptr);
	auto scene = Scene::CreateDetachedScene("Localized UI serialization");
	auto widget = scene->CreateEntity("Widget");
	auto& binding = widget.AddComponent<LocalizedUIComponent>();
	binding.LabelEntity = widget.GetHandle(); binding.LabelKey = "play"; binding.PlaceholderKey = "prompt";
	binding.OptionKeys = {"first", "", "third"};
	const auto serialized = SceneSerializer::SerializeEntityFull(*scene, widget.GetHandle());
	const auto clone = SceneSerializer::DeserializeEntityFromValue(*scene, serialized);
	REQUIRE(clone != entt::null);
	REQUIRE(scene->HasComponent<LocalizedUIComponent>(clone));
	const auto& copied = scene->GetComponent<LocalizedUIComponent>(clone);
	CHECK(copied.LabelEntity == clone);
	CHECK(copied.LabelKey == "play");
	CHECK(copied.PlaceholderKey == "prompt");
	CHECK(copied.OptionKeys == std::vector<std::string>{"first", "", "third"});
}

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

TEST_CASE("Editor language preview changes display and layout state without changing authored text or player language") {
	LanguageFolder folder;
	folder.WriteEnglishAndGerman();
	std::string saved = "en";
	GameLocalization::SetSavedLanguageProvider([&] { return saved; });
	REQUIRE(GameLocalization::Load(folder.Directory));
	auto scene = Scene::CreateDetachedScene("Preview");
	auto label = scene->CreateEntity("Label");
	auto& text = label.AddComponent<TextRendererComponent>();
	text.Text = "Authored placeholder";
	label.AddComponent<LocalizedTextComponent>().Key = "menu.play";
	const auto original = Json::Stringify(SceneSerializer::SerializeEntityFull(*scene, label.GetHandle()));
	scene->ClearDirty();
	scene->ClearUIDirty();
	LocalizedTextSystem system;
	int playerChanges = 0;
	const auto callback = GameLocalization::RegisterChangeCallback([&] { ++playerChanges; });
	const auto playerRevision = GameLocalization::GetRevision();
	REQUIRE(GameLocalization::SetPreviewLanguage("de"));
	CHECK(playerChanges == 0);
	CHECK(GameLocalization::GetRevision() == playerRevision);
	GameLocalization::UnregisterChangeCallback(callback);
	system.OnPreRender(*scene);
	CHECK(text.GetDisplayText() == "Spielen");
	CHECK(text.Text == "Authored placeholder");
	CHECK(GameLocalization::GetCurrentLanguage() == "en");
	CHECK(GameLocalization::Get("menu.play") == "Play");
	CHECK(saved == "en");
	CHECK(scene->IsUIDirty());
	CHECK_FALSE(scene->IsDirty());
	CHECK(Json::Stringify(SceneSerializer::SerializeEntityFull(*scene, label.GetHandle())) == original);
	CHECK(GameLocalization::GetPreview("menu.quit") == "Quit");
	CHECK(GameLocalization::GetPreview("missing") == "missing");
	CHECK(GameLocalization::GetMissingPreviewKeyCount() == 1);
	CHECK_FALSE(GameLocalization::SetPreviewLanguage("unknown"));
	CHECK(GameLocalization::GetPreviewLanguage() == "de");
	folder.WriteLanguage("de", "Deutsch", R"({ "menu.play": "Jetzt spielen" })");
	REQUIRE(GameLocalization::Reload());
	system.OnPreRender(*scene);
	CHECK(text.GetDisplayText() == "Jetzt spielen");
	REQUIRE(GameLocalization::SetPreviewLanguage({}));
	system.OnPreRender(*scene);
	CHECK(text.GetDisplayText() == text.Text);
	CHECK_FALSE(text.PreviewText.has_value());
	REQUIRE(GameLocalization::SetPreviewLanguage("de"));
	system.OnPreRender(*scene);
	label.RemoveComponent<LocalizedTextComponent>();
	system.OnPreRender(*scene);
	CHECK_FALSE(text.PreviewText.has_value());
	LanguageFolder other;
	other.WriteEnglishAndGerman();
	REQUIRE(GameLocalization::Load(other.Directory));
	CHECK(GameLocalization::GetPreviewLanguage().empty());
	CHECK(GameLocalization::GetCurrentLanguage() == "en");
	REQUIRE(GameLocalization::SetPreviewLanguage("de"));
	GameLocalization::Unload();
	CHECK(GameLocalization::GetPreviewLanguage().empty());
	GameLocalization::SetSavedLanguageProvider(nullptr);
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
