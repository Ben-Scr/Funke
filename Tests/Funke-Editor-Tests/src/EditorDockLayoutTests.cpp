#include <doctest/doctest.h>

#include "Gui/EditorDockLayout.hpp"
#include "Gui/EditorShortcuts.hpp"
#include "Gui/EditorWindowIcons.hpp"
#include "Gui/EditorMenu.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>
#include <string>

using namespace Funke;

namespace {
	constexpr const char* k_Panels[] = { "Entities", "Inspector", "Editor View", "Game View", "Project", "Log" };

	struct HeadlessImGui {
		HeadlessImGui() {
			Context = ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
			io.DisplaySize = ImVec2(1920.0f, 1080.0f);
			io.DeltaTime = 1.0f / 60.0f;
		}
		~HeadlessImGui() { ImGui::DestroyContext(Context); }

		// Mirrors the editor frame: dockspace host first, then the panels.
		ImGuiID Frame(const char* dockspaceName, const std::function<void(ImGuiID, ImVec2)>& beforeDockspace = {}) {
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
			ImGui::Begin("Host", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
			const ImGuiID dockspaceId = ImGui::GetID(dockspaceName);
			if (beforeDockspace) beforeDockspace(dockspaceId, ImGui::GetContentRegionAvail());
			ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
			ImGui::End();
			for (const char* name : k_Panels) {
				ImGui::Begin(name);
				ImGui::End();
			}
			ImGui::EndFrame();
			return dockspaceId;
		}

		ImGuiContext* Context = nullptr;
	};

	ImGuiDockNode* NodeOf(const char* windowName) {
		const ImGuiWindow* window = ImGui::FindWindowByName(windowName);
		return window != nullptr ? window->DockNode : nullptr;
	}

	void BuildDefaultWhenUndocked(ImGuiID dockspaceId, ImVec2 size) {
		if (!EditorDockLayout::HasDockedPanels(dockspaceId)) EditorDockLayout::BuildDefault(dockspaceId, size);
	}
}

TEST_CASE("EditorDockLayout builds the default arrangement into an empty dockspace") {
	HeadlessImGui imgui;
	ImGuiID dockspaceId = imgui.Frame("Dockspace", BuildDefaultWhenUndocked);
	imgui.Frame("Dockspace");
	CHECK(EditorDockLayout::HasDockedPanels(dockspaceId));

	for (const char* name : k_Panels) {
		CAPTURE(std::string(name));
		ImGuiDockNode* node = NodeOf(name);
		REQUIRE(node != nullptr);
		CHECK(ImGui::DockNodeGetRootNode(node)->ID == dockspaceId);
	}

	ImGuiDockNode* views = NodeOf("Editor View");
	CHECK(views == NodeOf("Game View"));
	CHECK(views->IsCentralNode());
	CHECK(NodeOf("Entities")->Pos.x < views->Pos.x);
	CHECK(NodeOf("Inspector")->Pos.x > views->Pos.x);
	CHECK(NodeOf("Project")->Pos.y > views->Pos.y);
	CHECK(NodeOf("Log")->Pos.x > NodeOf("Project")->Pos.x);
	CHECK(NodeOf("Entities")->Size.x > 100.0f);
	CHECK(NodeOf("Inspector")->Size.x > 100.0f);
}

TEST_CASE("EditorDockLayout preview reads a saved layout without changing the live context") {
	HeadlessImGui imgui;
	const ImGuiID dockspaceId = imgui.Frame("Dockspace", BuildDefaultWhenUndocked);
	imgui.Frame("Dockspace");
	ImGuiDockNode* views = NodeOf("Editor View");
	const std::string ini = ImGui::SaveIniSettingsToMemory();
	const auto preview = EditorDockLayout::ParsePreview(ini);
	CHECK(preview.Panes.size() == 5);
	CHECK(preview.Size.x > 0.0f);
	CHECK(preview.Size.y > 0.0f);
	float area = 0.0f;
	int central = 0;
	for (const auto& pane : preview.Panes) {
		CHECK(pane.Position.x >= 0.0f);
		CHECK(pane.Position.y >= 0.0f);
		CHECK(pane.Position.x + pane.Size.x <= 1.00001f);
		CHECK(pane.Position.y + pane.Size.y <= 1.00001f);
		area += pane.Size.x * pane.Size.y;
		central += pane.Central ? 1 : 0;
	}
	CHECK(area == doctest::Approx(1.0f));
	CHECK(central == 1);
	CHECK(ImGui::GetCurrentContext() == imgui.Context);
	CHECK(NodeOf("Editor View") == views);
	CHECK(EditorDockLayout::HasDockedPanels(dockspaceId));
	CHECK(std::string(ImGui::SaveIniSettingsToMemory()) == ini);
}

TEST_CASE("EditorDockLayout preview respects nested split ratios and ignores other settings") {
	const auto preview = EditorDockLayout::ParsePreview(
		"[Window][Inspector]\r\nPos=42,24\r\nSize=500,500\r\n"
		"[Docking][Data]\r\n"
		"DockSpace ID=0x10 Pos=-100,50 Size=1000,600 Split=X\r\n"
		"  DockNode ID=0x11 Parent=0x10 SizeRef=200,600\r\n"
		"  DockNode ID=0x12 Parent=0x10 SizeRef=800,600 Split=Y\r\n"
		"    DockNode ID=0x13 Parent=0x12 SizeRef=800,450 CentralNode=1\r\n"
		"    DockNode ID=0x14 Parent=0x12 SizeRef=800,150\r\n"
		"DockNode ID=0x20 Pos=0,0 Size=300,200\r\n"
		"[Table][Other]\r\nDockNode ID=bad\r\n");
	REQUIRE(preview.Panes.size() == 3);
	CHECK(preview.Size.x == 1000.0f);
	CHECK(preview.Panes[0].Size.x == doctest::Approx(0.2f));
	CHECK(preview.Panes[1].Position.x == doctest::Approx(0.2f));
	CHECK(preview.Panes[1].Size.y == doctest::Approx(0.75f));
	CHECK(preview.Panes[1].Central);
	CHECK(preview.Panes[2].Position.y == doctest::Approx(0.75f));
	CHECK(preview.Panes[2].Size.y == doctest::Approx(0.25f));
}

TEST_CASE("EditorDockLayout preview handles missing and malformed docking data") {
	for (const std::string& data : {
		std::string(), std::string("[Window][Log]\nSize=200,100\n"),
		std::string("[Docking][Data]\nDockSpace ID=0x1 Size=0,600\n"),
		std::string("[Docking][Data]\nDockSpace ID=0x1 Size=1000,600 Split=X\n"),
		std::string("[Docking][Data]\nDockSpace ID=0x1 Size=1000,600 Split=X\nDockNode ID=0x2 Parent=0x1 SizeRef=200,600\n"),
		std::string("[Docking][Data]\nDockSpace ID=0x1 Size=1000,600\nDockNode ID=0x1 Parent=0x1 SizeRef=200,600\n"),
		std::string("[Docking][Data]\nDockNode ID=0x1 Parent=0x2 SizeRef=200,600\n"),
		std::string("[Docking][Data]\nDockSpace ID=0x1 Size=999999999999999999999999,600\n") }) {
		CAPTURE(data);
		CHECK(EditorDockLayout::ParsePreview(data).Panes.empty());
	}
}

TEST_CASE("EditorDockLayout reset re-docks panels that already exist") {
	HeadlessImGui imgui;
	const ImGuiID dockspaceId = imgui.Frame("Dockspace", BuildDefaultWhenUndocked);
	imgui.Frame("Dockspace");
	ImGui::DockContextQueueUndockWindow(ImGui::GetCurrentContext(), ImGui::FindWindowByName("Inspector"));
	imgui.Frame("Dockspace");
	REQUIRE(NodeOf("Inspector") == nullptr);

	imgui.Frame("Dockspace", [](ImGuiID id, ImVec2 size) { EditorDockLayout::BuildDefault(id, size); });
	imgui.Frame("Dockspace");
	for (const char* name : k_Panels) {
		CAPTURE(std::string(name));
		ImGuiDockNode* node = NodeOf(name);
		REQUIRE(node != nullptr);
		CHECK(ImGui::DockNodeGetRootNode(node)->ID == dockspaceId);
	}
	CHECK(NodeOf("Inspector")->Pos.x > NodeOf("Editor View")->Pos.x);
}

TEST_CASE("EditorDockLayout ignores panels docked into another dockspace") {
	HeadlessImGui imgui;
	const ImGuiID oldId = imgui.Frame("OldDockspace", [](ImGuiID id, ImVec2 size) { EditorDockLayout::BuildDefault(id, size); });
	imgui.Frame("OldDockspace");
	CHECK(EditorDockLayout::HasDockedPanels(oldId));
	CHECK_FALSE(EditorDockLayout::HasDockedPanels(ImHashStr("NewDockspace", 0, ImHashStr("Host"))));
}

TEST_CASE("EditorDockLayout sees saved docking before the panels exist") {
	std::string ini;
	ImGuiID dockspaceId = 0;
	{
		HeadlessImGui imgui;
		dockspaceId = imgui.Frame("Dockspace", BuildDefaultWhenUndocked);
		imgui.Frame("Dockspace");
		ini = ImGui::SaveIniSettingsToMemory();
	}

	HeadlessImGui imgui;
	CHECK_FALSE(EditorDockLayout::HasDockedPanels(dockspaceId));
	ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());
	CHECK(EditorDockLayout::HasDockedPanels(dockspaceId));
}

TEST_CASE("Editor window icons preserve existing layout IDs and saved docking") {
	std::string ini;
	ImGuiID dockspaceId = 0;
	ImGuiID logDockId = 0;
	{
		HeadlessImGui imgui;
		dockspaceId = imgui.Frame("Dockspace", BuildDefaultWhenUndocked);
		imgui.Frame("Dockspace");
		logDockId = NodeOf("Log")->ID;
		ini = ImGui::SaveIniSettingsToMemory();
	}

	HeadlessImGui imgui;
	ImGui::GetPlatformIO().WindowTitleIcon = &EditorWindowIcons::Draw;
	ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());
	imgui.Frame("Dockspace");
	imgui.Frame("Dockspace");
	CHECK(EditorDockLayout::HasDockedPanels(dockspaceId));
	REQUIRE(NodeOf("Log") != nullptr);
	CHECK(NodeOf("Log")->ID == logDockId);
	for (const char* name : k_Panels) {
		CAPTURE(std::string(name));
		REQUIRE(ImGui::FindWindowByName(name) != nullptr);
		CHECK(ImGui::FindWindowByName(name)->ID == ImHashStr(name));
		CHECK(EditorWindowIcons::ForWindow(name) != EditorWindowIcons::Type::None);
	}
	const std::string saved = ImGui::SaveIniSettingsToMemory();
	CHECK(saved.find("[Window][Log]") != std::string::npos);
}

TEST_CASE("Editor window icon spacing scales without changing ordinary tabs") {
	HeadlessImGui imgui;
	ImGui::GetPlatformIO().WindowTitleIcon = &EditorWindowIcons::Draw;
	for (float fontSize : { 11.25f, 15.0f, 30.0f }) {
		ImGui::GetStyle().FontSizeBase = fontSize;
		ImGui::NewFrame();
		ImGui::Begin("Host");
		const float plainWidth = ImGui::TabItemCalcSize("Log", false).x;
		const float iconWidth = ImGui::WindowTitleIcon("Log");
		CHECK(iconWidth == doctest::Approx(ImGui::GetFontSize() * 1.3f));
		CHECK(ImGui::TabItemCalcSize("Log", false, true).x == doctest::Approx(plainWidth + iconWidth));
		CHECK(ImGui::TabItemCalcSize("Unknown", false, true).x == ImGui::TabItemCalcSize("Unknown", false).x);
		ImGui::GetPlatformIO().WindowTitleIcon = nullptr;
		CHECK(ImGui::TabItemCalcSize("Log", false, true).x == plainWidth);
		ImGui::GetPlatformIO().WindowTitleIcon = &EditorWindowIcons::Draw;
		ImGui::End();
		ImGui::EndFrame();
	}
}

TEST_CASE("Editor window icons support script window IDs and stay context local") {
	HeadlessImGui imgui;
	ImGui::GetPlatformIO().WindowTitleIcon = &EditorWindowIcons::Draw;
	CHECK(EditorWindowIcons::ForWindow("My Tool###ScriptWindow_My.Tools.Tool") == EditorWindowIcons::Type::Script);
	ImGuiContext* other = ImGui::CreateContext();
	ImGui::SetCurrentContext(other);
	CHECK(ImGui::GetPlatformIO().WindowTitleIcon == nullptr);
	ImGui::DestroyContext(other);
	ImGui::SetCurrentContext(imgui.Context);
	CHECK(ImGui::GetPlatformIO().WindowTitleIcon == &EditorWindowIcons::Draw);
}

TEST_CASE("Editor window icons use generated textures without loading during measurement") {
	HeadlessImGui imgui;
	static std::string requested;
	static int requestedSize = 0;
	static int requests = 0;
	requests = 0;
	struct ResetProvider {
		~ResetProvider() { EditorWindowIcons::SetTextureProvider(nullptr); }
	} resetProvider;
	EditorWindowIcons::SetTextureProvider([](const char* name, int size) -> ImTextureID {
		requested = name;
		requestedSize = size;
		++requests;
		return 42;
	});
	CHECK(EditorWindowIcons::Draw("Log", nullptr, ImVec2(), 15.5f, 0) == doctest::Approx(20.15f));
	CHECK(requests == 0);
	ImGui::NewFrame();
	ImGui::Begin("Icon textures");
	ImDrawList* list = ImGui::GetWindowDrawList();
	const int firstVertex = list->VtxBuffer.Size;
	const ImU32 tint = IM_COL32(80, 120, 160, 90);
	EditorWindowIcons::Draw("Log", list, ImVec2(20, 30), 15.5f, tint);
	CHECK(requests == 1);
	CHECK(requested == "ui_log");
	CHECK(requestedSize == 16);
	REQUIRE(list->VtxBuffer.Size == firstVertex + 4);
	CHECK(list->VtxBuffer[firstVertex].col == tint);
	CHECK(list->VtxBuffer[firstVertex].uv.y == 1.0f);
	CHECK(list->VtxBuffer[firstVertex + 2].uv.y == 0.0f);
	CHECK(list->VtxBuffer[firstVertex + 2].pos.x == doctest::Approx(35.5f));
	EditorWindowIcons::SetTextureProvider(nullptr);
	CHECK(EditorWindowIcons::Draw("Log", list, ImVec2(), 15.0f, tint) == doctest::Approx(19.5f));
	CHECK(list->VtxBuffer.Size == firstVertex + 4);
	CHECK(EditorWindowIcons::Draw("Unknown", list, ImVec2(), 15.0f, tint) == 0.0f);
	ImGui::End();
	ImGui::EndFrame();
}

TEST_CASE("Editor menu icons preserve native selection disabled state and item IDs") {
	HeadlessImGui imgui;
	bool selected = false;
	bool enabled = true;
	bool pressed = false;
	ImVec2 itemCenter;
	ImGuiID itemId = 0;
	const auto frame = [&]() {
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(20, 20));
		ImGui::SetNextWindowSize(ImVec2(300, 150));
		ImGui::Begin("Menu test", nullptr, ImGuiWindowFlags_NoTitleBar);
		pressed = EditorMenu::Item(EditorMenu::Icon::Profiler, "Profiler", "Ctrl+F6", &selected, enabled);
		itemCenter = GImGui->LastItemData.Rect.GetCenter();
		itemId = ImGui::GetItemID();
		CHECK(itemId == ImGui::GetID("Profiler"));
		CHECK(ImGui::GetCurrentWindow()->DC.MenuColumns.Widths[0] >= ImGui::GetFontSize());
		ImGui::End();
		ImGui::EndFrame();
	};
	frame(); frame();
	const ImGuiID originalId = itemId;
	ImGui::GetIO().AddMousePosEvent(itemCenter.x, itemCenter.y);
	frame();
	ImGui::GetIO().AddMouseButtonEvent(0, true);
	frame();
	ImGui::GetIO().AddMouseButtonEvent(0, false);
	frame();
	CHECK(pressed);
	CHECK(selected);
	CHECK(itemId == originalId);
	enabled = false;
	frame();
	ImGui::GetIO().AddMouseButtonEvent(0, true);
	frame();
	ImGui::GetIO().AddMouseButtonEvent(0, false);
	frame();
	CHECK_FALSE(pressed);
	CHECK(selected);
}

TEST_CASE("Editor menu icons draw submenu glyphs in the parent window") {
	HeadlessImGui imgui;
	bool opened = false;
	for (int i = 0; i < 3; ++i) {
		ImGui::NewFrame();
		ImGui::Begin("Menu parent");
		ImGuiWindow* parent = ImGui::GetCurrentWindow();
		ImGui::OpenPopup("Create");
		if (EditorMenu::Begin(EditorMenu::Icon::Add, "Create")) {
			opened = true;
			CHECK(ImGui::GetCurrentWindow() != parent);
			CHECK(parent->DrawList->VtxBuffer.Size > 0);
			EditorMenu::Item(EditorMenu::Icon::File, "File");
			ImGui::EndMenu();
		}
		ImGui::End();
		ImGui::EndFrame();
	}
	CHECK(opened);
}

TEST_CASE("EditorShortcuts use Command for ImGui shortcuts on macOS") {
	HeadlessImGui imgui;
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigMacOSXBehaviors = true;
	bool command = true;
	SUBCASE("Physical Command activates the primary shortcut modifier") {}
	SUBCASE("Physical Control does not activate Command shortcuts") { command = false; }
	io.AddKeyEvent(command ? ImGuiMod_Super : ImGuiMod_Ctrl, true);
	io.AddKeyEvent(command ? ImGuiKey_LeftSuper : ImGuiKey_LeftCtrl, true);
	io.AddKeyEvent(ImGuiKey_S, true);
	imgui.Frame("Shortcuts");
	CHECK(io.KeyCtrl == command);
	CHECK(io.KeySuper == !command);
	CHECK(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) == command);
}

TEST_CASE("EditorShortcuts distinguish rename open delete and navigation") {
	struct KeyCase {
		ImGuiKey Key;
		ImGuiKey Mod;
		bool Rename = false;
		bool Open = false;
		bool Delete = false;
		bool NavigateUp = false;
		bool Profiler = false;
		bool Shift = false;
	};
#ifdef FNK_PLATFORM_MACOS
	constexpr KeyCase cases[] = {
		{ ImGuiKey_Enter, ImGuiKey_None, true },
		{ ImGuiKey_KeypadEnter, ImGuiKey_None, true },
		{ ImGuiKey_DownArrow, ImGuiMod_Super, false, true },
		{ ImGuiKey_Backspace, ImGuiMod_Super, false, false, true },
		{ ImGuiKey_UpArrow, ImGuiMod_Super, false, false, false, true },
		{ ImGuiKey_P, ImGuiMod_Super, false, false, false, false, true, true },
		{ ImGuiKey_Enter, ImGuiMod_Super },
		{ ImGuiKey_Backspace, ImGuiKey_None },
		{ ImGuiKey_Backspace, ImGuiMod_Ctrl },
		{ ImGuiKey_F2, ImGuiKey_None },
		{ ImGuiKey_Delete, ImGuiKey_None, false, false, true },
		{ ImGuiKey_KeypadDecimal, ImGuiKey_None, false, false, true },
	};
#else
	constexpr KeyCase cases[] = {
		{ ImGuiKey_F2, ImGuiKey_None, true },
		{ ImGuiKey_Enter, ImGuiKey_None, false, true },
		{ ImGuiKey_KeypadEnter, ImGuiKey_None, false, true },
		{ ImGuiKey_Delete, ImGuiKey_None, false, false, true },
		{ ImGuiKey_KeypadDecimal, ImGuiKey_None, false, false, true },
		{ ImGuiKey_Backspace, ImGuiKey_None, false, false, false, true },
		{ ImGuiKey_F6, ImGuiMod_Ctrl, false, false, false, false, true },
		{ ImGuiKey_Enter, ImGuiMod_Ctrl },
		{ ImGuiKey_F2, ImGuiMod_Shift },
		{ ImGuiKey_Delete, ImGuiMod_Ctrl },
	};
#endif
	for (const KeyCase& keyCase : cases) {
		CAPTURE(static_cast<int>(keyCase.Key));
		CAPTURE(static_cast<int>(keyCase.Mod));
		CAPTURE(keyCase.Shift);
		HeadlessImGui imgui;
		ImGuiIO& io = ImGui::GetIO();
#ifdef FNK_PLATFORM_MACOS
		io.ConfigMacOSXBehaviors = true;
#else
		io.ConfigMacOSXBehaviors = false;
#endif
		if (keyCase.Mod != ImGuiKey_None) io.AddKeyEvent(keyCase.Mod, true);
		if (keyCase.Shift) io.AddKeyEvent(ImGuiMod_Shift, true);
		io.AddKeyEvent(keyCase.Key, true);
		imgui.Frame("Shortcuts");
		CHECK(EditorShortcuts::IsRenamePressed() == keyCase.Rename);
		CHECK(EditorShortcuts::IsOpenAssetPressed() == keyCase.Open);
		CHECK(EditorShortcuts::IsDeletePressed() == keyCase.Delete);
		CHECK(ImGui::IsKeyChordPressed(EditorShortcuts::k_NavigateUp) == keyCase.NavigateUp);
		CHECK(ImGui::IsKeyChordPressed(EditorShortcuts::k_Profiler) == keyCase.Profiler);
		io.DeltaTime = io.KeyRepeatDelay + io.KeyRepeatRate;
		imgui.Frame("Shortcuts");
		CHECK(ImGui::IsKeyPressed(keyCase.Key, true));
		CHECK_FALSE(EditorShortcuts::IsRenamePressed());
		CHECK_FALSE(EditorShortcuts::IsOpenAssetPressed());
		CHECK_FALSE(EditorShortcuts::IsDeletePressed());
	}
}

TEST_CASE("EditorShortcuts rename opens an input before Enter commits the new name") {
	HeadlessImGui imgui;
	ImGuiIO& io = ImGui::GetIO();
#ifdef FNK_PLATFORM_MACOS
	io.ConfigMacOSXBehaviors = true;
#else
	io.ConfigMacOSXBehaviors = false;
#endif
	char name[64] = "Old Name";
	bool renaming = false;
	bool active = false;
	auto frame = [&]() {
		ImGui::NewFrame();
		ImGui::Begin("Rename");
		if (!renaming && !io.WantTextInput && !ImGui::IsAnyItemActive()
			&& EditorShortcuts::IsRenamePressed()) {
			renaming = true;
			ImGui::SetKeyboardFocusHere();
		}
		bool committed = false;
		if (renaming) {
			committed = ImGui::InputText("##name", name, sizeof(name),
				ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
			active = ImGui::IsItemActive();
		}
		ImGui::End();
		ImGui::EndFrame();
		return committed;
	};
	frame();
	io.AddKeyEvent(static_cast<ImGuiKey>(EditorShortcuts::k_Rename), true);
	CHECK_FALSE(frame());
	REQUIRE(renaming);
	io.AddKeyEvent(static_cast<ImGuiKey>(EditorShortcuts::k_Rename), false);
	CHECK_FALSE(frame());
	REQUIRE(active);
	io.AddInputCharactersUTF8("New Name");
	CHECK_FALSE(frame());
	CHECK(std::string(name) == "New Name");
	io.AddKeyEvent(ImGuiKey_Enter, true);
	CHECK(frame());
}

TEST_CASE("EditorShortcuts undo is available from every panel and an undocked tool window") {
	for (const char* name : { "Editor View", "Entities", "Inspector", "Project", "Log", "Floating Tool" }) {
		CAPTURE(std::string(name));
		HeadlessImGui imgui;
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigMacOSXBehaviors = false;
		imgui.Frame("History");
		ImGui::NewFrame();
		ImGui::Begin(name);
		ImGui::SetWindowFocus();
		ImGui::End();
		ImGui::EndFrame();
		io.AddKeyEvent(ImGuiMod_Ctrl, true);
		io.AddKeyEvent(ImGuiKey_Z, true);
		ImGui::NewFrame();
		CHECK(EditorShortcuts::ReadHistoryAction(false) == EditorShortcuts::HistoryAction::Undo);
		CHECK(EditorShortcuts::ReadHistoryAction(true) == EditorShortcuts::HistoryAction::None);
		io.WantTextInput = true;
		CHECK(EditorShortcuts::ReadHistoryAction(false) == EditorShortcuts::HistoryAction::None);
		io.WantTextInput = false;
		ImGui::SetActiveID(42, ImGui::FindWindowByName(name));
		CHECK(EditorShortcuts::ReadHistoryAction(false) == EditorShortcuts::HistoryAction::None);
		ImGui::ClearActiveID();
		io.AppFocusLost = true;
		CHECK(EditorShortcuts::ReadHistoryAction(false) == EditorShortcuts::HistoryAction::None);
		ImGui::EndFrame();
	}
}

TEST_CASE("EditorShortcuts history uses Command on macOS and both redo chords without key repeat") {
	for (bool mac : { false, true }) {
		for (bool shift : { false, true }) {
			CAPTURE(mac);
			CAPTURE(shift);
			HeadlessImGui imgui;
			ImGuiIO& io = ImGui::GetIO();
			io.ConfigMacOSXBehaviors = mac;
			io.AddKeyEvent(mac ? ImGuiMod_Super : ImGuiMod_Ctrl, true);
			if (shift) io.AddKeyEvent(ImGuiMod_Shift, true);
			io.AddKeyEvent(shift ? ImGuiKey_Z : ImGuiKey_Y, true);
			imgui.Frame("History");
			CHECK(EditorShortcuts::ReadHistoryAction(false) == EditorShortcuts::HistoryAction::Redo);
			io.DeltaTime = io.KeyRepeatDelay + io.KeyRepeatRate;
			imgui.Frame("History");
			CHECK(EditorShortcuts::ReadHistoryAction(false) == EditorShortcuts::HistoryAction::None);
		}
	}
}
