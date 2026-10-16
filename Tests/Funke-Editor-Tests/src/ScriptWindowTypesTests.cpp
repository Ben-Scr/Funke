#include <doctest/doctest.h>

#include "Editor/ScriptWindowTypes.hpp"

using namespace Funke;

TEST_CASE("C# editor window list fills in titles and menus and sorts by menu") {
	const auto types = ParseScriptWindowTypes(R"([
		{"type":"Game.Stats","title":"Stats","menu":"Game/Stats"},
		{"type":"Funke.AudioMixer.AudioMixerWindow","title":"Audio Mixer","menu":"Audio/Audio Mixer","width":320,"height":330},
		{"type":"Game.Tools.Untitled"},
		{"title":"no type"},
		"junk"
	])");
	REQUIRE(types.size() == 3);
	CHECK(types[0].Menu == "Audio/Audio Mixer");
	CHECK(types[0].Width == doctest::Approx(320.0f));
	CHECK(types[0].Height == doctest::Approx(330.0f));
	CHECK(types[1].Width == 0.0f);
	CHECK(types[1].Menu == "Game/Stats");
	CHECK(types[2].Type == "Game.Tools.Untitled");
	CHECK(types[2].Title == "Untitled");
	CHECK(types[2].Menu == "Untitled");

	CHECK(ParseScriptWindowTypes("not json").empty());
	CHECK(ParseScriptWindowTypes("{}").empty());
}

TEST_CASE("Menu paths split on slashes and ignore empty parts") {
	CHECK(SplitMenuPath("Audio/Audio Mixer") == std::vector<std::string>{ "Audio", "Audio Mixer" });
	CHECK(SplitMenuPath("/Audio//Mixer/") == std::vector<std::string>{ "Audio", "Mixer" });
	CHECK(SplitMenuPath("Single") == std::vector<std::string>{ "Single" });
	CHECK(SplitMenuPath("").empty());
	CHECK(SplitMenuPath("///").empty());
}
