#include "ImGuiContextLayer.hpp"

#include "Assets/AssetRegistry.hpp"
#include "Core/Application.hpp"
#include "Core/Assert.hpp"
#include "Core/Window.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Events/FunkeEvent.hpp"
#include "Graphics/Text/FontHandle.hpp"
#include "Gui/ImGuiContextSetup.hpp"
#include "Gui/EditorTheme.hpp"
#include "Gui/ImGuiFonts.hpp"
#include "Serialization/Path.hpp"
#include "Utils/ExpressionEvaluator.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <utility>

namespace Funke {

	float ImGuiContextLayer::s_DpiScale = 1.0f;

	namespace {
		constexpr const char* k_ImGuiIniFileName = "imgui.ini";

		// MUST be consumed at the start of OnPreRender BEFORE NewFrame: mid-frame ClearIniSettings+LoadIniSettingsFromDisk corrupts the dock context, leaving every dockable window floating.
		std::string s_PendingLayoutReloadPath;
		// Starts as DefaultIfUndocked so a fresh install, or a layout saved under another dockspace ID, gets the default.
		ImGuiContextLayer::DockLayoutRequest s_DockLayoutRequest = ImGuiContextLayer::DockLayoutRequest::DefaultIfUndocked;

		// ── Numeric expression input ─────────────────────────────────
		// Bridges ImGui scalar text entry to the engine expression evaluator so
		// typing "5/2" or "/2" into any Drag/Slider/Input numeric field evaluates.
		// Installed once on the context (OnAttach) via the SetScalarExpressionHook
		// patch in imgui_widgets.cpp; p_data arrives holding the current value.
		double ReadScalarAsDouble(ImGuiDataType type, const void* p) {
			switch (type) {
			case ImGuiDataType_S8:     return static_cast<double>(*static_cast<const ImS8*>(p));
			case ImGuiDataType_U8:     return static_cast<double>(*static_cast<const ImU8*>(p));
			case ImGuiDataType_S16:    return static_cast<double>(*static_cast<const ImS16*>(p));
			case ImGuiDataType_U16:    return static_cast<double>(*static_cast<const ImU16*>(p));
			case ImGuiDataType_S32:    return static_cast<double>(*static_cast<const ImS32*>(p));
			case ImGuiDataType_U32:    return static_cast<double>(*static_cast<const ImU32*>(p));
			case ImGuiDataType_S64:    return static_cast<double>(*static_cast<const ImS64*>(p));
			case ImGuiDataType_U64:    return static_cast<double>(*static_cast<const ImU64*>(p));
			case ImGuiDataType_Float:  return static_cast<double>(*static_cast<const float*>(p));
			case ImGuiDataType_Double: return *static_cast<const double*>(p);
			default:                   return 0.0;
			}
		}

		double RoundClamp(double v, double lo, double hi) {
			v = std::round(v);
			return v < lo ? lo : (v > hi ? hi : v);
		}

		bool WriteScalarFromDouble(ImGuiDataType type, void* p, double v) {
			switch (type) {
			case ImGuiDataType_Float:  *static_cast<float*>(p)  = static_cast<float>(v); return true;
			case ImGuiDataType_Double: *static_cast<double*>(p) = v; return true;
			case ImGuiDataType_S8:  *static_cast<ImS8*>(p)  = static_cast<ImS8>(RoundClamp(v, INT8_MIN, INT8_MAX));    return true;
			case ImGuiDataType_U8:  *static_cast<ImU8*>(p)  = static_cast<ImU8>(RoundClamp(v, 0, UINT8_MAX));          return true;
			case ImGuiDataType_S16: *static_cast<ImS16*>(p) = static_cast<ImS16>(RoundClamp(v, INT16_MIN, INT16_MAX)); return true;
			case ImGuiDataType_U16: *static_cast<ImU16*>(p) = static_cast<ImU16>(RoundClamp(v, 0, UINT16_MAX));        return true;
			case ImGuiDataType_S32: *static_cast<ImS32*>(p) = static_cast<ImS32>(RoundClamp(v, INT32_MIN, INT32_MAX)); return true;
			case ImGuiDataType_U32: *static_cast<ImU32*>(p) = static_cast<ImU32>(RoundClamp(v, 0, UINT32_MAX));        return true;
			case ImGuiDataType_S64: {
				// INT64 range exceeds double's exact-integer range; clamp in double space
				// and guard the cast so v == 2^63 (rounded INT64_MAX) can't trigger UB.
				const double r = std::round(v);
				ImS64 out;
				if (r >= 9223372036854775808.0)       out = INT64_MAX;
				else if (r <= -9223372036854775808.0) out = INT64_MIN;
				else                                  out = static_cast<ImS64>(r);
				*static_cast<ImS64*>(p) = out;
				return true;
			}
			case ImGuiDataType_U64: {
				const double r = std::round(v);
				ImU64 out;
				if (r <= 0.0)                         out = 0;
				else if (r >= 18446744073709551616.0) out = UINT64_MAX;
				else                                  out = static_cast<ImU64>(r);
				*static_cast<ImU64*>(p) = out;
				return true;
			}
			default: return false; // Bool/String/unknown — not a numeric scalar.
			}
		}

		bool ScalarExpressionHook(const char* buf, ImGuiDataType type, void* p_data) {
			double result = 0.0;
			if (!ExpressionEvaluator::Evaluate(buf, ReadScalarAsDouble(type, p_data), result)) {
				return false;
			}
			return WriteScalarFromDouble(type, p_data, result);
		}

		// ImGui can diverge window->DockId to 0 while window->DockNode stays live (observed for overlay-layer panels); WindowSettingsHandler_WriteAll reads DockId verbatim, serialising them as floating. Sync from the live pointer before every save.
		void SyncDockIdsFromLiveNodes() {
			ImGuiContext* ctx = ImGui::GetCurrentContext();
			if (ctx == nullptr) return;
			for (int n = 0; n < ctx->Windows.Size; n++) {
				ImGuiWindow* window = ctx->Windows[n];
				if (window->DockNode != nullptr &&
					window->DockId != window->DockNode->ID) {
					window->DockId = window->DockNode->ID;
				}
			}
		}

		std::filesystem::path GetEditorUserIniFilePath() {
			try {
				return std::filesystem::path(Path::GetSpecialFolderPath(SpecialFolder::LocalAppData)) /
					"Funke" / "Editor" / k_ImGuiIniFileName;
			} catch (...) {
				return std::filesystem::path(k_ImGuiIniFileName);
			}
		}

		std::string ResolveEditorIniFilePath() {
			std::filesystem::path userIniFile = GetEditorUserIniFilePath();
			std::error_code ec;

			if (!userIniFile.parent_path().empty()) {
				std::filesystem::create_directories(userIniFile.parent_path(), ec);
			}

			return userIniFile.make_preferred().string();
		}

		std::filesystem::path GetLayoutPresetsDirectory() {
			try {
				return std::filesystem::path(Path::GetSpecialFolderPath(SpecialFolder::LocalAppData))
					/ "Funke" / "Editor" / "Layouts";
			} catch (...) {
				return std::filesystem::path("Layouts");
			}
		}

		std::filesystem::path GetLayoutPresetPath(const std::string& name) {
			return GetLayoutPresetsDirectory() / (name + ".ini");
		}

		struct ThemeRounding {
			float Window, Child, Frame, Popup, Scrollbar, Grab, Tab;
		};
		constexpr ThemeRounding k_DefaultRounding{ 6.0f, 4.0f, 4.0f, 6.0f, 8.0f, 3.0f, 4.0f };
		constexpr ThemeRounding k_GlassRounding{ 12.0f, 8.0f, 6.0f, 10.0f, 9.0f, 6.0f, 6.0f };

		// Truncates like ImGuiStyle::ScaleAllSizes so a theme switch lands on the same values as startup.
		void SetRounding(ImGuiStyle& style, const ThemeRounding& r, float scale) {
			style.WindowRounding    = std::trunc(r.Window * scale);
			style.ChildRounding     = std::trunc(r.Child * scale);
			style.FrameRounding     = std::trunc(r.Frame * scale);
			style.PopupRounding     = std::trunc(r.Popup * scale);
			style.ScrollbarRounding = std::trunc(r.Scrollbar * scale);
			style.GrabRounding      = std::trunc(r.Grab * scale);
			style.TabRounding       = std::trunc(r.Tab * scale);
		}
	}

	void ImGuiContextLayer::OnAttach(Application& app) {
		if (m_IsInitialized) {
			return;
		}

		Window* window = app.GetWindow();
		FNK_ASSERT(window != nullptr, FunkeErrorCode::InvalidHandle, "ImGuiContextLayer requires an active Window");
		SDL_Window* sdlWindow = window->GetNativeWindow();
		FNK_ASSERT(sdlWindow != nullptr, FunkeErrorCode::InvalidHandle, "Window has no SDL handle");

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		// Numeric fields accept arithmetic ("5/2", "/2", …) via this evaluator hook.
		ImGui::SetScalarExpressionHook(&ScalarExpressionHook);

		EditorRuntime::ImGuiContextSetup::PublishImGuiContextToPackages();

		ImGuiIO& io = ImGui::GetIO();
		m_IniFilePath = ResolveEditorIniFilePath();
		io.IniFilename = m_IniFilePath.c_str();
		// 1s: small enough that a hard quit loses minimal layout; large enough to avoid disk thrash.
		io.IniSavingRate = 1.0f;
		// Docking/viewport flags MUST precede LoadIniSettingsFromDisk: the [Docking][Data] and [Viewport] sections only parse through handlers registered by these flags; loading first silently destroys the persisted layout.
		EditorRuntime::ImGuiContextSetup::ApplyCommonIoConfigFlags(io);
		std::error_code loadEc;
		const bool iniFileExists = std::filesystem::is_regular_file(m_IniFilePath, loadEc);
		const std::uintmax_t iniFileSize = iniFileExists
			? std::filesystem::file_size(m_IniFilePath, loadEc)
			: 0u;
		if (iniFileExists) {
			ImGui::LoadIniSettingsFromDisk(m_IniFilePath.c_str());
		}
		FNK_CORE_INFO_TAG("ImGui",
			"Editor layout file: {} ({}, {} bytes)",
			m_IniFilePath,
			iniFileExists ? "loaded" : "missing — defaults will be used",
			iniFileSize);

		const float dpiScale = EditorRuntime::ImGuiContextSetup::CaptureDpiScale(sdlWindow);
		s_DpiScale = dpiScale;

		// EditorPreferences::Load hasn't run yet (ImGuiEditorLayer::OnAttach is later); font size is finalised by ApplyEditorFontSize once prefs load.
		{
			const uint64_t fontId = EditorPreferences::GetEditorFontAssetId();
			bool loadedCustom = false;
			if (fontId != 0 && fontId != k_DefaultFontAssetId) {
				const std::string path = AssetRegistry::ResolvePath(fontId);
				if (!path.empty() && std::filesystem::exists(path)) {
					const float fontSize = k_FunkeImGuiFontSize * dpiScale;
					ImFontConfig fontCfg;
					fontCfg.SizePixels = fontSize;
					fontCfg.PixelSnapH = true;
					if (io.Fonts->AddFontFromFileTTF(
							path.c_str(), fontSize, &fontCfg, io.Fonts->GetGlyphRangesDefault())) {
						loadedCustom = true;
					} else {
						FNK_CORE_WARN_TAG("ImGui",
							"Editor font '{}' failed to load (unsupported or corrupt); falling back to default.",
							path);
					}
				} else {
					FNK_CORE_WARN_TAG("ImGui",
						"Editor font asset id {} could not be resolved; falling back to default.",
						fontId);
				}
			}
			if (!loadedCustom) {
				LoadFunkeImGuiFont(io, dpiScale);
			}
		}

		EditorRuntime::ImGuiContextSetup::InitBackends(sdlWindow);

		ApplyFunkeTheme();
		// Must run after the theme — ScaleAllSizes is multiplicative on the
		// current style values.
		ImGui::GetStyle().ScaleAllSizes(dpiScale);

		m_IsInitialized = true;
	}

	void ImGuiContextLayer::OnDetach(Application& app) {
		(void)app;
		if (!m_IsInitialized) {
			return;
		}

		// Explicit flush before DestroyContext: the timer-driven auto-save may not have fired yet.
		const ImGuiIO& io = ImGui::GetIO();
		if (io.IniFilename != nullptr && *io.IniFilename != '\0') {
			SyncDockIdsFromLiveNodes();
			ImGui::SaveIniSettingsToDisk(io.IniFilename);
			std::error_code ec;
			const bool exists = std::filesystem::is_regular_file(io.IniFilename, ec);
			FNK_CORE_INFO_TAG("ImGui", "Saved editor layout to {} ({})",
				io.IniFilename,
				exists ? "ok" : "WRITE FAILED — path not writable?");
		}

		EditorRuntime::ImGuiContextSetup::ShutdownBackends();
		m_IniFilePath.clear();

		m_IsInitialized = false;
	}

	void ImGuiContextLayer::OnPreRender(Application& app) {
		(void)app;
		if (!m_IsInitialized) {
			return;
		}

		if (!s_PendingLayoutReloadPath.empty()) {
			const std::string path = std::move(s_PendingLayoutReloadPath);
			s_PendingLayoutReloadPath.clear();
			ImGui::ClearIniSettings();
			ImGui::LoadIniSettingsFromDisk(path.c_str());
			ImGui::GetIO().WantSaveIniSettings = false;
			m_LastSaveTime = std::chrono::steady_clock::now();
			// A preset saved before a dockspace ID change docks nothing; fall back to the default instead of floating panels.
			if (s_DockLayoutRequest == DockLayoutRequest::None) {
				s_DockLayoutRequest = DockLayoutRequest::DefaultIfUndocked;
			}
			FNK_CORE_INFO_TAG("ImGui",
				"Applied deferred layout reload from '{}'.", path);
		}

		EditorRuntime::ImGuiContextSetup::NewFrame();
	}

	void ImGuiContextLayer::OnPostRender(Application& app) {
		(void)app;
		if (!m_IsInitialized) {
			return;
		}
		EditorRuntime::ImGuiContextSetup::RenderAndPresent();

		FlushSettingsIfDirtyOrPeriodic();
	}

	void ImGuiContextLayer::ApplyEditorFontSize() {
		// _NextFrameFontSizeBase: documented mid-frame hack for FontSizeBase changes (see ImGui demo).
		ImGuiContext* ctx = ImGui::GetCurrentContext();
		if (ctx == nullptr) return;
		const float zoom = static_cast<float>(EditorPreferences::GetEditorFontZoomPercent()) / 100.0f;
		const float scaled = k_FunkeImGuiFontSize * zoom * s_DpiScale;
		ImGuiStyle& style = ImGui::GetStyle();
		style.FontSizeBase = scaled;
		style._NextFrameFontSizeBase = scaled;
	}

	void ImGuiContextLayer::FlushSettingsIfDirtyOrPeriodic() {
		constexpr float k_PeriodicSaveSeconds = 5.0f;

		if (!m_IsInitialized) return;
		ImGuiIO& io = ImGui::GetIO();
		if (io.IniFilename == nullptr || *io.IniFilename == '\0') return;

		const auto now = std::chrono::steady_clock::now();
		const float secondsSinceSave = std::chrono::duration<float>(
			now - m_LastSaveTime).count();

		const bool shouldSave = io.WantSaveIniSettings
			|| secondsSinceSave >= k_PeriodicSaveSeconds;
		if (!shouldSave) return;

		SyncDockIdsFromLiveNodes();
		ImGui::SaveIniSettingsToDisk(io.IniFilename);
		io.WantSaveIniSettings = false;
		m_LastSaveTime = now;
	}

	void ImGuiContextLayer::OnEvent(Application& app, FunkeEvent& event) {
		(void)app;
		if (!m_IsInitialized) return;
		const EventType type = event.GetEventType();
		if (type == EventType::WindowLostFocus || type == EventType::WindowClose) {
			ImGuiIO& io = ImGui::GetIO();
			if (io.IniFilename != nullptr && *io.IniFilename != '\0') {
				SyncDockIdsFromLiveNodes();
				ImGui::SaveIniSettingsToDisk(io.IniFilename);
				io.WantSaveIniSettings = false;
				m_LastSaveTime = std::chrono::steady_clock::now();
			}
		}
	}

	bool ImGuiContextLayer::IsValidLayoutPresetName(const std::string& name) {
		if (name.empty() || name.size() > 64) return false;
		if (name == "." || name == "..") return false;
		for (char c : name) {
			if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
				c == '"' || c == '<' || c == '>' || c == '|') {
				return false;
			}
			// Reject control chars; they're never typed deliberately and
			// produce filenames that can't be displayed in the menu.
			if (static_cast<unsigned char>(c) < 0x20) return false;
		}
		// Windows silently strips a trailing dot/space on save; refuse up front so ListLayoutPresets finds the file.
		if (name.back() == ' ' || name.back() == '.') return false;
		return true;
	}

	std::vector<std::string> ImGuiContextLayer::ListLayoutPresets() {
		std::vector<std::string> result;
		const std::filesystem::path dir = GetLayoutPresetsDirectory();
		std::error_code ec;
		if (!std::filesystem::is_directory(dir, ec)) {
			return result;
		}
		for (std::filesystem::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
			const std::filesystem::directory_entry& entry = *it;
			std::error_code ec2;
			if (!entry.is_regular_file(ec2) || ec2) continue;
			const std::filesystem::path& p = entry.path();
			if (p.extension() != ".ini") continue;
			result.push_back(p.stem().string());
		}
		std::sort(result.begin(), result.end(), [](const std::string& a, const std::string& b) {
			return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
				[](char x, char y) { return std::tolower((unsigned char)x) < std::tolower((unsigned char)y); });
		});
		return result;
	}

	bool ImGuiContextLayer::SaveLayoutPreset(const std::string& name) {
		if (!IsValidLayoutPresetName(name)) {
			FNK_CORE_WARN_TAG("ImGui",
				"SaveLayoutPreset: rejected invalid preset name '{}'.", name);
			return false;
		}
		const std::filesystem::path dir = GetLayoutPresetsDirectory();
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		if (ec) {
			FNK_CORE_WARN_TAG("ImGui",
				"SaveLayoutPreset: failed to create presets directory '{}': {}",
				dir.string(), ec.message());
			return false;
		}
		const std::filesystem::path presetPath = GetLayoutPresetPath(name);
		ImGui::SaveIniSettingsToDisk(presetPath.string().c_str());
		ec.clear();
		const bool exists = std::filesystem::is_regular_file(presetPath, ec);
		if (!exists) {
			FNK_CORE_WARN_TAG("ImGui",
				"SaveLayoutPreset: write failed (path not writable?): '{}'",
				presetPath.string());
			return false;
		}
		FNK_CORE_INFO_TAG("ImGui", "Saved layout preset '{}' → {}",
			name, presetPath.string());
		return true;
	}

	bool ImGuiContextLayer::LoadLayoutPreset(const std::string& name) {
		if (!IsValidLayoutPresetName(name)) {
			FNK_CORE_WARN_TAG("ImGui",
				"LoadLayoutPreset: rejected invalid preset name '{}'.", name);
			return false;
		}
		const std::filesystem::path presetPath = GetLayoutPresetPath(name);
		std::error_code ec;
		if (!std::filesystem::is_regular_file(presetPath, ec)) {
			FNK_CORE_WARN_TAG("ImGui",
				"LoadLayoutPreset: preset file not found at '{}'.",
				presetPath.string());
			return false;
		}
		ImGuiIO& io = ImGui::GetIO();
		if (io.IniFilename == nullptr || *io.IniFilename == '\0') {
			FNK_CORE_WARN_TAG("ImGui",
				"LoadLayoutPreset: no IniFilename set; ignoring.");
			return false;
		}
		// Write to the user ini immediately (survives a hard quit), then defer the in-context reload to start-of-next-frame (see s_PendingLayoutReloadPath).
		std::filesystem::copy_file(presetPath, io.IniFilename,
			std::filesystem::copy_options::overwrite_existing, ec);
		if (ec) {
			FNK_CORE_WARN_TAG("ImGui",
				"LoadLayoutPreset: failed to copy preset to user ini: {}",
				ec.message());
			return false;
		}
		s_PendingLayoutReloadPath = io.IniFilename;
		FNK_CORE_INFO_TAG("ImGui",
			"Queued layout preset '{}' for reload from {}",
			name, presetPath.string());
		return true;
	}

	bool ImGuiContextLayer::DeleteLayoutPreset(const std::string& name) {
		if (!IsValidLayoutPresetName(name)) {
			FNK_CORE_WARN_TAG("ImGui",
				"DeleteLayoutPreset: rejected invalid preset name '{}'.", name);
			return false;
		}
		const std::filesystem::path presetPath = GetLayoutPresetPath(name);
		std::error_code ec;
		const bool removed = std::filesystem::remove(presetPath, ec) && !ec;
		if (!removed) {
			FNK_CORE_WARN_TAG("ImGui",
				"DeleteLayoutPreset: remove failed for '{}': {}",
				presetPath.string(), ec ? ec.message() : "file did not exist");
			return false;
		}
		FNK_CORE_INFO_TAG("ImGui", "Deleted layout preset '{}' ({})",
			name, presetPath.string());
		return true;
	}

	void ImGuiContextLayer::ResetLayoutToDefault() {
		s_DockLayoutRequest = DockLayoutRequest::Default;
		FNK_CORE_INFO_TAG("ImGui", "Queued reset of the editor layout to the default.");
	}

	ImGuiContextLayer::DockLayoutRequest ImGuiContextLayer::ConsumeDockLayoutRequest() {
		return std::exchange(s_DockLayoutRequest, DockLayoutRequest::None);
	}

	void ImGuiContextLayer::ApplyFunkeTheme() {
		ImGuiStyle& style = ImGui::GetStyle();

		// ── Sizing & Rounding ───────────────────────────────────────
		style.WindowPadding     = ImVec2(10, 10);
		style.FramePadding      = ImVec2(7, 4);
		style.CellPadding       = ImVec2(5, 3);
		style.ItemSpacing       = ImVec2(8, 5);
		style.ItemInnerSpacing  = ImVec2(5, 4);
		style.IndentSpacing     = 16.0f;
		style.ScrollbarSize     = 11.0f;
		style.GrabMinSize       = 8.0f;

		SetRounding(style, k_DefaultRounding, 1.0f);
		style.DragDropTargetRounding = 4.0f;

		style.WindowBorderSize  = 1.0f;
		style.FrameBorderSize   = 0.0f;
		style.PopupBorderSize   = 1.0f;
		style.TabBorderSize     = 0.0f;
		style.TabBarBorderSize  = 1.0f;
		style.TabBarOverlineSize = 2.0f;

		style.WindowMenuButtonPosition = ImGuiDir_None;
		style.SeparatorTextBorderSize  = 2.0f;

		// Separate from sizing: EditorPreferences re-applies just the palette on theme switch without stomping DPI-scaled sizing.
		ApplyFunkeThemeColors();
	}

	// Neutral graphite with the blue interactive accent from EditorTheme (the only accent source).
	void ImGuiContextLayer::ApplyFunkeThemeColors(bool dark) {
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* c = style.Colors;

		auto pick = [dark](ImVec4 d, ImVec4 l) { return dark ? d : l; };
		auto withAlpha = [](ImVec4 v, float a) { return ImVec4(v.x, v.y, v.z, a); };

		const ImVec4 bg        = pick(ImVec4(0.110f, 0.114f, 0.125f, 1.00f), ImVec4(0.949f, 0.953f, 0.961f, 1.00f));
		const ImVec4 bgChild   = pick(ImVec4(0.118f, 0.122f, 0.133f, 1.00f), ImVec4(0.965f, 0.969f, 0.976f, 1.00f));
		const ImVec4 bgPopup   = pick(ImVec4(0.145f, 0.149f, 0.165f, 1.00f), ImVec4(1.000f, 1.000f, 1.000f, 1.00f));
		const ImVec4 chrome    = pick(ImVec4(0.086f, 0.090f, 0.098f, 1.00f), ImVec4(0.890f, 0.898f, 0.914f, 1.00f));
		const ImVec4 surface   = pick(ImVec4(0.165f, 0.171f, 0.188f, 1.00f), ImVec4(0.890f, 0.898f, 0.914f, 1.00f));
		const ImVec4 surfaceHi = pick(ImVec4(0.212f, 0.220f, 0.243f, 1.00f), ImVec4(0.843f, 0.855f, 0.878f, 1.00f));
		const ImVec4 surfaceAct= pick(ImVec4(0.255f, 0.265f, 0.294f, 1.00f), ImVec4(0.788f, 0.804f, 0.835f, 1.00f));
		const ImVec4 border    = pick(ImVec4(0.85f, 0.90f, 1.00f, 0.09f),    ImVec4(0.10f, 0.14f, 0.22f, 0.14f));
		const ImVec4 accent    = EditorTheme::Colors::AccentFor(dark);
		const ImVec4 highlight = EditorTheme::Colors::HighlightFor(dark);
		const ImVec4 text      = pick(ImVec4(0.922f, 0.925f, 0.937f, 1.00f), ImVec4(0.118f, 0.125f, 0.145f, 1.00f));
		const ImVec4 textDim   = pick(ImVec4(0.545f, 0.557f, 0.592f, 1.00f), ImVec4(0.447f, 0.463f, 0.502f, 1.00f));

		c[ImGuiCol_WindowBg]             = bg;
		c[ImGuiCol_ChildBg]              = bgChild;
		c[ImGuiCol_PopupBg]              = bgPopup;

		c[ImGuiCol_Border]               = border;
		c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

		c[ImGuiCol_Text]                 = text;
		c[ImGuiCol_TextDisabled]         = textDim;
		c[ImGuiCol_TextSelectedBg]       = withAlpha(accent, 0.35f);
		c[ImGuiCol_TextLink]             = accent;

		c[ImGuiCol_FrameBg]              = surface;
		c[ImGuiCol_FrameBgHovered]       = surfaceHi;
		c[ImGuiCol_FrameBgActive]        = surfaceAct;

		c[ImGuiCol_TitleBg]              = chrome;
		c[ImGuiCol_TitleBgActive]        = chrome;
		c[ImGuiCol_TitleBgCollapsed]     = withAlpha(chrome, 0.75f);

		c[ImGuiCol_MenuBarBg]            = chrome;

		c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_ScrollbarGrab]        = pick(ImVec4(0.255f, 0.271f, 0.306f, 1.00f), ImVec4(0.722f, 0.745f, 0.784f, 1.00f));
		c[ImGuiCol_ScrollbarGrabHovered] = pick(ImVec4(0.322f, 0.341f, 0.384f, 1.00f), ImVec4(0.631f, 0.659f, 0.710f, 1.00f));
		c[ImGuiCol_ScrollbarGrabActive]  = pick(ImVec4(0.400f, 0.424f, 0.475f, 1.00f), ImVec4(0.545f, 0.576f, 0.635f, 1.00f));

		c[ImGuiCol_Button]               = surface;
		c[ImGuiCol_ButtonHovered]        = surfaceHi;
		c[ImGuiCol_ButtonActive]         = surfaceAct;

		// Neutral: Header also fills collapsing headers. Hierarchy selection applies the accent explicitly.
		c[ImGuiCol_Header]               = surface;
		c[ImGuiCol_HeaderHovered]        = surfaceHi;
		c[ImGuiCol_HeaderActive]         = surfaceAct;

		c[ImGuiCol_Separator]            = border;
		c[ImGuiCol_SeparatorHovered]     = withAlpha(accent, 0.60f);
		c[ImGuiCol_SeparatorActive]      = accent;

		c[ImGuiCol_ResizeGrip]           = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_ResizeGripHovered]    = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_ResizeGripActive]     = ImVec4(0, 0, 0, 0);

		c[ImGuiCol_InputTextCursor]      = text;

		// Tabs sit on the chrome color; the selected one merges with its panel and gets an amber overline.
		// Selected tab is a lighter rounded "folder tab" on the darker tab strip.
		c[ImGuiCol_Tab]                  = chrome;
		c[ImGuiCol_TabHovered]           = surfaceHi;
		c[ImGuiCol_TabSelected]          = surface;
		c[ImGuiCol_TabSelectedOverline]  = highlight;
		c[ImGuiCol_TabDimmed]            = chrome;
		c[ImGuiCol_TabDimmedSelected]    = ImVec4((bg.x + surface.x) * 0.5f, (bg.y + surface.y) * 0.5f, (bg.z + surface.z) * 0.5f, 1.0f);
		c[ImGuiCol_TabDimmedSelectedOverline] = withAlpha(highlight, 0.0f);

		c[ImGuiCol_DockingPreview]       = withAlpha(accent, 0.40f);
		c[ImGuiCol_DockingEmptyBg]       = pick(ImVec4(0.070f, 0.075f, 0.086f, 1.00f), ImVec4(0.850f, 0.863f, 0.886f, 1.00f));

		c[ImGuiCol_PlotLines]            = accent;
		c[ImGuiCol_PlotLinesHovered]     = highlight;
		c[ImGuiCol_PlotHistogram]        = accent;
		c[ImGuiCol_PlotHistogramHovered] = highlight;

		c[ImGuiCol_CheckMark]            = highlight;
		c[ImGuiCol_SliderGrab]           = withAlpha(accent, 0.80f);
		c[ImGuiCol_SliderGrabActive]     = accent;

		c[ImGuiCol_DragDropTarget]       = accent;
		c[ImGuiCol_DragDropTargetBg]     = withAlpha(accent, 0.12f);

		c[ImGuiCol_NavCursor]            = accent;
		c[ImGuiCol_NavWindowingHighlight] = withAlpha(highlight, 0.70f);
		c[ImGuiCol_NavWindowingDimBg]    = pick(ImVec4(0, 0, 0, 0.40f), ImVec4(0.12f, 0.15f, 0.20f, 0.20f));

		c[ImGuiCol_TableHeaderBg]        = chrome;
		c[ImGuiCol_TableBorderStrong]    = border;
		c[ImGuiCol_TableBorderLight]     = withAlpha(border, border.w * 0.6f);
		c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_TableRowBgAlt]        = pick(ImVec4(1, 1, 1, 0.025f), ImVec4(0, 0, 0, 0.030f));

		c[ImGuiCol_ModalWindowDimBg]     = pick(ImVec4(0, 0, 0, 0.55f), ImVec4(0.12f, 0.15f, 0.20f, 0.30f));
	}

	// Docked panels stack on the dockspace host window, so the backdrop shows through two WindowBg layers (~45%).
	// Windows in their own OS window get WindowBg/PopupBg forced opaque by ImGui, hence the navy RGB that matches
	// their tinted caption. Surfaces keep a plausible opaque RGB because some widgets reuse them with their own alpha.
	void ImGuiContextLayer::ApplyGlassThemeColors() {
		ApplyFunkeThemeColors(/*dark=*/true);

		ImVec4* c = ImGui::GetStyle().Colors;
		const ImVec4 accent    = EditorTheme::Colors::AccentFor(true);
		const ImVec4 highlight = EditorTheme::Colors::HighlightFor(true);

		const ImVec4 surface   (0.36f, 0.42f, 0.56f, 0.22f);
		const ImVec4 surfaceHi (0.42f, 0.48f, 0.64f, 0.32f);
		const ImVec4 surfaceAct(0.48f, 0.55f, 0.72f, 0.42f);
		const ImVec4 chrome    (0.035f, 0.045f, 0.095f, 0.40f);
		const ImVec4 edge      (0.78f, 0.86f, 1.00f, 0.16f);
		const ImVec4 clear     (0.0f, 0.0f, 0.0f, 0.0f);

		c[ImGuiCol_WindowBg]             = ImVec4(0.05f, 0.065f, 0.13f, 0.34f);
		c[ImGuiCol_ChildBg]              = ImVec4(0.55f, 0.62f, 0.80f, 0.03f);
		c[ImGuiCol_PopupBg]              = ImVec4(0.07f, 0.085f, 0.16f, 0.90f);

		c[ImGuiCol_Border]               = edge;
		c[ImGuiCol_Separator]            = ImVec4(edge.x, edge.y, edge.z, 0.12f);
		c[ImGuiCol_TableBorderStrong]    = edge;
		c[ImGuiCol_TableBorderLight]     = ImVec4(edge.x, edge.y, edge.z, 0.08f);

		c[ImGuiCol_Text]                 = ImVec4(0.93f, 0.94f, 0.96f, 1.00f);
		c[ImGuiCol_TextDisabled]         = ImVec4(0.60f, 0.64f, 0.72f, 1.00f);

		c[ImGuiCol_FrameBg]              = surface;
		c[ImGuiCol_FrameBgHovered]       = surfaceHi;
		c[ImGuiCol_FrameBgActive]        = surfaceAct;
		c[ImGuiCol_Button]               = surface;
		c[ImGuiCol_ButtonHovered]        = surfaceHi;
		c[ImGuiCol_ButtonActive]         = surfaceAct;
		c[ImGuiCol_Header]               = surface;
		c[ImGuiCol_HeaderHovered]        = surfaceHi;
		c[ImGuiCol_HeaderActive]         = surfaceAct;

		c[ImGuiCol_TitleBg]              = chrome;
		c[ImGuiCol_TitleBgActive]        = chrome;
		c[ImGuiCol_TitleBgCollapsed]     = ImVec4(chrome.x, chrome.y, chrome.z, 0.30f);
		c[ImGuiCol_MenuBarBg]            = chrome;
		c[ImGuiCol_TableHeaderBg]        = ImVec4(0.55f, 0.62f, 0.80f, 0.06f);
		c[ImGuiCol_TableRowBgAlt]        = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);

		c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.70f, 0.78f, 0.95f, 0.22f);
		c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.70f, 0.78f, 0.95f, 0.32f);
		c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.70f, 0.78f, 0.95f, 0.42f);

		// Unselected tabs sit on the TitleBg strip; a fill of their own would double its tint.
		c[ImGuiCol_Tab]                  = clear;
		c[ImGuiCol_TabHovered]           = surfaceHi;
		c[ImGuiCol_TabSelected]          = surface;
		c[ImGuiCol_TabSelectedOverline]  = highlight;
		c[ImGuiCol_TabDimmed]            = clear;
		c[ImGuiCol_TabDimmedSelected]    = ImVec4(surface.x, surface.y, surface.z, 0.12f);

		c[ImGuiCol_SliderGrab]           = ImVec4(accent.x, accent.y, accent.z, 0.85f);
		c[ImGuiCol_DockingEmptyBg]       = ImVec4(0.0f, 0.0f, 0.0f, 0.20f);
		c[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.02f, 0.03f, 0.06f, 0.50f);
	}

	void ImGuiContextLayer::ApplyThemeRounding(bool glass) {
		SetRounding(ImGui::GetStyle(), glass ? k_GlassRounding : k_DefaultRounding, s_DpiScale);
	}

}
