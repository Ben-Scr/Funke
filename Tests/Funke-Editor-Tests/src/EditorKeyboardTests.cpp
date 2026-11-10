#include <doctest/doctest.h>
#include "Gui/EditorKeyboard.hpp"
#include <string>
#include <vector>

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

TEST_CASE("Game View reserves Tab and activation while editor panels and popups retain navigation") {
	KeyboardFixture fixture;
	ImGuiWindow* game = nullptr;
	ImGuiWindow* toolbar = nullptr;
	ImGuiWindow* inspector = nullptr;
	ImGuiWindow* popup = nullptr;
	ImGuiID first = 0, second = 0, toolbarToggle = 0;
	bool toggled = false;
	bool openPopup = false;
	auto frame = [&] {
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(300, 400));
		ImGui::Begin("Inspector");
		inspector = ImGui::GetCurrentWindow();
		first = ImGui::GetID("First");
		second = ImGui::GetID("Second");
		ImGui::Button("First");
		ImGui::Button("Second");
		ImGui::End();
		ImGui::SetNextWindowPos(ImVec2(350, 0));
		ImGui::SetNextWindowSize(ImVec2(400, 400));
		ImGui::Begin("Game View", nullptr, ImGuiWindowFlags_NoNavInputs);
		game = ImGui::GetCurrentWindow();
		Funke::EditorKeyboard::ReserveGameNavigation();
		ImGui::BeginChild("Toolbar", ImVec2(0, 80));
		toolbar = ImGui::GetCurrentWindow();
		Funke::EditorKeyboard::ReserveGameNavigation();
		toolbarToggle = ImGui::GetID("VSync");
		ImGui::Checkbox("VSync", &toggled);
		ImGui::Button("Stats");
		if (openPopup) {
			ImGui::OpenPopup("Settings");
			openPopup = false;
		}
		if (ImGui::BeginPopup("Settings")) {
			popup = ImGui::GetCurrentWindow();
			ImGui::Selectable("Option A");
			ImGui::Selectable("Option B");
			ImGui::EndPopup();
		}
		ImGui::EndChild();
		ImGui::End();
		ImGui::EndFrame();
	};
	auto press = [&](ImGuiKey key, bool shift = false) {
		auto& io = ImGui::GetIO();
		io.AddKeyEvent(ImGuiMod_Shift, shift);
		io.AddKeyEvent(key, true);
		frame();
		io.AddKeyEvent(key, false);
		io.AddKeyEvent(ImGuiMod_Shift, false);
		frame();
		frame();
	};
	frame(); frame();
	for (auto* target : { game, toolbar }) {
		ImGui::FocusWindow(target);
		if (target == toolbar) ImGui::SetFocusID(toolbarToggle, toolbar);
		frame();
		const auto previousId = GImGui->NavId;
		for (bool shift : { false, true }) {
			press(ImGuiKey_Tab, shift);
			CHECK(GImGui->NavWindow == target);
			CHECK(GImGui->NavId == previousId);
			CHECK_FALSE(ImGui::GetIO().NavVisible);
			CHECK_FALSE(GImGui->NavMoveSubmitted);
		}
		for (auto key : { ImGuiKey_Enter, ImGuiKey_Space, ImGuiKey_DownArrow }) {
			press(key);
			CHECK_FALSE(toggled);
			CHECK(GImGui->NavId == previousId);
		}
	}
	ImGui::FocusWindow(inspector);
	frame(); frame();
	press(ImGuiKey_Tab);
	CHECK(GImGui->NavId == first);
	press(ImGuiKey_Tab);
	CHECK(GImGui->NavId == second);
	press(ImGuiKey_Tab, true);
	CHECK(GImGui->NavId == first);
	CHECK(ImGui::GetIO().NavVisible);

	ImGui::FocusWindow(toolbar);
	openPopup = true;
	frame(); frame();
	REQUIRE(popup != nullptr);
	REQUIRE(GImGui->NavWindow == popup);
	GImGui->NavCursorVisible = true;
	const auto previousId = GImGui->NavId;
	press(ImGuiKey_Tab);
	CHECK(GImGui->NavWindow == popup);
	CHECK(GImGui->NavId != previousId);
	CHECK(ImGui::GetIO().NavVisible);
}

TEST_CASE("Tab focuses an editor slider without starting numeric input; Enter still edits text") {
	KeyboardFixture fixture;
	SUBCASE("Windows and Linux shortcuts") { ImGui::GetIO().ConfigMacOSXBehaviors = false; }
	SUBCASE("macOS shortcuts") { ImGui::GetIO().ConfigMacOSXBehaviors = true; }
	float value = 1.2f;
	ImGuiID slider = 0;
	auto frame = [&] {
		ImGui::NewFrame();
		ImGui::Begin("TabSlider");
		ImGui::Button("Before");
		if (slider == 0) {
			ImGui::FocusWindow(ImGui::GetCurrentWindow());
			ImGui::SetFocusID(ImGui::GetID("Before"), ImGui::GetCurrentWindow());
		}
		slider = ImGui::GetID("Value");
		Funke::EditorKeyboard::SliderFloat("Value", &value, 0.75f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
		ImGui::Button("After");
		ImGui::End(); ImGui::EndFrame();
	};
	auto press = [&](ImGuiKey key) {
		ImGui::GetIO().AddKeyEvent(key, true); frame();
		ImGui::GetIO().AddKeyEvent(key, false); frame(); frame();
	};
	frame(); frame();
	GImGui->NavCursorVisible = true;
	GImGui->NavInputSource = ImGuiInputSource_Keyboard;
	press(ImGuiKey_Tab);
	CHECK(GImGui->NavId == slider);
	CHECK_FALSE(ImGui::TempInputIsActive(slider));
	press(ImGuiKey_D);
	CHECK(value == doctest::Approx(1.21f));
	press(ImGuiKey_LeftArrow);
	CHECK(value == doctest::Approx(1.2f));
	press(ImGuiKey_Enter);
	CHECK(ImGui::TempInputIsActive(slider));
	press(ImGuiKey_A);
	CHECK(value == doctest::Approx(1.2f));
	const auto selectModifier = ImGui::GetIO().ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
	ImGui::GetIO().AddKeyEvent(selectModifier, true);
	press(ImGuiKey_A);
	ImGui::GetIO().AddKeyEvent(selectModifier, false); frame();
	ImGui::GetIO().AddInputCharactersUTF8("1.75"); frame();
	press(ImGuiKey_Enter);
	CHECK(value == doctest::Approx(1.75f));
	CHECK_FALSE(ImGui::TempInputIsActive(slider));
	press(ImGuiKey_Tab);
	const float unfocused = value;
	press(ImGuiKey_D);
	CHECK(value == unfocused);
}

TEST_CASE("Editor combo handles empty disabled single and scrolling lists without wrapping") {
	for (const int count : { 0, 1, 4, 40 }) {
		KeyboardFixture fixture;
		int selected = count ? 0 : -1;
		bool open = false;
		bool initialize = true;
		bool allDisabled = count == 4;
		ImGuiID combo = 0;
		std::vector<ImGuiID> rows(count);
		float scroll = 0;
		auto frame = [&] {
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(10, 10));
			ImGui::SetNextWindowSize(ImVec2(350, 400));
			ImGui::Begin("Combo Cases");
			combo = ImGui::GetID("Choice");
			if (initialize) {
				ImGui::FocusWindow(ImGui::GetCurrentWindow());
				ImGui::SetFocusID(combo, ImGui::GetCurrentWindow());
				GImGui->NavCursorVisible = true;
				initialize = false;
			}
			open = Funke::EditorKeyboard::BeginCombo("Choice", "Choice", ImGuiComboFlags_HeightSmall);
			if (open) {
				for (int i = 0; i < count; ++i) {
					const std::string label = "Option " + std::to_string(i);
					rows[i] = ImGui::GetID(label.c_str());
					ImGui::BeginDisabled(allDisabled || (count == 40 && (i == 1 || i == 2)));
					if (ImGui::Selectable(label.c_str(), selected == i)) selected = i;
					if (selected == i) ImGui::SetItemDefaultFocus();
					ImGui::EndDisabled();
				}
				scroll = ImGui::GetScrollY();
				Funke::EditorKeyboard::EndCombo();
			}
			ImGui::End(); ImGui::EndFrame();
		};
		auto press = [&](ImGuiKey key) {
			ImGui::GetIO().AddKeyEvent(key, true); frame();
			ImGui::GetIO().AddKeyEvent(key, false); frame(); frame();
		};
		frame(); frame(); press(ImGuiKey_Enter);
		REQUIRE(open);
		if (count == 40) {
			press(ImGuiKey_A);
			CHECK(GImGui->NavId == rows[3]);
			CHECK(selected == 0);
			for (int i = 0; i < 45; ++i) press(ImGuiKey_DownArrow);
			CHECK(GImGui->NavId == rows[39]);
			CHECK(scroll > 0);
			press(ImGuiKey_W); press(ImGuiKey_UpArrow);
			CHECK(GImGui->NavId == rows[37]);
			press(ImGuiKey_Enter);
			CHECK(selected == 37);
			CHECK_FALSE(open);
			CHECK(GImGui->NavId == combo);
		}
		else {
			press(ImGuiKey_A); press(ImGuiKey_W);
			press(ImGuiKey_DownArrow); press(ImGuiKey_UpArrow);
			if (count == 1) {
				CHECK(GImGui->NavId == rows[0]);
				press(ImGuiKey_Enter);
				CHECK_FALSE(open);
				CHECK(selected == 0);
			}
			else {
				press(ImGuiKey_Enter);
				CHECK(selected == (count ? 0 : -1));
				press(ImGuiKey_Escape);
				CHECK_FALSE(open);
			}
			CHECK(GImGui->NavId == combo);
		}
	}
}

TEST_CASE("Editor combo search owns W and A while text input is active") {
	KeyboardFixture fixture;
	char search[32]{};
	bool open = false, focusSearch = false;
	int selected = 0;
	ImGuiID searchId = 0;
	auto frame = [&] {
		ImGui::NewFrame();
		ImGui::Begin("Search Combo");
		if (!open) {
			ImGui::FocusWindow(ImGui::GetCurrentWindow());
			ImGui::SetFocusID(ImGui::GetID("Choice"), ImGui::GetCurrentWindow());
			GImGui->NavCursorVisible = true;
		}
		open = Funke::EditorKeyboard::BeginCombo("Choice", "First");
		if (open) {
			if (focusSearch) { ImGui::SetKeyboardFocusHere(); focusSearch = false; }
			searchId = ImGui::GetID("Search");
			ImGui::InputText("Search", search, sizeof(search));
			if (ImGui::Selectable("First", selected == 0)) selected = 0;
			if (ImGui::Selectable("Second", selected == 1)) selected = 1;
			Funke::EditorKeyboard::EndCombo();
		}
		ImGui::End(); ImGui::EndFrame();
	};
	auto press = [&](ImGuiKey key, const char* text = nullptr) {
		ImGui::GetIO().AddKeyEvent(key, true);
		if (text) ImGui::GetIO().AddInputCharactersUTF8(text);
		frame(); ImGui::GetIO().AddKeyEvent(key, false); frame(); frame();
	};
	frame(); frame(); press(ImGuiKey_Enter);
	REQUIRE(open);
	focusSearch = true; frame(); frame(); frame();
	REQUIRE(GImGui->ActiveId == searchId);
	press(ImGuiKey_W, "w"); press(ImGuiKey_A, "a");
	CHECK(std::string(search) == "wa");
	CHECK(GImGui->NavId == searchId);
	CHECK(GImGui->ActiveId == searchId);
	CHECK(selected == 0);
	CHECK(open);
}
