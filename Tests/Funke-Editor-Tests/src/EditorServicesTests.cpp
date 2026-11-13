// EditorRestart's pending-reload bookkeeping and the JetBrains IDE detection in ExternalEditor.

#include <doctest/doctest.h>

#include "Editor/EditorRestart.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Editor/ExternalEditorProjectContext.hpp"
#include "Project/FunkeProject.hpp"
#include "Serialization/Path.hpp"
#include "Scripting/StructFieldBytes.hpp"
#include "Inspector/ListSelectionCache.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>

using namespace Funke;

namespace {
	struct ScriptSolutionFixture {
		std::filesystem::path Root = std::filesystem::temp_directory_path()
			/ ("FunkeScriptSolutions_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
			/ Path::FromUtf8("Spiel mit Leerzeichen \xc3\xa4");
		FunkeProject Project;
		std::filesystem::path NativeSolution = Root / "NativeScripts/build/Context_Game-NativeScripts.sln";

		ScriptSolutionFixture() {
			std::filesystem::create_directories(Root);
			Project.Name = "Context Game";
			Project.SlnPath = Path::ToUtf8(Root / "Context Game.sln");
			Project.NativeScriptsDir = Path::ToUtf8(Root / "NativeScripts");
			std::ofstream(Path::FromUtf8(Project.SlnPath)) << "managed solution";
		}

		void GenerateNativeSolution() const {
			std::filesystem::create_directories(NativeSolution.parent_path());
			std::ofstream(NativeSolution) << "native solution";
		}

		~ScriptSolutionFixture() {
			std::error_code error;
			std::filesystem::remove_all(Root.parent_path(), error);
		}
	};
}

TEST_CASE("External editor keeps native scripts out of the managed solution during generation") {
	ScriptSolutionFixture fixture;
	const auto expectedNative = Path::ToUtf8(std::filesystem::weakly_canonical(fixture.NativeSolution));
	const auto expectedManaged = Path::ToUtf8(std::filesystem::canonical(Path::FromUtf8(fixture.Project.SlnPath)));
	for (bool generated : { false, true }) {
		if (generated) fixture.GenerateNativeSolution();
		for (const char* extension : { ".cpp", ".cc", ".cxx", ".c", ".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".CPP", ".HPP" }) {
			CAPTURE(generated);
			CAPTURE(extension);
			CHECK(ExternalEditorDetail::ResolveProjectContext(&fixture.Project, "Assets/Scripts/Player" + std::string(extension)) == expectedNative);
		}
		CHECK(ExternalEditorDetail::ResolveProjectContext(&fixture.Project, "Assets/Scripts/Player.cs") == expectedManaged);
	}
	CHECK(ExternalEditorDetail::ResolveProjectContext(nullptr, "Player.cpp").empty());
	fixture.Project.NativeScriptsDir.clear();
	CHECK(ExternalEditorDetail::ResolveProjectContext(&fixture.Project, "Player.cpp").empty());
}

TEST_CASE("External editor waits for the native solution while the managed solution already exists") {
	using namespace ExternalEditorDetail;
	using namespace std::chrono_literals;
	ScriptSolutionFixture fixture;
	const auto solution = ResolveProjectContext(&fixture.Project, "Player.cpp");
	CHECK(WaitForProjectContext(solution, {}, 0ms) == ProjectContextStatus::TimedOut);
	auto pending = std::async(std::launch::async, [&] { return WaitForProjectContext(solution, {}, 5s); });
	CHECK(pending.wait_for(30ms) == std::future_status::timeout);
	fixture.GenerateNativeSolution();
	CHECK(pending.get() == ProjectContextStatus::Ready);
	CHECK(WaitForProjectContext(solution, {}, 0ms) == ProjectContextStatus::Ready);
}

TEST_CASE("External editor cancels pending solution launches on shutdown") {
	using namespace ExternalEditorDetail;
	using namespace std::chrono_literals;
	ScriptSolutionFixture fixture;
	const auto solution = ResolveProjectContext(&fixture.Project, "Player.cpp");
	std::stop_source stop;
	auto pending = std::async(std::launch::async, [&] { return WaitForProjectContext(solution, stop.get_token(), 5s); });
	CHECK(pending.wait_for(30ms) == std::future_status::timeout);
	stop.request_stop();
	CHECK(pending.get() == ProjectContextStatus::Cancelled);
	fixture.GenerateNativeSolution();
	CHECK(WaitForProjectContext(solution, stop.get_token(), 0ms) == ProjectContextStatus::Cancelled);
	CHECK(WaitForProjectContext("", {}, 0ms) == ProjectContextStatus::Ready);
}

TEST_CASE("EditorRestart keeps one line per key and prompts only on real changes") {
	EditorRestart::Clear();
	EditorRestart::Request("module:Funke.2D", "Engine module removed: Funke.2D");
	CHECK(EditorRestart::HasPending());
	CHECK(EditorRestart::ConsumePromptRequest());
	CHECK_FALSE(EditorRestart::ConsumePromptRequest());

	// Re-requesting the same reason every frame must not reopen the prompt after "Later".
	EditorRestart::Request("module:Funke.2D", "Engine module removed: Funke.2D");
	CHECK_FALSE(EditorRestart::ConsumePromptRequest());
	CHECK(EditorRestart::GetPending().size() == 1);
	CHECK(EditorRestart::GetPendingKind() == EditorRestart::Kind::Reload);

	EditorRestart::Request("render-backend", "Rendering API: Vulkan -> Direct3D12", EditorRestart::Kind::Restart);
	CHECK(EditorRestart::ConsumePromptRequest());
	CHECK(EditorRestart::GetPendingKind() == EditorRestart::Kind::Restart);

	EditorRestart::Withdraw("render-backend");
	CHECK(EditorRestart::GetPendingKind() == EditorRestart::Kind::Reload);
	EditorRestart::Withdraw("module:Funke.2D");
	CHECK_FALSE(EditorRestart::HasPending());
	EditorRestart::Clear();
}

TEST_CASE("C# struct component fields read and write in place in their raw bytes") {
	struct Stats { bool Alive; float Hp; uint8_t Kind; int64_t Score; float VelX, VelY; int16_t Level; };
	Stats stats{ true, 12.5f, 2, -7, 1.0f, -2.5f, 3 };
	auto* bytes = reinterpret_cast<uint8_t*>(&stats);
	const std::size_t size = sizeof(stats);

	CHECK(ReadStructField(bytes, size, { "bool", offsetof(Stats, Alive), 1 }) == "true");
	CHECK(ReadStructField(bytes, size, { "float", offsetof(Stats, Hp), 4 }) == "12.5");
	CHECK(ReadStructField(bytes, size, { "enum", offsetof(Stats, Kind), 1 }) == "2");
	CHECK(ReadStructField(bytes, size, { "long", offsetof(Stats, Score), 8 }) == "-7");
	CHECK(ReadStructField(bytes, size, { "vector2", offsetof(Stats, VelX), 8 }) == "1,-2.5");

	CHECK(WriteStructField(bytes, size, { "float", offsetof(Stats, Hp), 4 }, "99.25"));
	CHECK(stats.Hp == 99.25f);
	CHECK(WriteStructField(bytes, size, { "vector2", offsetof(Stats, VelX), 8 }, "3, 4"));
	CHECK(stats.VelX == 3.0f);
	CHECK(stats.VelY == 4.0f);
	CHECK(WriteStructField(bytes, size, { "bool", offsetof(Stats, Alive), 1 }, "false"));
	CHECK_FALSE(stats.Alive);
	CHECK(WriteStructField(bytes, size, { "short", offsetof(Stats, Level), 2 }, "-12"));
	CHECK(stats.Level == -12);

	// A bad value, a wrong size or a slot past the end leaves the bytes alone.
	CHECK_FALSE(WriteStructField(bytes, size, { "vector2", offsetof(Stats, VelX), 8 }, "5,abc"));
	CHECK(stats.VelX == 3.0f);
	CHECK_FALSE(WriteStructField(bytes, size, { "float", offsetof(Stats, Hp), 8 }, "1"));
	CHECK_FALSE(WriteStructField(bytes, size, { "long", static_cast<uint32_t>(size - 4), 8 }, "1"));
	CHECK_FALSE(ReadStructField(bytes, size, { "string", 0, 8 }).has_value());
}

TEST_CASE("Retained struct field edits follow current field names after layout changes") {
	std::array<int32_t, 2> values{ 11, 22 };
	auto* bytes = reinterpret_cast<uint8_t*>(values.data());
	const std::string before = R"([{"name":"Speed","type":"int","offset":0,"size":4},{"name":"Armor","type":"int","offset":4,"size":4}])";
	CHECK(WriteStructFieldByName(bytes, sizeof(values), before, "Speed", "int", "7"));
	CHECK(values[0] == 7);
	CHECK(values[1] == 22);

	values = { 22, 7 };
	const std::string after = R"([{"name":"Armor","type":"int","offset":0,"size":4},{"name":"Speed","type":"int","offset":4,"size":4}])";
	CHECK(WriteStructFieldByName(bytes, sizeof(values), after, "Speed", "int", "11"));
	CHECK(values[0] == 22);
	CHECK(values[1] == 11);
}

TEST_CASE("Retained struct field edits reject changed or missing schemas without touching bytes") {
	std::array<int32_t, 2> values{ 11, 22 };
	const auto original = values;
	auto* bytes = reinterpret_cast<uint8_t*>(values.data());
	for (const std::string_view fields : {
		R"([{"name":"Speed","type":"float","offset":0,"size":4}])",
		R"([{"name":"Renamed","type":"int","offset":0,"size":4}])",
		R"([{"name":"Speed","type":"int","offset":0,"size":4,"readOnly":true}])",
		R"([{"name":"Speed","type":"int","offset":8,"size":4}])",
		R"([{"name":"Speed","type":"int","offset":-1,"size":4}])",
		R"([])"
	}) {
		CAPTURE(fields);
		CHECK_FALSE(WriteStructFieldByName(bytes, sizeof(values), fields, "Speed", "int", "7"));
		CHECK(values == original);
	}
}

#ifdef FNK_PLATFORM_WINDOWS
namespace {
	void WriteFile(const std::filesystem::path& path, const std::string& text) {
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path) << text;
	}
}

TEST_CASE("JetBrains IDEs are found in Toolbox 2.x and 1.x installs with their product version") {
	const std::filesystem::path root = std::filesystem::temp_directory_path() / "FunkeExternalEditorTest";
	std::filesystem::remove_all(root);
	// Toolbox 2.x: %LOCALAPPDATA%\Programs\<product>
	WriteFile(root / "Programs" / "IntelliJ IDEA Ultimate" / "bin" / "idea64.exe", "");
	WriteFile(root / "Programs" / "IntelliJ IDEA Ultimate" / "product-info.json",
		R"({ "name": "IntelliJ IDEA", "version": "2099.1" })");
	// Toolbox 1.x: apps\<app>\ch-0\<build>
	WriteFile(root / "JetBrains" / "Toolbox" / "apps" / "Rider" / "ch-0" / "991.1" / "bin" / "rider64.exe", "");
	WriteFile(root / "JetBrains" / "Toolbox" / "apps" / "Rider" / "ch-0" / "991.1" / "product-info.json",
		R"({ "name": "JetBrains Rider", "version": "2099.2" })");

	char* previous = nullptr;
	size_t length = 0;
	_dupenv_s(&previous, &length, "LOCALAPPDATA");
	_putenv_s("LOCALAPPDATA", root.string().c_str());
	ExternalEditor::RefreshEditors();
	_putenv_s("LOCALAPPDATA", previous ? previous : "");
	free(previous);

	const auto& editors = ExternalEditor::GetAvailableEditors();
	auto find = [&](const char* name) {
		return std::find_if(editors.begin(), editors.end(), [name](const ExternalEditorInfo& e) { return e.DisplayName == name; });
	};
	const auto idea = find("IntelliJ IDEA 2099.1");
	REQUIRE(idea != editors.end());
	CHECK(idea->Type == ExternalEditorType::IntelliJ);
	const auto rider = find("JetBrains Rider 2099.2");
	REQUIRE(rider != editors.end());
	CHECK(rider->Type == ExternalEditorType::Rider);

	ExternalEditor::RefreshEditors();
	std::filesystem::remove_all(root);
}
#endif

TEST_CASE("Inspector list selection follows visible fields and releases abandoned selections") {
	PropertyDrawer::ListSelectionCache cache;
	cache.BeginFrame();
	auto& selected = cache.Get("scene1/entity1/field");
	selected.Selected.insert(7);
	selected.LastClicked = 7;
	cache.BeginFrame();
	CHECK(cache.Get("scene1/entity1/field").Selected.contains(7));
	CHECK(cache.Get("scene1/entity1/field").LastClicked == 7);
	CHECK(cache.Get("scene2/entity1/field").Selected.empty());
	CHECK(cache.Size() == 2);

	cache.BeginFrame();
	cache.Get("scene2/entity1/field");
	cache.BeginFrame();
	CHECK(cache.Size() == 1);
	CHECK(cache.Get("scene1/entity1/field").Selected.empty());
	CHECK(cache.Get("scene1/entity1/field").LastClicked == -1);

	for (int i = 0; i < 1000; ++i) {
		cache.BeginFrame();
		cache.Get("entity/" + std::to_string(i)).Selected.insert(i);
		CHECK(cache.Size() <= 3);
	}
	cache.BeginFrame();
	cache.BeginFrame();
	CHECK(cache.Size() == 0);
	cache.Get("final").Selected.insert(1);
	cache.Clear();
	CHECK(cache.Size() == 0);
}
