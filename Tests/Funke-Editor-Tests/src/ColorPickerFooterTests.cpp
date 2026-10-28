#include <doctest/doctest.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <initializer_list>

namespace {
	struct ColorPickerFixture {
		ColorPickerFixture() {
			Context = ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
			io.DisplaySize = ImVec2(800.0f, 600.0f);
			io.DeltaTime = 1.0f / 60.0f;
			io.UserData = this;
			ImGui::SetColorPickerFooterHook([](float* color, ImGuiColorEditFlags flags) {
				auto& fixture = *static_cast<ColorPickerFixture*>(ImGui::GetIO().UserData);
				++fixture.Calls;
				fixture.Flags = flags;
				fixture.Disabled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
				ImGui::Button("Pick");
				if (!fixture.Commit || fixture.Disabled) return false;
				color[0] = 0.25f;
				color[1] = 0.5f;
				color[2] = 0.75f;
				return true;
			});
		}

		~ColorPickerFixture() {
			ImGui::SetColorPickerFooterHook(nullptr);
			ImGui::DestroyContext(Context);
		}

		bool Frame(float* color, ImGuiColorEditFlags flags = 0, bool disabled = false,
			bool readOnly = false, bool colorEdit = false) {
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f));
			ImGui::Begin("Colors", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
			ImGui::SetNextItemWidth(250.0f);
			ImGui::PushItemFlag(ImGuiItemFlags_ReadOnly, readOnly);
			ImGui::BeginDisabled(disabled);
			if (colorEdit) {
				ImGui::PushID("##color");
				ImGui::OpenPopup("picker");
				ImGui::PopID();
			}
			const bool changed = colorEdit
				? ImGui::ColorEdit4("##color", color, flags)
				: ImGui::ColorPicker4("##color", color, flags);
			ImGui::EndDisabled();
			ImGui::PopItemFlag();
			ImGui::End();
			ImGui::EndFrame();
			return changed;
		}

		ImGuiContext* Context = nullptr;
		int Calls = 0;
		ImGuiColorEditFlags Flags = 0;
		bool Commit = true;
		bool Disabled = false;
	};
}

TEST_CASE("ColorPicker footer commits through the picker and ColorEdit popup") {
	for (const bool colorEdit : { false, true }) {
		CAPTURE(colorEdit);
		ColorPickerFixture fixture;
		float color[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
		CHECK(fixture.Frame(color, 0, false, false, colorEdit));
		CHECK(fixture.Calls == 1);
		CHECK(color[0] == 0.25f);
		CHECK(color[1] == 0.5f);
		CHECK(color[2] == 0.75f);
		CHECK(color[3] == 0.4f);
		CHECK_FALSE(fixture.Frame(color, 0, false, false, colorEdit));
	}
}

TEST_CASE("ColorPicker footer receives resolved HSV and no-alpha flags") {
	ColorPickerFixture fixture;
	float color[4] = { 0.1f, 0.2f, 0.3f, 42.0f };
	CHECK(fixture.Frame(color, ImGuiColorEditFlags_InputHSV | ImGuiColorEditFlags_NoAlpha));
	CHECK((fixture.Flags & ImGuiColorEditFlags_InputHSV) != 0);
	CHECK((fixture.Flags & ImGuiColorEditFlags_NoAlpha) != 0);
	CHECK(color[3] == 42.0f);
}

TEST_CASE("ColorPicker footer inherits disabled and read-only state") {
	for (const bool readOnly : { false, true }) {
		CAPTURE(readOnly);
		ColorPickerFixture fixture;
		float color[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
		CHECK_FALSE(fixture.Frame(color, 0, !readOnly, readOnly));
		CHECK(fixture.Calls == 1);
		CHECK(fixture.Disabled);
		CHECK(color[0] == 0.1f);
		CHECK(color[1] == 0.2f);
		CHECK(color[2] == 0.3f);
		CHECK(color[3] == 0.4f);
	}
}

TEST_CASE("ColorPicker footer skips option thumbnails and can be removed") {
	ColorPickerFixture fixture;
	float color[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
	const ImGuiColorEditFlags previewFlags = ImGuiColorEditFlags_NoSidePreview
		| ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoOptions;
	CHECK_FALSE(fixture.Frame(color, previewFlags));
	CHECK(fixture.Calls == 0);
	CHECK(fixture.Frame(color));
	CHECK(fixture.Calls == 1);
	ImGui::SetColorPickerFooterHook(nullptr);
	CHECK_FALSE(fixture.Frame(color));
	CHECK(fixture.Calls == 1);
}
