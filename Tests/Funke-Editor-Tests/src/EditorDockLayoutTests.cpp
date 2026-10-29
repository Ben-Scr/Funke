#include <doctest/doctest.h>

#include "Gui/EditorDockLayout.hpp"
#include "Gui/EditorShortcuts.hpp"

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
