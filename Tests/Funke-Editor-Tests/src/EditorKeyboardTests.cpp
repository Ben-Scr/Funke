#include <doctest/doctest.h>
#include "Gui/EditorKeyboard.hpp"

namespace {
	struct KeyboardFixture {
		ImGuiContext* Context = ImGui::CreateContext();
		int Value = 5;
		KeyboardFixture() {
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			io.DisplaySize = ImVec2(800, 600);
			io.DeltaTime = 1.0f / 60.0f;
		}
		~KeyboardFixture() { ImGui::DestroyContext(Context); }
		void Frame(bool focus = true, bool disabled = false) {
			ImGui::NewFrame();
			ImGui::Begin("Keyboard");
			if (focus) { ImGui::FocusWindow(ImGui::GetCurrentWindow()); ImGui::SetFocusID(ImGui::GetID("Slider"), ImGui::GetCurrentWindow()); }
			ImGui::BeginDisabled(disabled);
			Funke::EditorKeyboard::SliderInt("Slider", &Value, 0, 10, "%d", 0);
			ImGui::EndDisabled();
			ImGui::End();
			ImGui::EndFrame();
		}
		void Press(ImGuiKey key, bool disabled = false) {
			ImGui::GetIO().AddKeyEvent(key, true); Frame(true, disabled);
			ImGui::GetIO().AddKeyEvent(key, false); Frame(true, disabled);
		}
	};
}
TEST_CASE("Editor slider arrows and aliases apply one step and respect disabled state") {
	KeyboardFixture fixture;
	fixture.Frame(); fixture.Frame();
	fixture.Press(ImGuiKey_RightArrow);
	CHECK(fixture.Value == 6);
	fixture.Press(ImGuiKey_D);
	CHECK(fixture.Value == 7);
	fixture.Press(ImGuiKey_A);
	CHECK(fixture.Value == 6);
	fixture.Press(ImGuiKey_LeftArrow);
	CHECK(fixture.Value == 5);
	fixture.Press(ImGuiKey_D, true);
	CHECK(fixture.Value == 5);
	for (int i = 0; i < 12; ++i) fixture.Press(ImGuiKey_D);
	CHECK(fixture.Value == 10);
}

TEST_CASE("Editor dropdown aliases preview without committing and Escape cancels") {
	KeyboardFixture fixture;
	int selected = 0;
	bool open = false;
	auto frame = [&] {
		ImGui::NewFrame();
		ImGui::Begin("Dropdown");
		if (!open) { ImGui::FocusWindow(ImGui::GetCurrentWindow()); ImGui::SetFocusID(ImGui::GetID("Choice"), ImGui::GetCurrentWindow()); GImGui->NavCursorVisible = true; GImGui->NavInputSource = ImGuiInputSource_Keyboard; }
		open = Funke::EditorKeyboard::BeginCombo("Choice", selected == 0 ? "First" : "Last", 0);
		if (open) {
			for (int i = 0; i < 3; ++i) {
				ImGui::BeginDisabled(i == 1);
				if (ImGui::Selectable(i == 0 ? "First" : i == 1 ? "Disabled" : "Last", selected == i)) selected = i;
				if (selected == i) ImGui::SetItemDefaultFocus();
				ImGui::EndDisabled();
			}
			Funke::EditorKeyboard::EndCombo();
		}
		ImGui::End(); ImGui::EndFrame();
	};
	auto press = [&](ImGuiKey key) {
		ImGui::GetIO().AddKeyEvent(key, true); frame();
		ImGui::GetIO().AddKeyEvent(key, false); frame(); frame();
	};
	frame(); frame(); press(ImGuiKey_Enter);
	CHECK(open);
	press(ImGuiKey_A);
	CHECK(selected == 0);
	press(ImGuiKey_A); press(ImGuiKey_DownArrow);
	press(ImGuiKey_Enter);
	CHECK(selected == 2);
	CHECK_FALSE(open);
	press(ImGuiKey_Enter);
	press(ImGuiKey_W);
	CHECK(selected == 2);
	press(ImGuiKey_Escape);
	CHECK_FALSE(open);
	CHECK(selected == 2);
}
