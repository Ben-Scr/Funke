#include <doctest/doctest.h>

#include "Gui/EditorDockLayout.hpp"

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
