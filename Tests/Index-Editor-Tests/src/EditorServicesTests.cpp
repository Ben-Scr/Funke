// EditorRestart's pending-reload bookkeeping and the JetBrains IDE detection in ExternalEditor.

#include <doctest/doctest.h>

#include "Editor/EditorRestart.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Scripting/StructFieldBytes.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Index;

TEST_CASE("EditorRestart keeps one line per key and prompts only on real changes") {
	EditorRestart::Clear();
	EditorRestart::Request("module:Index.2D", "Engine module removed: Index.2D");
	CHECK(EditorRestart::HasPending());
	CHECK(EditorRestart::ConsumePromptRequest());
	CHECK_FALSE(EditorRestart::ConsumePromptRequest());

	// Re-requesting the same reason every frame must not reopen the prompt after "Later".
	EditorRestart::Request("module:Index.2D", "Engine module removed: Index.2D");
	CHECK_FALSE(EditorRestart::ConsumePromptRequest());
	CHECK(EditorRestart::GetPending().size() == 1);
	CHECK(EditorRestart::GetPendingKind() == EditorRestart::Kind::Reload);

	EditorRestart::Request("render-backend", "Rendering API: Vulkan -> Direct3D12", EditorRestart::Kind::Restart);
	CHECK(EditorRestart::ConsumePromptRequest());
	CHECK(EditorRestart::GetPendingKind() == EditorRestart::Kind::Restart);

	EditorRestart::Withdraw("render-backend");
	CHECK(EditorRestart::GetPendingKind() == EditorRestart::Kind::Reload);
	EditorRestart::Withdraw("module:Index.2D");
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

#ifdef IDX_PLATFORM_WINDOWS
namespace {
	void WriteFile(const std::filesystem::path& path, const std::string& text) {
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path) << text;
	}
}

TEST_CASE("JetBrains IDEs are found in Toolbox 2.x and 1.x installs with their product version") {
	const std::filesystem::path root = std::filesystem::temp_directory_path() / "IndexExternalEditorTest";
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
