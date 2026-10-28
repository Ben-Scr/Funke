#include <doctest/doctest.h>

#include "Gui/AssetRenameInput.hpp"

#include <functional>
#include <string>

using namespace Funke;

namespace {
	using RenameResult = AssetRenameInput::Result;

	struct RenameInputFixture {
		explicit RenameInputFixture(bool macOS) {
			Context = ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = 1.0f / 60.0f;
			io.ConfigMacOSXBehaviors = macOS;
			io.UserData = this;
		}

		~RenameInputFixture() { ImGui::DestroyContext(Context); }

		RenameResult Frame(const std::function<void()>& before = {}, const std::function<void()>& after = {}) {
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f));
			ImGui::Begin("Project", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
			if (before) before();
			ImGui::SetNextItemWidth(200.0f);
			const RenameResult result = Input.Render("##rename", Buffer, sizeof(Buffer));
			Active = ImGui::IsItemActive();
			if (after) after();
			ImGui::End();
			ImGui::EndFrame();
			return result;
		}

		void Activate() {
			for (int frame = 0; frame < 4 && !Active; ++frame) {
				REQUIRE(Frame() == RenameResult::Editing);
			}
			REQUIRE(Active);
		}

		ImGuiContext* Context = nullptr;
		AssetRenameInput Input;
		char Buffer[256] = "NewEntityScript";
		bool Active = false;
		bool PlatformFocused = true;
	};
}

TEST_CASE("AssetRenameInput retries delayed activation and commits typed text on Enter") {
	for (bool macOS : { false, true }) {
		CAPTURE(macOS);
		RenameInputFixture fixture(macOS);
		for (int frame = 0; frame < 5; ++frame) {
			CHECK(fixture.Frame(
				[] { ImGui::GetCurrentContext()->MovingWindow = ImGui::GetCurrentWindow(); },
				[] { ImGui::GetCurrentContext()->MovingWindow = nullptr; }) == RenameResult::Editing);
			CHECK_FALSE(fixture.Active);
		}
		fixture.Activate();
		ImGui::GetIO().AddInputCharactersUTF8("PlayerController");
		CHECK(fixture.Frame() == RenameResult::Editing);
		CHECK(std::string(fixture.Buffer) == "PlayerController");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
		CHECK(fixture.Frame() == RenameResult::Commit);
	}
}

TEST_CASE("AssetRenameInput waits for the creation popup to close") {
	for (bool macOS : { false, true }) {
		CAPTURE(macOS);
		RenameInputFixture fixture(macOS);
		for (int frame = 0; frame < 5; ++frame) {
			CHECK(fixture.Frame(
				[frame] { if (frame == 0) ImGui::OpenPopup("Create"); },
				[frame] {
					ImGui::SetNextWindowPos(ImVec2(400.0f, 200.0f));
					if (ImGui::BeginPopup("Create")) {
						ImGui::TextUnformatted("EntityScript");
						if (frame == 4) ImGui::CloseCurrentPopup();
						ImGui::EndPopup();
					}
				}) == RenameResult::Editing);
			CHECK_FALSE(fixture.Active);
		}
		fixture.Activate();
		ImGui::GetIO().AddInputCharactersUTF8("Enemy");
		CHECK(fixture.Frame() == RenameResult::Editing);
		CHECK(std::string(fixture.Buffer) == "Enemy");
	}
}

TEST_CASE("AssetRenameInput waits for native window focus before starting text input") {
	for (bool macOS : { false, true }) {
		CAPTURE(macOS);
		RenameInputFixture fixture(macOS);
		ImGui::GetPlatformIO().Platform_GetWindowFocus = [](ImGuiViewport*) {
			return static_cast<RenameInputFixture*>(ImGui::GetIO().UserData)->PlatformFocused;
		};
		fixture.PlatformFocused = false;
		for (int frame = 0; frame < 5; ++frame) {
			CHECK(fixture.Frame() == RenameResult::Editing);
			CHECK_FALSE(fixture.Active);
			CHECK_FALSE(fixture.Context->PlatformImeData.WantTextInput);
		}
		fixture.PlatformFocused = true;
		fixture.Activate();
		ImGui::GetIO().AddInputCharactersUTF8("NativeFocusRestored");
		CHECK(fixture.Frame() == RenameResult::Editing);
		CHECK(std::string(fixture.Buffer) == "NativeFocusRestored");
	}
}

TEST_CASE("AssetRenameInput withdraws queued activation when native focus changes") {
	for (bool macOS : { false, true }) {
		CAPTURE(macOS);
		RenameInputFixture fixture(macOS);
		ImGui::GetPlatformIO().Platform_GetWindowFocus = [](ImGuiViewport*) {
			return static_cast<RenameInputFixture*>(ImGui::GetIO().UserData)->PlatformFocused;
		};
		REQUIRE(fixture.Frame() == RenameResult::Editing);
		REQUIRE_FALSE(fixture.Active);
		fixture.PlatformFocused = false;
		CHECK(fixture.Frame([] {
			REQUIRE(ImGui::GetCurrentContext()->NavActivateId == ImGui::GetID("##rename"));
		}) == RenameResult::Editing);
		CHECK_FALSE(fixture.Active);
		CHECK_FALSE(fixture.Context->PlatformImeData.WantTextInput);
		ImGui::GetIO().AddMousePosEvent(30.0f, 18.0f);
		ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		CHECK(fixture.Frame() == RenameResult::Editing);
		CHECK_FALSE(fixture.Active);
		CHECK_FALSE(fixture.Context->PlatformImeData.WantTextInput);
		ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
		REQUIRE(fixture.Frame() == RenameResult::Editing);
		fixture.PlatformFocused = true;
		fixture.Activate();
		ImGui::GetIO().AddInputCharactersUTF8("FocusRecovered");
		CHECK(fixture.Frame() == RenameResult::Editing);
		CHECK(std::string(fixture.Buffer) == "FocusRecovered");
	}
}

TEST_CASE("AssetRenameInput withdraws queued activation when a popup opens") {
	for (bool macOS : { false, true }) {
		CAPTURE(macOS);
		RenameInputFixture fixture(macOS);
		REQUIRE(fixture.Frame() == RenameResult::Editing);
		REQUIRE_FALSE(fixture.Active);
		CHECK(fixture.Frame(
			[] {
				REQUIRE(ImGui::GetCurrentContext()->NavActivateId == ImGui::GetID("##rename"));
				ImGui::OpenPopup("Create");
			},
			[] {
				if (ImGui::BeginPopup("Create")) {
					ImGui::TextUnformatted("EntityScript");
					ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
				}
			}) == RenameResult::Editing);
		CHECK_FALSE(fixture.Active);
		CHECK_FALSE(fixture.Context->PlatformImeData.WantTextInput);
		fixture.Activate();
		ImGui::GetIO().AddInputCharactersUTF8("PopupClosed");
		CHECK(fixture.Frame() == RenameResult::Editing);
		CHECK(std::string(fixture.Buffer) == "PopupClosed");
	}
}

TEST_CASE("AssetRenameInput cancels with Escape after editing") {
	for (bool macOS : { false, true }) {
		CAPTURE(macOS);
		RenameInputFixture fixture(macOS);
		fixture.Activate();
		ImGui::GetIO().AddInputCharactersUTF8("Discarded");
		REQUIRE(fixture.Frame() == RenameResult::Editing);
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
		CHECK(fixture.Frame() == RenameResult::Cancel);
	}
}

TEST_CASE("AssetRenameInput commits an activated input when clicking outside") {
	for (bool macOS : { false, true }) {
		CAPTURE(macOS);
		RenameInputFixture fixture(macOS);
		fixture.Activate();
		ImGui::GetIO().AddInputCharactersUTF8("LevelSettings");
		REQUIRE(fixture.Frame() == RenameResult::Editing);
		ImGui::GetIO().AddMousePosEvent(400.0f, 150.0f);
		ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
		CHECK(fixture.Frame() == RenameResult::Commit);
		CHECK(std::string(fixture.Buffer) == "LevelSettings");
	}
}

TEST_CASE("AssetRenameInput reset waits for a fresh activation") {
	RenameInputFixture fixture(true);
	fixture.Activate();
	ImGui::ClearActiveID();
	fixture.Input.Reset();
	fixture.Active = false;
	CHECK(fixture.Frame(
		[] { ImGui::GetCurrentContext()->MovingWindow = ImGui::GetCurrentWindow(); },
		[] { ImGui::GetCurrentContext()->MovingWindow = nullptr; }) == RenameResult::Editing);
	CHECK_FALSE(fixture.Active);
	fixture.Activate();
}
