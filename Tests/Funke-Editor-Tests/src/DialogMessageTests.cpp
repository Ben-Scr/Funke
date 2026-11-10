#include <doctest/doctest.h>

#include "Gui/ImGuiDialog.hpp"
#include "Core/Platform/DialogText.hpp"
#include <imgui_internal.h>

#include <string>

namespace {
	struct MessageFixture {
		ImGuiContext* Previous = ImGui::GetCurrentContext();
		ImGuiContext* Context = ImGui::CreateContext();
		ImGuiWindow* Dialog = nullptr;
		ImGuiWindow* Body = nullptr;
		bool FooterVisible = false;
		bool Open = true;
		bool ExplicitWidth = true;

		MessageFixture(float scale = 1.0f, ImVec2 display = ImVec2(800.0f, 600.0f)) {
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
			io.DisplaySize = display;
			io.DeltaTime = 1.0f / 60.0f;
			ImGui::GetStyle().FontScaleMain = scale;
		}

		~MessageFixture() {
			ImGui::DestroyContext(Context);
			ImGui::SetCurrentContext(Previous);
		}

		void Frame(const std::string& message, bool scrollToEnd = false, bool close = false) {
			ImGui::NewFrame();
			if (Open) { ImGui::OpenPopup("Error"); Open = false; }
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (ExplicitWidth) Funke::ImGuiDialog::NextMessageWindow();
			Body = nullptr;
			if (Funke::ImGuiDialog::BeginModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
				Dialog = ImGui::GetCurrentWindow();
				Funke::ImGuiDialog::Message("##Details", message.c_str());
				for (ImGuiWindow* window : Context->Windows) {
					if (window->ParentWindow == Dialog && window->Active) Body = window;
				}
				if (scrollToEnd && Body) ImGui::SetScrollY(Body, Body->ScrollMax.y);
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
				ImGui::Button("OK", ImVec2(Funke::ImGuiDialog::FooterButton(0, 1, 100.0f), 0.0f));
				FooterVisible = ImGui::IsItemVisible();
				if (close) ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}
			ImGui::EndFrame();
		}
	};

	std::string LongError() {
		std::string text;
		for (int i = 0; i < 200; ++i)
			text += "Project could not be opened: dependency " + std::to_string(i) + " is missing (100% complete).\n";
		return text + "Last diagnostic";
	}
}

TEST_CASE("Dialog messages keep short errors compact and literal") {
	MessageFixture fixture;
	for (int i = 0; i < 5; ++i) fixture.Frame("Project failed: 100% / %s / ## details");
	CHECK(fixture.Body == nullptr);
	REQUIRE(fixture.Dialog != nullptr);
	CHECK(fixture.Dialog->Size.y < 150.0f);
	CHECK(fixture.Dialog->Pos.x + fixture.Dialog->Size.x * 0.5f == doctest::Approx(400.0f).epsilon(0.02));
	CHECK(fixture.Dialog->Pos.y + fixture.Dialog->Size.y * 0.5f == doctest::Approx(300.0f).epsilon(0.03));
	CHECK(fixture.FooterVisible);
}

TEST_CASE("Dialog messages scroll long errors while keeping the footer visible at different scales") {
	for (float scale : { 0.75f, 1.0f, 2.0f }) {
		CAPTURE(scale);
		MessageFixture fixture(scale, ImVec2(640.0f, 480.0f));
		const std::string text = LongError();
		for (int i = 0; i < 5; ++i) fixture.Frame(text);
		REQUIRE(fixture.Body != nullptr);
		CHECK(fixture.Body->ScrollbarY);
		CHECK_FALSE(fixture.Body->ScrollbarX);
		CHECK(fixture.Body->ScrollMax.y > 0.0f);
		CHECK(fixture.Dialog->Size.y < 480.0f);
		CHECK(fixture.Dialog->Size.x < 640.0f);
		CHECK(fixture.FooterVisible);
		fixture.Frame(text, true);
		fixture.Frame(text);
		CHECK(fixture.Body->Scroll.y == doctest::Approx(fixture.Body->ScrollMax.y));
		CHECK(fixture.FooterVisible);
		fixture.Frame(text, false, true);
		fixture.Frame(text);
		fixture.Open = true;
		for (int i = 0; i < 3; ++i) fixture.Frame(text);
		REQUIRE(fixture.Body != nullptr);
		CHECK(fixture.Body->Scroll.y == 0.0f);
	}
}

TEST_CASE("Dialog messages wrap unbroken paths without widening the dialog") {
	MessageFixture fixture;
	for (int i = 0; i < 5; ++i) fixture.Frame("C:/" + std::string(10000, 'x'));
	REQUIRE(fixture.Body != nullptr);
	CHECK(fixture.Body->ScrollbarY);
	CHECK_FALSE(fixture.Body->ScrollbarX);
	CHECK(fixture.Dialog->Size.x < 800.0f);
	CHECK(fixture.FooterVisible);
}

TEST_CASE("Dialog messages without an explicit width stay readable across frames and reopen") {
	for (float scale : { 0.75f, 1.0f, 2.0f }) {
		for (ImVec2 display : { ImVec2(800.0f, 600.0f), ImVec2(320.0f, 480.0f) }) {
			CAPTURE(scale);
			CAPTURE(display.x);
			MessageFixture fixture(scale, display);
			fixture.ExplicitWidth = false;
			const std::string message = "Delete \"Player\"?";
			for (int frame = 0; frame < 8; ++frame) {
				fixture.Frame(message);
				REQUIRE(fixture.Dialog != nullptr);
				CHECK(fixture.Dialog->Size.x >= std::min(300.0f, display.x * 0.9f));
				CHECK(fixture.Dialog->Size.x <= display.x * 0.9f);
			}
			CHECK(fixture.Body == nullptr);
			CHECK(fixture.FooterVisible);
			for (int frame = 0; frame < 5; ++frame) fixture.Frame(LongError());
			REQUIRE(fixture.Body != nullptr);
			CHECK(fixture.Body->ScrollbarY);
			CHECK_FALSE(fixture.Body->ScrollbarX);
			CHECK(fixture.FooterVisible);
			fixture.Frame(LongError(), false, true);
			fixture.Frame(message);
			fixture.Open = true;
			for (int frame = 0; frame < 5; ++frame) fixture.Frame(message);
			CHECK(fixture.Dialog->Size.x >= std::min(300.0f, display.x * 0.9f));
			CHECK(fixture.Body == nullptr);
			CHECK(fixture.FooterVisible);
		}
	}
}

TEST_CASE("Dialog bodies bound combined metadata and keep wrapped footer actions visible") {
	for (float scale : { 1.0f, 2.0f }) {
		MessageFixture fixture(scale, ImVec2(640.0f, 480.0f));
		ImGuiWindow* body = nullptr;
		for (int frame = 0; frame < 6; ++frame) {
			ImGui::NewFrame();
			if (frame == 0) ImGui::OpenPopup("Library");
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(1000.0f, 0.0f), ImGuiCond_Appearing);
			if (Funke::ImGuiDialog::BeginModal("Library", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGuiWindow* dialog = ImGui::GetCurrentWindow();
				if (Funke::ImGuiDialog::BeginBody("##Metadata")) {
					body = ImGui::GetCurrentWindow();
					for (int i = 0; i < 100; ++i) ImGui::Text("Dependency %d: A project with a long package description", i);
					Funke::ImGuiDialog::Error("##Error", LongError().c_str(), ImVec4(1, 0, 0, 1), "Copy details");
				}
				Funke::ImGuiDialog::EndBody();
				ImGui::Separator();
				for (int i = 0; i < 3; ++i) {
					ImGui::PushID(i);
					const float width = Funke::ImGuiDialog::FooterButton(i, 3, ImGui::GetFontSize() * 14.0f);
					ImGui::Button("Action", ImVec2(width, 0.0f));
					if (frame == 5) {
						CAPTURE(scale);
						CAPTURE(i);
						CHECK(ImGui::IsItemVisible());
						CHECK(ImGui::GetItemRectMax().x <= dialog->Pos.x + dialog->Size.x);
						CHECK(ImGui::GetItemRectMax().y <= dialog->Pos.y + dialog->Size.y);
					}
					ImGui::PopID();
				}
				if (frame == 5) {
					REQUIRE(body != nullptr);
					CHECK(body->ScrollbarY);
					CHECK_FALSE(body->ScrollbarX);
					CHECK(body->DC.ChildWindows.empty());
					CHECK(dialog->ScrollMax.y == 0.0f);
					CHECK(dialog->Pos.y + dialog->Size.y <= 480.0f);
					CHECK(dialog->Size.x <= 640.0f * 0.9f);
					CHECK(dialog->Size.y <= 480.0f * 0.85f);
				}
				ImGui::EndPopup();
			}
			ImGui::EndFrame();
		}
	}
}

TEST_CASE("Native dialog previews bound lines and retain complete UTF-8 characters") {
	using Funke::Platform::DialogText;
	CHECK(DialogText::Prepare("Short error: 100% complete").Preview == "Short error: 100% complete");
	CHECK_FALSE(DialogText::Prepare("Short error").Truncated);
	const auto lines = DialogText::Prepare(LongError());
	CHECK(lines.Truncated);
	CHECK(std::count(lines.Preview.begin(), lines.Preview.end(), '\n') <= 10);
	const auto path = DialogText::Prepare(std::string(10000, 'x'));
	CHECK(path.Truncated);
	CHECK(path.Preview.size() < 750);
	std::string unicode;
	for (int i = 0; i < 1000; ++i) unicode += "ä";
	const auto preview = DialogText::Prepare(unicode);
	CHECK(preview.Truncated);
	CHECK(preview.Preview.substr(0, 160) == unicode.substr(0, 160));
	CHECK(preview.Preview[160] == '\n');
	CHECK(DialogText::Prepare("one\r\ntwo\tthree").Preview == "one\ntwo three");
}
