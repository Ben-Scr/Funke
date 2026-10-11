#include "ImGuiContextLayer.hpp"

#ifdef IDX_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "Core/Application.hpp"
#include "Core/Assert.hpp"
#include "Core/Window.hpp"
#include "Gui/ImGuiContextSetup.hpp"
#include "Gui/ImGuiFonts.hpp"

#include <imgui.h>

namespace Index {

	namespace {
		float s_DpiScale = 1.0f;

		float Luminance(const ImVec4& color) {
			return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
		}

		Color ToColor(const ImVec4& color) {
			return Color(color.x, color.y, color.z, color.w);
		}

		void ApplyNativeTitlebarFromStyle() {
			Window* window = Application::GetWindow();
			if (!window) return;

			// Pick dark/light DWM titlebar chrome from the theme; leave the
			// caption + text colors at their default (alpha = 0 ->
			// DWMWA_COLOR_DEFAULT) so Windows paints the standard system
			// titlebar — same look as a no-customization app like Godot.
			const ImGuiStyle& style = ImGui::GetStyle();
			const bool dark = Luminance(style.Colors[ImGuiCol_WindowBg]) < 0.5f;
			window->SetTitlebarDarkMode(dark);
			window->SetTitlebarActiveColor(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
			window->SetTitlebarInactiveColor(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
			window->SetTitlebarTextColor(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
		}
	}

	void ImGuiContextLayer::OnAttach(Application& app) {
		if (m_IsInitialized) {
			return;
		}

		Window* window = app.GetWindow();
		IDX_ASSERT(window != nullptr, IndexErrorCode::InvalidHandle, "ImGuiContextLayer requires an active Window");
		GLFWwindow* glfwWindow = window->GetGLFWWindow();
		IDX_ASSERT(glfwWindow != nullptr, IndexErrorCode::InvalidHandle, "Window has no GLFW handle");

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		EditorRuntime::ImGuiContextSetup::PublishImGuiContextToPackages();

		ImGuiIO& io = ImGui::GetIO();
		EditorRuntime::ImGuiContextSetup::ApplyCommonIoConfigFlags(io);

		const float dpiScale = EditorRuntime::ImGuiContextSetup::CaptureDpiScale(glfwWindow);
		s_DpiScale = dpiScale;

		LoadIndexImGuiFont(io, dpiScale);

		EditorRuntime::ImGuiContextSetup::InitBackends(glfwWindow);

		ApplyIndexTheme(GetSystemTheme());
		m_IsInitialized = true;
	}

	void ImGuiContextLayer::OnDetach(Application& app) {
		(void)app;
		if (!m_IsInitialized) {
			return;
		}

		EditorRuntime::ImGuiContextSetup::ShutdownBackends();

		m_IsInitialized = false;
	}

	void ImGuiContextLayer::OnPreRender(Application& app) {
		(void)app;
		if (!m_IsInitialized) {
			return;
		}
		EditorRuntime::ImGuiContextSetup::NewFrame();
	}

	void ImGuiContextLayer::OnPostRender(Application& app) {
		(void)app;
		if (!m_IsInitialized) {
			return;
		}
		EditorRuntime::ImGuiContextSetup::RenderAndPresent();
	}

	ImGuiContextLayer::Theme ImGuiContextLayer::GetSystemTheme() {
#ifdef IDX_PLATFORM_WINDOWS
		DWORD appsUseLightTheme = 1;
		DWORD valueSize = sizeof(appsUseLightTheme);
		const LSTATUS status = RegGetValueW(
			HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			L"AppsUseLightTheme",
			RRF_RT_REG_DWORD,
			nullptr,
			&appsUseLightTheme,
			&valueSize);
		if (status == ERROR_SUCCESS) {
			return appsUseLightTheme == 0 ? Theme::Dark : Theme::Light;
		}
#endif
		return Theme::Dark;
	}

	void ImGuiContextLayer::ApplyIndexTheme(Theme theme) {
		ImGuiStyle& style = ImGui::GetStyle();
		const bool dark = (theme == Theme::Dark);
		style = ImGuiStyle();

		// ── Sizing & Rounding ───────────────────────────────────────
		style.WindowPadding     = ImVec2(14, 14);
		style.FramePadding      = ImVec2(10, 6);
		style.CellPadding       = ImVec2(6, 4);
		style.ItemSpacing       = ImVec2(8, 7);
		style.ItemInnerSpacing  = ImVec2(6, 4);
		style.IndentSpacing     = 16.0f;
		style.ScrollbarSize     = 10.0f;
		style.GrabMinSize       = 8.0f;

		style.WindowRounding    = 8.0f;
		style.ChildRounding     = 8.0f;
		style.FrameRounding     = 6.0f;
		style.PopupRounding     = 8.0f;
		style.ScrollbarRounding = 8.0f;
		style.GrabRounding      = 4.0f;
		style.TabRounding       = 6.0f;

		style.WindowBorderSize  = 1.0f;
		style.FrameBorderSize   = 0.0f;
		style.PopupBorderSize   = 1.0f;
		style.TabBorderSize     = 0.0f;

		style.WindowMenuButtonPosition = ImGuiDir_None;
		style.SeparatorTextBorderSize  = 2.0f;

		// ── Colors ──────────────────────────────────────────────────
		ImVec4* c = style.Colors;

		const ImVec4 bg        = dark ? ImVec4(0.110f, 0.114f, 0.125f, 1.00f) : ImVec4(0.949f, 0.953f, 0.961f, 1.00f);
		const ImVec4 bgChild   = ImVec4(0, 0, 0, 0);
		const ImVec4 bgPopup   = dark ? ImVec4(0.145f, 0.149f, 0.165f, 1.00f) : ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		const ImVec4 surface   = dark ? ImVec4(0.165f, 0.171f, 0.188f, 1.00f) : ImVec4(0.890f, 0.898f, 0.914f, 1.00f);
		const ImVec4 surfaceHi = dark ? ImVec4(0.212f, 0.220f, 0.243f, 1.00f) : ImVec4(0.843f, 0.855f, 0.878f, 1.00f);
		const ImVec4 surfaceAct= dark ? ImVec4(0.255f, 0.265f, 0.294f, 1.00f) : ImVec4(0.788f, 0.804f, 0.835f, 1.00f);
		const ImVec4 border    = dark ? ImVec4(0.85f, 0.90f, 1.00f, 0.09f) : ImVec4(0.10f, 0.14f, 0.22f, 0.14f);
		const ImVec4 accent    = dark ? ImVec4(0.357f, 0.553f, 0.937f, 1.00f) : ImVec4(0.184f, 0.420f, 0.859f, 1.00f);
		const ImVec4 text      = dark ? ImVec4(0.922f, 0.925f, 0.937f, 1.00f) : ImVec4(0.118f, 0.125f, 0.145f, 1.00f);
		const ImVec4 textDim   = dark ? ImVec4(0.545f, 0.557f, 0.592f, 1.00f) : ImVec4(0.447f, 0.463f, 0.502f, 1.00f);

		c[ImGuiCol_WindowBg]             = bg;
		c[ImGuiCol_ChildBg]              = bgChild;
		c[ImGuiCol_PopupBg]              = bgPopup;

		c[ImGuiCol_Border]               = border;
		c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

		c[ImGuiCol_Text]                 = text;
		c[ImGuiCol_TextDisabled]         = textDim;
		c[ImGuiCol_TextSelectedBg]       = ImVec4(accent.x, accent.y, accent.z, 0.35f);

		c[ImGuiCol_FrameBg]              = surface;
		c[ImGuiCol_FrameBgHovered]       = surfaceHi;
		c[ImGuiCol_FrameBgActive]        = dark ? ImVec4(0.25f, 0.25f, 0.30f, 1.00f) : ImVec4(0.78f, 0.82f, 0.88f, 1.00f);

		c[ImGuiCol_TitleBg]              = dark ? ImVec4(0.10f, 0.10f, 0.12f, 1.00f) : ImVec4(0.86f, 0.88f, 0.91f, 1.00f);
		c[ImGuiCol_TitleBgActive]        = dark ? ImVec4(0.14f, 0.14f, 0.17f, 1.00f) : ImVec4(0.78f, 0.83f, 0.90f, 1.00f);
		c[ImGuiCol_TitleBgCollapsed]     = dark ? ImVec4(0.10f, 0.10f, 0.12f, 0.75f) : ImVec4(0.86f, 0.88f, 0.91f, 0.75f);

		c[ImGuiCol_MenuBarBg]            = dark ? ImVec4(0.14f, 0.14f, 0.16f, 1.00f) : ImVec4(0.90f, 0.92f, 0.95f, 1.00f);

		c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_ScrollbarGrab]        = dark ? ImVec4(0.28f, 0.28f, 0.32f, 1.00f) : ImVec4(0.66f, 0.70f, 0.76f, 1.00f);
		c[ImGuiCol_ScrollbarGrabHovered] = dark ? ImVec4(0.35f, 0.35f, 0.40f, 1.00f) : ImVec4(0.56f, 0.61f, 0.68f, 1.00f);
		c[ImGuiCol_ScrollbarGrabActive]  = dark ? ImVec4(0.42f, 0.42f, 0.48f, 1.00f) : ImVec4(0.48f, 0.54f, 0.62f, 1.00f);

		c[ImGuiCol_Button]               = surface;
		c[ImGuiCol_ButtonHovered]        = surfaceHi;
		c[ImGuiCol_ButtonActive]         = surfaceAct;

		c[ImGuiCol_Header]               = ImVec4(accent.x, accent.y, accent.z, 0.22f);
		c[ImGuiCol_HeaderHovered]        = surfaceHi;
		c[ImGuiCol_HeaderActive]         = ImVec4(accent.x, accent.y, accent.z, 0.30f);

		c[ImGuiCol_Separator]            = border;
		c[ImGuiCol_SeparatorHovered]     = dark ? ImVec4(0.36f, 0.36f, 0.42f, 1.00f) : ImVec4(0.54f, 0.60f, 0.68f, 1.00f);
		c[ImGuiCol_SeparatorActive]      = dark ? ImVec4(0.44f, 0.44f, 0.50f, 1.00f) : ImVec4(0.46f, 0.52f, 0.62f, 1.00f);

		c[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_ResizeGripHovered]    = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_ResizeGripActive]     = ImVec4(0, 0, 0, 0);

		c[ImGuiCol_Tab]                  = dark ? ImVec4(0.15f, 0.15f, 0.18f, 1.00f) : ImVec4(0.86f, 0.89f, 0.93f, 1.00f);
		c[ImGuiCol_TabHovered]           = surfaceHi;
		c[ImGuiCol_TabSelected]          = dark ? ImVec4(0.20f, 0.20f, 0.24f, 1.00f) : ImVec4(0.78f, 0.84f, 0.91f, 1.00f);
		c[ImGuiCol_TabSelectedOverline]  = accent;
		c[ImGuiCol_TabDimmed]            = dark ? ImVec4(0.12f, 0.12f, 0.14f, 1.00f) : ImVec4(0.91f, 0.93f, 0.96f, 1.00f);
		c[ImGuiCol_TabDimmedSelected]    = dark ? ImVec4(0.17f, 0.17f, 0.20f, 1.00f) : ImVec4(0.84f, 0.88f, 0.93f, 1.00f);

		c[ImGuiCol_DockingPreview]       = ImVec4(accent.x, accent.y, accent.z, 0.40f);
		c[ImGuiCol_DockingEmptyBg]       = dark ? ImVec4(0.10f, 0.10f, 0.10f, 1.00f) : ImVec4(0.93f, 0.94f, 0.96f, 1.00f);

		c[ImGuiCol_CheckMark]            = accent;
		c[ImGuiCol_SliderGrab]           = dark ? ImVec4(0.40f, 0.40f, 0.46f, 1.00f) : ImVec4(0.55f, 0.62f, 0.72f, 1.00f);
		c[ImGuiCol_SliderGrabActive]     = dark ? ImVec4(0.50f, 0.50f, 0.56f, 1.00f) : ImVec4(0.43f, 0.52f, 0.66f, 1.00f);

		c[ImGuiCol_DragDropTarget]       = ImVec4(accent.x, accent.y, accent.z, 0.70f);

		c[ImGuiCol_NavCursor]            = accent;

		c[ImGuiCol_TableHeaderBg]        = dark ? ImVec4(0.15f, 0.15f, 0.18f, 1.00f) : ImVec4(0.86f, 0.89f, 0.93f, 1.00f);
		c[ImGuiCol_TableBorderStrong]    = border;
		c[ImGuiCol_TableBorderLight]     = ImVec4(border.x, border.y, border.z, 0.40f);
		c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_TableRowBgAlt]        = dark ? ImVec4(1, 1, 1, 0.03f) : ImVec4(0, 0, 0, 0.035f);

		c[ImGuiCol_ModalWindowDimBg]     = dark ? ImVec4(0, 0, 0, 0.55f) : ImVec4(0.18f, 0.22f, 0.28f, 0.35f);

		ApplyNativeTitlebarFromStyle();
		style.ScaleAllSizes(s_DpiScale);
	}

}
