#include <doctest/doctest.h>
#include "Gui/StringTableDocument.hpp"
#include "Undo/AssetFileHistory.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include <chrono>
#include <filesystem>

using namespace Funke;
namespace {
	struct Tables {
		std::filesystem::path Root = std::filesystem::temp_directory_path() / ("FnkLoc" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		AssetFileHistory History;
		Tables() { std::filesystem::create_directories(Root / "Localization"); History.Initialize(Root); }
		~Tables() { History.Clear(); std::error_code ec; if (Root.parent_path() == std::filesystem::temp_directory_path() && Root.filename().string().starts_with("FnkLoc")) std::filesystem::remove_all(Root, ec); }
		std::filesystem::path Path() const { return Root / "Localization/en.json"; }
	};
}
TEST_CASE("String table editing preserves metadata and uses reversible asset history") {
	Tables files;
	const std::string original = R"({"meta":{"code":"en","displayName":"English","custom":42},"custom":[1,2],"strings":{"hello":"Hello","empty":""}})";
	REQUIRE(File::WriteAllText(files.Path().string(), original));
	StringTableDocument doc; REQUIRE(doc.Open(files.Path()));
	REQUIRE(doc.Set("hello", "Hallo\nWelt")); REQUIRE(doc.Set("new", "Ω 中文"));
	CHECK_FALSE(doc.Rename("new", "hello")); REQUIRE(doc.Rename("new", "renamed"));
	REQUIRE(doc.Remove("empty")); REQUIRE(doc.Save(files.History)); CHECK_FALSE(doc.IsDirty());
	const auto saved = File::ReadAllText(files.Path().string());
	const auto root = Json::Parse(saved);
	CHECK(root.FindMember("meta")->FindMember("custom")->AsIntOr(0) == 42);
	CHECK(root.FindMember("custom")->GetArray().size() == 2);
	CHECK(root.FindMember("strings")->FindMember("renamed")->AsStringOr() == "Ω 中文");
	CHECK(files.History.Undo() == "Edit String Table"); CHECK(File::ReadAllText(files.Path().string()) == original);
	CHECK(files.History.Redo() == "Edit String Table"); CHECK(File::ReadAllText(files.Path().string()) == saved);
}
TEST_CASE("String table authoring refuses malformed duplicate and nonstring data") {
	Tables files;
	for (const auto* text : { "{", "[]", "{\"strings\":[]}", "{\"strings\":{\"x\":1}}", "{\"strings\":{\"x\":\"a\",\"\\u0078\":\"b\"}}", "{\"meta\":{\"code\":3},\"strings\":{}}" }) {
		REQUIRE(File::WriteAllText(files.Path().string(), text));
		StringTableDocument doc; CHECK_FALSE(doc.Open(files.Path())); CHECK_FALSE(doc.Set("x", "lost")); CHECK_FALSE(doc.Save(files.History));
		CHECK_FALSE(doc.Error().empty()); CHECK(File::ReadAllText(files.Path().string()) == text);
	}
}
TEST_CASE("String table disk conflicts retain local edits and refuse save") {
	Tables files; const std::string a = R"({"strings":{"x":"a"}})", b = R"({"strings":{"x":"b"}})";
	REQUIRE(File::WriteAllText(files.Path().string(), a)); StringTableDocument doc; REQUIRE(doc.Open(files.Path()));
	REQUIRE(doc.Set("x", "local")); REQUIRE(File::WriteAllText(files.Path().string(), b));
	CHECK_FALSE(doc.Refresh()); CHECK(doc.HasConflict()); CHECK_FALSE(doc.Save(files.History));
	CHECK(doc.Strings()->FindMember("x")->AsStringOr() == "local"); CHECK(File::ReadAllText(files.Path().string()) == b);
	REQUIRE(doc.Open(files.Path())); CHECK_FALSE(doc.HasConflict()); REQUIRE(File::WriteAllText(files.Path().string(), a));
	REQUIRE(doc.Refresh()); CHECK(doc.Strings()->FindMember("x")->AsStringOr() == "a");
}
TEST_CASE("New language files reject traversal collisions and disappearing baselines") {
	Tables files; StringTableDocument doc;
	CHECK_FALSE(doc.Create(files.Root / "Localization", "../bad", "Bad"));
	REQUIRE(doc.Create(files.Root / "Localization", "en", "English")); REQUIRE(doc.Set("key", "")); REQUIRE(doc.Save(files.History));
	CHECK_FALSE(doc.Create(files.Root / "Localization", "en", "English"));
	REQUIRE(doc.Open(files.Path())); REQUIRE(doc.Set("key", "value")); std::filesystem::remove(files.Path());
	CHECK_FALSE(doc.Save(files.History)); CHECK_FALSE(std::filesystem::exists(files.Path()));
}
