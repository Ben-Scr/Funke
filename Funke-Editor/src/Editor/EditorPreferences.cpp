#include "Editor/EditorPreferences.hpp"

#include "Core/Log.hpp"
#include "Gui/GlassBackdrop.hpp"
#include "Gui/ImGuiContextLayer.hpp"
#include "Gui/ImGuiFonts.hpp"
#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SpecialFolder.hpp"
#include "Scripting/ScriptSystem.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

#include "Core/Platform.hpp"

namespace Funke {
	namespace {
		constexpr size_t k_EditorViewToolbarItemCount = static_cast<size_t>(EditorViewToolbarItem::Count);

		// JSON keys, indexed by EditorViewToolbarItem.
		constexpr const char* k_EditorViewToolbarKeys[k_EditorViewToolbarItemCount] = {
			"GizmoModes", "ViewMode3D", "DrawMode", "Gizmos", "PostProcessing", "GridSnap", "OptionsPill",
		};

		constexpr std::array<bool, k_EditorViewToolbarItemCount> AllToolbarItemsVisible() {
			std::array<bool, k_EditorViewToolbarItemCount> items{};
			for (bool& item : items) item = true;
			return items;
		}

		constexpr size_t k_GameViewToolbarItemCount = static_cast<size_t>(GameViewToolbarItem::Count);

		// JSON keys, indexed by GameViewToolbarItem.
		constexpr const char* k_GameViewToolbarKeys[k_GameViewToolbarItemCount] = {
			"Aspect", "VSync", "Stats", "Logs", "Toolbar",
		};

		constexpr std::array<bool, k_GameViewToolbarItemCount> AllGameViewToolbarItemsVisible() {
			std::array<bool, k_GameViewToolbarItemCount> items{};
			for (bool& item : items) item = true;
			return items;
		}

		// All state lives in the .cpp so the header stays light. The
		// class is a static facade — there's only ever one EditorPreferences
		// per editor process.
		struct State {
			EditorThemeMode Theme = EditorThemeMode::SystemDefault;
			// FUNKE_EDITOR_THEME shows another theme for one session; Save keeps writing the user's own.
			bool ThemeFromEnvironment = false;
			EditorThemeMode PersistedTheme = EditorThemeMode::SystemDefault;
			ImVec4 CustomColors[ImGuiCol_COUNT]{};
			bool CustomColorsSeeded = false;

			uint64_t EditorFontAssetId = k_DefaultFontAssetId;
			int EditorFontZoomPercent = EditorPreferences::k_DefaultEditorFontZoomPercent;
			float HierarchyRowScale = EditorPreferences::k_DefaultHierarchyRowScale;

			InspectorComponentView InspectorView = InspectorComponentView::Normal;

			bool ShowFileExtensions = false;
			bool AutoRecompileScripts = true;
			bool RecompileScriptsOnPlay = false;
			bool ExitPlayModeOnRecompilation = true;
			bool AutoSaveScenes = false;
			float AutoSaveIntervalSeconds = 120.0f;
			bool AutoSavePrefabs = true;
			bool AutoReloadExternalSceneChanges = true;
			bool ConfirmOnDeleteEntity = true;
			bool ConfirmOnDeleteAsset = true;

			EditorCamera3DSettings Camera3D;

			bool GridSnapEnabled = false;
			float GridSizeX = EditorPreferences::k_DefaultGridSize;
			float GridSizeY = EditorPreferences::k_DefaultGridSize;
			bool GridSnapLinkXY = true;

			bool RotationSnapEnabled = false;
			float RotationSnapDegrees = EditorPreferences::k_DefaultRotationSnap;
			bool ScaleSnapEnabled = false;
			float ScaleSnap = EditorPreferences::k_DefaultScaleSnap;

			bool AlignmentGuidesEnabled = true;
			float AlignmentSnapThreshold = EditorPreferences::k_DefaultAlignmentSnapThreshold;

			std::array<bool, static_cast<size_t>(EditorViewToolbarItem::Count)> EditorViewToolbar = AllToolbarItemsVisible();
			std::array<bool, static_cast<size_t>(GameViewToolbarItem::Count)> GameViewToolbar = AllGameViewToolbarItemsVisible();

			bool Loaded = false;
			bool FreshlyCreated = false;
			bool HasAppliedTheme = false;
			EditorThemeMode LastAppliedTheme = EditorThemeMode::SystemDefault;
		};

		State& S() {
			static State s;
			return s;
		}

		std::filesystem::path PrefsPath() {
			try {
				return std::filesystem::path(Path::GetSpecialFolderPath(SpecialFolder::LocalAppData))
					/ "Funke" / "Editor" / "EditorPreferences.json";
			} catch (...) {
				return std::filesystem::path("EditorPreferences.json");
			}
		}

		const char* ThemeModeToString(EditorThemeMode mode) {
			switch (mode) {
				case EditorThemeMode::Dark:          return "Dark";
				case EditorThemeMode::Light:         return "Light";
				case EditorThemeMode::SystemDefault: return "SystemDefault";
				case EditorThemeMode::Custom:        return "Custom";
				case EditorThemeMode::Glass:         return "Glass";
			}
			return "SystemDefault";
		}

		EditorThemeMode ThemeModeFromString(std::string_view value) {
			if (value == "Auto")          return EditorThemeMode::SystemDefault;
			if (value == "Dark")          return EditorThemeMode::Dark;
			if (value == "Light")         return EditorThemeMode::Light;
			if (value == "SystemDefault") return EditorThemeMode::SystemDefault;
			if (value == "Custom")        return EditorThemeMode::Custom;
			if (value == "Glass")         return EditorThemeMode::Glass;
			return EditorThemeMode::SystemDefault;
		}

		// Dev tooling: FUNKE_EDITOR_THEME=auto|dark|light|glass|custom (scripts/dev/Run.ps1 -Theme).
		void ApplyThemeFromEnvironment() {
			const char* value = std::getenv("FUNKE_EDITOR_THEME");
			if (!value || !*value) return;
			std::string name(value);
			std::transform(name.begin(), name.end(), name.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			EditorThemeMode mode;
			if (name == "auto")        mode = EditorThemeMode::SystemDefault;
			else if (name == "dark")   mode = EditorThemeMode::Dark;
			else if (name == "light")  mode = EditorThemeMode::Light;
			else if (name == "glass")  mode = EditorThemeMode::Glass;
			else if (name == "custom") mode = EditorThemeMode::Custom;
			else {
				FNK_CORE_WARN_TAG("EditorPrefs", "Ignoring unknown FUNKE_EDITOR_THEME '{}'.", value);
				return;
			}

			State& s = S();
			s.PersistedTheme = s.Theme;
			s.ThemeFromEnvironment = true;
			s.Theme = mode;
		}

		const char* InspectorViewToString(InspectorComponentView view) {
			switch (view) {
				case InspectorComponentView::Normal:   return "Normal";
				case InspectorComponentView::Detailed: return "Detailed";
			}
			return "Normal";
		}

		InspectorComponentView InspectorViewFromString(std::string_view value) {
			if (value == "Detailed") return InspectorComponentView::Detailed;
			return InspectorComponentView::Normal;
		}

		int NormalizeFontZoomPercent(int percent) {
			if (percent < EditorPreferences::k_MinEditorFontZoomPercent) {
				percent = EditorPreferences::k_MinEditorFontZoomPercent;
			}
			if (percent > EditorPreferences::k_MaxEditorFontZoomPercent) {
				percent = EditorPreferences::k_MaxEditorFontZoomPercent;
			}

			const int step = EditorPreferences::k_EditorFontZoomStepPercent;
			return ((percent + step / 2) / step) * step;
		}

		float ClampHierarchyRowScale(float scale) {
			if (scale < EditorPreferences::k_MinHierarchyRowScale) return EditorPreferences::k_MinHierarchyRowScale;
			if (scale > EditorPreferences::k_MaxHierarchyRowScale) return EditorPreferences::k_MaxHierarchyRowScale;
			return scale;
		}

		bool IsSystemDarkMode() {
			// Dark when the OS exposes no preference.
			return Platform::IsSystemDarkTheme().value_or(true);
		}

		EditorThemeMode ResolveTheme(EditorThemeMode mode) {
			if (mode == EditorThemeMode::SystemDefault) {
				return IsSystemDarkMode() ? EditorThemeMode::Dark : EditorThemeMode::Light;
			}
			return mode;
		}

		void MarkThemeApplied(EditorThemeMode resolved) {
			S().HasAppliedTheme = true;
			S().LastAppliedTheme = resolved;
		}

		float Luminance(const ImVec4& color) {
			return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
		}

		Color ToColor(const ImVec4& color) {
			return Color(color.x, color.y, color.z, color.w);
		}

		void ApplyNativeTitlebarFromStyle(bool glass = false) {
			Window* window = Application::GetWindow();
			if (!window) return;

			const ImGuiStyle& style = ImGui::GetStyle();
			const bool dark = Luminance(style.Colors[ImGuiCol_WindowBg]) < 0.5f;
			window->SetTitlebarDarkMode(dark);
			// Alpha 0 restores the OS default caption.
			window->SetTitlebarActiveColor(glass ? ToColor(GlassBackdrop::TitlebarColor(true)) : Color{ 0.0f, 0.0f, 0.0f, 0.0f });
			window->SetTitlebarInactiveColor(glass ? ToColor(GlassBackdrop::TitlebarColor(false)) : Color{ 0.0f, 0.0f, 0.0f, 0.0f });
			window->SetTitlebarTextColor(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
		}

		void SeedCustomColorsFromCurrent() {
			const ImGuiStyle& style = ImGui::GetStyle();
			std::memcpy(S().CustomColors, style.Colors, sizeof(ImVec4) * ImGuiCol_COUNT);
			S().CustomColorsSeeded = true;
		}

		Json::Value Vec4ToJson(const ImVec4& v) {
			Json::Value arr = Json::Value::MakeArray();
			arr.Append(Json::Value(static_cast<double>(v.x)));
			arr.Append(Json::Value(static_cast<double>(v.y)));
			arr.Append(Json::Value(static_cast<double>(v.z)));
			arr.Append(Json::Value(static_cast<double>(v.w)));
			return arr;
		}

		bool JsonToVec4(const Json::Value& v, ImVec4& out) {
			if (!v.IsArray()) return false;
			const Json::Value::Array& arr = v.GetArray();
			if (arr.size() != 4) return false;
			out.x = static_cast<float>(arr[0].AsDoubleOr(0.0));
			out.y = static_cast<float>(arr[1].AsDoubleOr(0.0));
			out.z = static_cast<float>(arr[2].AsDoubleOr(0.0));
			out.w = static_cast<float>(arr[3].AsDoubleOr(1.0));
			return true;
		}
	}

	void EditorPreferences::Load() {
		State& s = S();
		if (s.Loaded) return;
		s.Loaded = true;

		const std::filesystem::path path = PrefsPath();
		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec)) {
			s.FreshlyCreated = true;
			Save();
			ApplyThemeFromEnvironment();
			ScriptSystem::SetAutoRecompileEnabled(s.AutoRecompileScripts);
			return;
		}

		const std::string text = File::ReadAllText(path.string());
		if (text.empty()) {
			FNK_CORE_WARN_TAG("EditorPrefs", "EditorPreferences.json was empty; using defaults.");
			ApplyThemeFromEnvironment();
			return;
		}

		std::string parseError;
		Json::Value root = Json::Parse(text, &parseError);
		if (!root.IsObject()) {
			FNK_CORE_WARN_TAG("EditorPrefs",
				"Failed to parse EditorPreferences.json: {}", parseError);
			ApplyThemeFromEnvironment();
			return;
		}

		if (const Json::Value* v = root.FindMember("Theme")) {
			s.Theme = ThemeModeFromString(v->AsStringOr("SystemDefault"));
		}
		if (const Json::Value* v = root.FindMember("EditorFontAssetId")) {
			// Stored as a decimal string so 64-bit ids round-trip cleanly.
			// Same convention as FunkeProject::DefaultFontAssetId.
			s.EditorFontAssetId = v->IsString()
				? static_cast<uint64_t>(std::stoull(v->AsStringOr("0")))
				: v->AsUInt64Or(k_DefaultFontAssetId);
			if (s.EditorFontAssetId == 0) {
				s.EditorFontAssetId = k_DefaultFontAssetId;
			}
		}
		if (const Json::Value* v = root.FindMember("EditorFontZoomPercent")) {
			s.EditorFontZoomPercent = NormalizeFontZoomPercent(v->AsIntOr(
				EditorPreferences::k_DefaultEditorFontZoomPercent));
		}
		else if (const Json::Value* v = root.FindMember("EditorFontSize")) {
			const float size = static_cast<float>(v->AsDoubleOr(static_cast<double>(k_FunkeImGuiFontSize)));
			const int percent = static_cast<int>(std::lround((size / k_FunkeImGuiFontSize) * 100.0f));
			s.EditorFontZoomPercent = NormalizeFontZoomPercent(percent);
		}
		if (const Json::Value* v = root.FindMember("HierarchyRowScale")) {
			s.HierarchyRowScale = ClampHierarchyRowScale(
				static_cast<float>(v->AsDoubleOr(k_DefaultHierarchyRowScale)));
		}
		if (const Json::Value* v = root.FindMember("InspectorComponentView")) {
			s.InspectorView = InspectorViewFromString(v->AsStringOr("Normal"));
		}
		if (const Json::Value* v = root.FindMember("ShowFileExtensions")) {
			s.ShowFileExtensions = v->AsBoolOr(false);
		}
		if (const Json::Value* v = root.FindMember("AutoRecompileScripts")) {
			s.AutoRecompileScripts = v->AsBoolOr(true);
		}
		if (const Json::Value* v = root.FindMember("RecompileScriptsOnPlay")) {
			s.RecompileScriptsOnPlay = v->AsBoolOr(false);
		}
		if (const Json::Value* v = root.FindMember("ExitPlayModeOnRecompilation")) {
			s.ExitPlayModeOnRecompilation = v->AsBoolOr(true);
		}
		if (const Json::Value* v = root.FindMember("AutoSaveScenes")) {
			s.AutoSaveScenes = v->AsBoolOr(false);
		}
		if (const Json::Value* v = root.FindMember("AutoSaveIntervalSeconds")) {
			s.AutoSaveIntervalSeconds = static_cast<float>(v->AsDoubleOr(120.0));
			if (s.AutoSaveIntervalSeconds < 5.0f) s.AutoSaveIntervalSeconds = 5.0f;
		}
		if (const Json::Value* v = root.FindMember("AutoSavePrefabs")) {
			s.AutoSavePrefabs = v->AsBoolOr(true);
		}
		if (const Json::Value* v = root.FindMember("AutoReloadExternalSceneChanges")) {
			s.AutoReloadExternalSceneChanges = v->AsBoolOr(true);
		}
		// Back-compat: the single ConfirmOnDelete pref was split into separate
		// entity/asset toggles. Seed both from the legacy key when present so an
		// existing "off" choice carries over, then let the new keys override.
		if (const Json::Value* v = root.FindMember("ConfirmOnDelete")) {
			const bool legacy = v->AsBoolOr(true);
			s.ConfirmOnDeleteEntity = legacy;
			s.ConfirmOnDeleteAsset = legacy;
		}
		if (const Json::Value* v = root.FindMember("ConfirmOnDeleteEntity")) {
			s.ConfirmOnDeleteEntity = v->AsBoolOr(true);
		}
		if (const Json::Value* v = root.FindMember("ConfirmOnDeleteAsset")) {
			s.ConfirmOnDeleteAsset = v->AsBoolOr(true);
		}
		if (const Json::Value* camera = root.FindMember("Camera3D"); camera && camera->IsObject()) {
			EditorCamera3DSettings settings;
			if (const Json::Value* mode = camera->FindMember("Mode"); mode && mode->IsString()) {
				settings.NavigationMode = mode->AsStringOr() == "Fly"
					? EditorCamera3DNavigationMode::Fly : EditorCamera3DNavigationMode::Orbit;
			}
			auto readFloat = [&](const char* key, float& value, float minimum, float maximum) {
				if (const Json::Value* member = camera->FindMember(key); member && member->IsNumber()) {
					const double number = member->AsDoubleOr(value);
					if (std::isfinite(number)) {
						value = static_cast<float>(std::clamp(number, static_cast<double>(minimum), static_cast<double>(maximum)));
					}
				}
			};
			auto readBool = [&](const char* key, bool& value) {
				if (const Json::Value* member = camera->FindMember(key); member && member->IsBool()) {
					value = member->AsBoolOr(value);
				}
			};
			readFloat("MoveSpeed", settings.MoveSpeed, EditorCamera3DSettings::k_MinMoveSpeed, EditorCamera3DSettings::k_MaxMoveSpeed);
			readFloat("BoostMultiplier", settings.BoostMultiplier, EditorCamera3DSettings::k_MinBoostMultiplier, EditorCamera3DSettings::k_MaxBoostMultiplier);
			readFloat("LookSensitivity", settings.LookSensitivity, EditorCamera3DSettings::k_MinSensitivity, EditorCamera3DSettings::k_MaxSensitivity);
			readFloat("PanSensitivity", settings.PanSensitivity, EditorCamera3DSettings::k_MinSensitivity, EditorCamera3DSettings::k_MaxSensitivity);
			readFloat("ZoomSensitivity", settings.ZoomSensitivity, EditorCamera3DSettings::k_MinSensitivity, EditorCamera3DSettings::k_MaxSensitivity);
			readFloat("FieldOfView", settings.FieldOfView, EditorCamera3DSettings::k_MinFieldOfView, EditorCamera3DSettings::k_MaxFieldOfView);
			readBool("ScaleSpeedWithDistance", settings.ScaleSpeedWithDistance);
			readBool("InvertLookX", settings.InvertLookX);
			readBool("InvertLookY", settings.InvertLookY);
			readBool("FlyWorldUp", settings.FlyWorldUp);
			settings.Sanitize();
			s.Camera3D = settings;
		}
		if (const Json::Value* v = root.FindMember("GridSnapEnabled")) {
			s.GridSnapEnabled = v->AsBoolOr(false);
		}
		if (const Json::Value* v = root.FindMember("GridSizeX")) {
			s.GridSizeX = static_cast<float>(v->AsDoubleOr(EditorPreferences::k_DefaultGridSize));
			if (s.GridSizeX < EditorPreferences::k_MinGridSize) s.GridSizeX = EditorPreferences::k_MinGridSize;
		}
		if (const Json::Value* v = root.FindMember("GridSizeY")) {
			s.GridSizeY = static_cast<float>(v->AsDoubleOr(EditorPreferences::k_DefaultGridSize));
			if (s.GridSizeY < EditorPreferences::k_MinGridSize) s.GridSizeY = EditorPreferences::k_MinGridSize;
		}
		if (const Json::Value* v = root.FindMember("GridSnapLinkXY")) {
			s.GridSnapLinkXY = v->AsBoolOr(true);
		}
		if (const Json::Value* v = root.FindMember("RotationSnapEnabled")) {
			s.RotationSnapEnabled = v->AsBoolOr(false);
		}
		if (const Json::Value* v = root.FindMember("RotationSnapDegrees")) {
			s.RotationSnapDegrees = static_cast<float>(v->AsDoubleOr(EditorPreferences::k_DefaultRotationSnap));
			if (s.RotationSnapDegrees < EditorPreferences::k_MinRotationSnap) s.RotationSnapDegrees = EditorPreferences::k_MinRotationSnap;
		}
		if (const Json::Value* v = root.FindMember("ScaleSnapEnabled")) {
			s.ScaleSnapEnabled = v->AsBoolOr(false);
		}
		if (const Json::Value* v = root.FindMember("ScaleSnap")) {
			s.ScaleSnap = static_cast<float>(v->AsDoubleOr(EditorPreferences::k_DefaultScaleSnap));
			if (s.ScaleSnap < EditorPreferences::k_MinScaleSnap) s.ScaleSnap = EditorPreferences::k_MinScaleSnap;
		}
		if (const Json::Value* v = root.FindMember("AlignmentGuidesEnabled")) {
			s.AlignmentGuidesEnabled = v->AsBoolOr(true);
		}
		if (const Json::Value* v = root.FindMember("AlignmentSnapThreshold")) {
			s.AlignmentSnapThreshold = static_cast<float>(v->AsDoubleOr(EditorPreferences::k_DefaultAlignmentSnapThreshold));
			if (s.AlignmentSnapThreshold < EditorPreferences::k_MinAlignmentSnapThreshold) s.AlignmentSnapThreshold = EditorPreferences::k_MinAlignmentSnapThreshold;
		}
		if (const Json::Value* v = root.FindMember("EditorViewToolbar"); v && v->IsObject()) {
			for (size_t i = 0; i < k_EditorViewToolbarItemCount; ++i) {
				if (const Json::Value* item = v->FindMember(k_EditorViewToolbarKeys[i])) {
					s.EditorViewToolbar[i] = item->AsBoolOr(true);
				}
			}
		}
		if (const Json::Value* v = root.FindMember("GameViewToolbar"); v && v->IsObject()) {
			for (size_t i = 0; i < k_GameViewToolbarItemCount; ++i) {
				if (const Json::Value* item = v->FindMember(k_GameViewToolbarKeys[i])) {
					s.GameViewToolbar[i] = item->AsBoolOr(true);
				}
			}
		}

		if (const Json::Value* v = root.FindMember("CustomColors"); v && v->IsObject()) {
			ImGuiStyle tempStyle;
			ImGui::StyleColorsDark(&tempStyle);
			std::memcpy(s.CustomColors, tempStyle.Colors, sizeof(ImVec4) * ImGuiCol_COUNT);
			for (int i = 0; i < ImGuiCol_COUNT; i++) {
				const char* name = ImGui::GetStyleColorName(i);
				if (!name) continue;
				if (const Json::Value* entry = v->FindMember(name)) {
					ImVec4 parsed;
					if (JsonToVec4(*entry, parsed)) {
						s.CustomColors[i] = parsed;
					}
				}
			}
			s.CustomColorsSeeded = true;
		}

		ApplyThemeFromEnvironment();
		ImGuiContextLayer::ApplyEditorFontSize();
		ScriptSystem::SetAutoRecompileEnabled(s.AutoRecompileScripts);
	}

	bool EditorPreferences::WasFreshlyCreated() {
		return S().FreshlyCreated;
	}

	void EditorPreferences::Save() {
		const State& s = S();
		const std::filesystem::path path = PrefsPath();
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);

		Json::Value root = Json::Value::MakeObject();
		root.AddMember("Theme", Json::Value(std::string(ThemeModeToString(
			s.ThemeFromEnvironment ? s.PersistedTheme : s.Theme))));
		root.AddMember("EditorFontAssetId", Json::Value(std::to_string(s.EditorFontAssetId)));
		root.AddMember("EditorFontZoomPercent", Json::Value(s.EditorFontZoomPercent));
		root.AddMember("HierarchyRowScale", Json::Value(static_cast<double>(s.HierarchyRowScale)));
		root.AddMember("InspectorComponentView", Json::Value(std::string(InspectorViewToString(s.InspectorView))));
		root.AddMember("ShowFileExtensions", Json::Value(s.ShowFileExtensions));
		root.AddMember("AutoRecompileScripts", Json::Value(s.AutoRecompileScripts));
		root.AddMember("RecompileScriptsOnPlay", Json::Value(s.RecompileScriptsOnPlay));
		root.AddMember("ExitPlayModeOnRecompilation", Json::Value(s.ExitPlayModeOnRecompilation));
		root.AddMember("AutoSaveScenes", Json::Value(s.AutoSaveScenes));
		root.AddMember("AutoSaveIntervalSeconds", Json::Value(static_cast<double>(s.AutoSaveIntervalSeconds)));
		root.AddMember("AutoSavePrefabs", Json::Value(s.AutoSavePrefabs));
		root.AddMember("AutoReloadExternalSceneChanges", Json::Value(s.AutoReloadExternalSceneChanges));
		root.AddMember("ConfirmOnDeleteEntity", Json::Value(s.ConfirmOnDeleteEntity));
		root.AddMember("ConfirmOnDeleteAsset", Json::Value(s.ConfirmOnDeleteAsset));
		{
			const EditorCamera3DSettings& settings = s.Camera3D;
			Json::Value camera = Json::Value::MakeObject();
			camera.AddMember("Mode", Json::Value(std::string(settings.NavigationMode == EditorCamera3DNavigationMode::Fly ? "Fly" : "Orbit")));
			camera.AddMember("MoveSpeed", Json::Value(static_cast<double>(settings.MoveSpeed)));
			camera.AddMember("BoostMultiplier", Json::Value(static_cast<double>(settings.BoostMultiplier)));
			camera.AddMember("LookSensitivity", Json::Value(static_cast<double>(settings.LookSensitivity)));
			camera.AddMember("PanSensitivity", Json::Value(static_cast<double>(settings.PanSensitivity)));
			camera.AddMember("ZoomSensitivity", Json::Value(static_cast<double>(settings.ZoomSensitivity)));
			camera.AddMember("FieldOfView", Json::Value(static_cast<double>(settings.FieldOfView)));
			camera.AddMember("ScaleSpeedWithDistance", Json::Value(settings.ScaleSpeedWithDistance));
			camera.AddMember("InvertLookX", Json::Value(settings.InvertLookX));
			camera.AddMember("InvertLookY", Json::Value(settings.InvertLookY));
			camera.AddMember("FlyWorldUp", Json::Value(settings.FlyWorldUp));
			root.AddMember("Camera3D", std::move(camera));
		}
		root.AddMember("GridSnapEnabled", Json::Value(s.GridSnapEnabled));
		root.AddMember("GridSizeX", Json::Value(static_cast<double>(s.GridSizeX)));
		root.AddMember("GridSizeY", Json::Value(static_cast<double>(s.GridSizeY)));
		root.AddMember("GridSnapLinkXY", Json::Value(s.GridSnapLinkXY));
		root.AddMember("RotationSnapEnabled", Json::Value(s.RotationSnapEnabled));
		root.AddMember("RotationSnapDegrees", Json::Value(static_cast<double>(s.RotationSnapDegrees)));
		root.AddMember("ScaleSnapEnabled", Json::Value(s.ScaleSnapEnabled));
		root.AddMember("ScaleSnap", Json::Value(static_cast<double>(s.ScaleSnap)));
		root.AddMember("AlignmentGuidesEnabled", Json::Value(s.AlignmentGuidesEnabled));
		root.AddMember("AlignmentSnapThreshold", Json::Value(static_cast<double>(s.AlignmentSnapThreshold)));
		{
			Json::Value toolbar = Json::Value::MakeObject();
			for (size_t i = 0; i < k_EditorViewToolbarItemCount; ++i) {
				toolbar.AddMember(std::string(k_EditorViewToolbarKeys[i]), Json::Value(s.EditorViewToolbar[i]));
			}
			root.AddMember("EditorViewToolbar", std::move(toolbar));
		}
		{
			Json::Value toolbar = Json::Value::MakeObject();
			for (size_t i = 0; i < k_GameViewToolbarItemCount; ++i) {
				toolbar.AddMember(std::string(k_GameViewToolbarKeys[i]), Json::Value(s.GameViewToolbar[i]));
			}
			root.AddMember("GameViewToolbar", std::move(toolbar));
		}

		// Always serialize CustomColors once seeded — the user can return
		// to the saved set after experimenting with Dark/Light/System.
		if (s.CustomColorsSeeded) {
			Json::Value colors = Json::Value::MakeObject();
			for (int i = 0; i < ImGuiCol_COUNT; i++) {
				const char* name = ImGui::GetStyleColorName(i);
				if (!name) continue;
				colors.AddMember(std::string(name), Vec4ToJson(s.CustomColors[i]));
			}
			root.AddMember("CustomColors", std::move(colors));
		}

		const std::string text = Json::Stringify(root, /*pretty*/ true);
		if (!File::WriteAllText(path.string(), text)) {
			FNK_CORE_WARN_TAG("EditorPrefs",
				"Failed to write EditorPreferences.json to '{}'.", path.string());
		}
	}

	void EditorPreferences::ApplyTheme() {
		const EditorThemeMode resolved = ResolveTheme(S().Theme);
		ImGuiContextLayer::ApplyThemeRounding(/*glass=*/resolved == EditorThemeMode::Glass);

		switch (resolved) {
			case EditorThemeMode::Glass:
				ImGuiContextLayer::ApplyGlassThemeColors();
				MarkThemeApplied(resolved);
				ApplyNativeTitlebarFromStyle(/*glass=*/true);
				return;
			case EditorThemeMode::Light:
				ImGuiContextLayer::ApplyFunkeThemeColors(/*dark=*/false);
				MarkThemeApplied(resolved);
				ApplyNativeTitlebarFromStyle();
				return;
			case EditorThemeMode::Dark:
			case EditorThemeMode::SystemDefault: // already resolved above
				ImGuiContextLayer::ApplyFunkeThemeColors();
				MarkThemeApplied(resolved);
				ApplyNativeTitlebarFromStyle();
				return;
			case EditorThemeMode::Custom: {
				if (!S().CustomColorsSeeded) {
					SeedCustomColorsFromCurrent();
				}
				ImGuiStyle& style = ImGui::GetStyle();
				std::memcpy(style.Colors, S().CustomColors, sizeof(ImVec4) * ImGuiCol_COUNT);
				MarkThemeApplied(resolved);
				ApplyNativeTitlebarFromStyle();
				return;
			}
		}
	}

	void EditorPreferences::ApplySystemThemeIfNeeded() {
		State& s = S();
		if (s.Theme != EditorThemeMode::SystemDefault) {
			return;
		}

		const EditorThemeMode resolved = ResolveTheme(s.Theme);
		if (s.HasAppliedTheme && s.LastAppliedTheme == resolved) {
			return;
		}

		ApplyTheme();
	}

	EditorThemeMode EditorPreferences::GetResolvedThemeMode() {
		return ResolveTheme(S().Theme);
	}

	EditorThemeMode EditorPreferences::GetResolvedThemeMode(EditorThemeMode mode) {
		return ResolveTheme(mode);
	}

	EditorThemeMode EditorPreferences::GetThemeMode() {
		return S().Theme;
	}

	void EditorPreferences::SetThemeMode(EditorThemeMode mode) {
		if (S().Theme == mode) return;
		S().ThemeFromEnvironment = false;

		if (mode == EditorThemeMode::Custom && !S().CustomColorsSeeded) {
			SeedCustomColorsFromCurrent();
		}

		S().Theme = mode;
		ApplyTheme();
		Save();
	}

	ImVec4& EditorPreferences::CustomColor(int imGuiColIdx) {
		assert(imGuiColIdx >= 0 && imGuiColIdx < ImGuiCol_COUNT);
		if (!S().CustomColorsSeeded) {
			SeedCustomColorsFromCurrent();
		}
		return S().CustomColors[imGuiColIdx];
	}

	void EditorPreferences::ResetCustomColorsToDefault() {
		// Seed from the resolved default theme (Dark/Light per the OS), NOT the
		// live style: in Custom mode the live style already *is* the custom
		// colors, so reseeding from it was a no-op. Build the base palette on the
		// live style, capture it, then restore so the caller's ApplyTheme() is
		// what actually pushes the reset colors back on screen.
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4 saved[ImGuiCol_COUNT];
		std::memcpy(saved, style.Colors, sizeof(saved));

		if (ResolveTheme(EditorThemeMode::SystemDefault) == EditorThemeMode::Light) {
			ImGuiContextLayer::ApplyFunkeThemeColors(/*dark=*/false);
		}
		else {
			ImGuiContextLayer::ApplyFunkeThemeColors();
		}
		std::memcpy(S().CustomColors, style.Colors, sizeof(ImVec4) * ImGuiCol_COUNT);
		S().CustomColorsSeeded = true;

		std::memcpy(style.Colors, saved, sizeof(saved));
		Save();
	}

	uint64_t EditorPreferences::GetEditorFontAssetId() {
		return S().EditorFontAssetId;
	}

	void EditorPreferences::SetEditorFontAssetId(uint64_t id) {
		const uint64_t normalized = (id == 0) ? k_DefaultFontAssetId : id;
		if (S().EditorFontAssetId == normalized) return;
		S().EditorFontAssetId = normalized;
		Save();
	}

	int EditorPreferences::GetEditorFontZoomPercent() {
		return S().EditorFontZoomPercent;
	}

	void EditorPreferences::SetEditorFontZoomPercent(int percent) {
		const int normalized = NormalizeFontZoomPercent(percent);
		if (S().EditorFontZoomPercent == normalized) return;
		S().EditorFontZoomPercent = normalized;
		Save();
		ImGuiContextLayer::ApplyEditorFontSize();
	}

	float EditorPreferences::GetHierarchyRowScale() {
		return S().HierarchyRowScale;
	}

	void EditorPreferences::SetHierarchyRowScale(float value) {
		const float clamped = ClampHierarchyRowScale(value);
		if (S().HierarchyRowScale == clamped) return;
		S().HierarchyRowScale = clamped;
		Save();
	}

	InspectorComponentView EditorPreferences::GetInspectorComponentView() {
		return S().InspectorView;
	}

	void EditorPreferences::SetInspectorComponentView(InspectorComponentView value) {
		if (S().InspectorView == value) return;
		S().InspectorView = value;
		Save();
	}

	bool EditorPreferences::GetShowFileExtensions() {
		return S().ShowFileExtensions;
	}

	void EditorPreferences::SetShowFileExtensions(bool value) {
		if (S().ShowFileExtensions == value) return;
		S().ShowFileExtensions = value;
		Save();
	}

	bool EditorPreferences::GetAutoRecompileScripts() {
		return S().AutoRecompileScripts;
	}

	void EditorPreferences::SetAutoRecompileScripts(bool value) {
		if (S().AutoRecompileScripts == value) return;
		S().AutoRecompileScripts = value;
		ScriptSystem::SetAutoRecompileEnabled(value);
		Save();
	}

	bool EditorPreferences::GetRecompileScriptsOnPlay() {
		return S().RecompileScriptsOnPlay;
	}

	void EditorPreferences::SetRecompileScriptsOnPlay(bool value) {
		if (S().RecompileScriptsOnPlay == value) return;
		S().RecompileScriptsOnPlay = value;
		Save();
	}

	bool EditorPreferences::GetExitPlayModeOnRecompilation() {
		return S().ExitPlayModeOnRecompilation;
	}

	void EditorPreferences::SetExitPlayModeOnRecompilation(bool value) {
		if (S().ExitPlayModeOnRecompilation == value) return;
		S().ExitPlayModeOnRecompilation = value;
		Save();
	}

	bool EditorPreferences::GetAutoSaveScenes() {
		return S().AutoSaveScenes;
	}

	void EditorPreferences::SetAutoSaveScenes(bool value) {
		if (S().AutoSaveScenes == value) return;
		S().AutoSaveScenes = value;
		Save();
	}

	float EditorPreferences::GetAutoSaveIntervalSeconds() {
		return S().AutoSaveIntervalSeconds;
	}

	void EditorPreferences::SetAutoSaveIntervalSeconds(float seconds) {
		const float clamped = (seconds < 5.0f) ? 5.0f : seconds;
		if (S().AutoSaveIntervalSeconds == clamped) return;
		S().AutoSaveIntervalSeconds = clamped;
		Save();
	}

	bool EditorPreferences::GetAutoSavePrefabs() {
		return S().AutoSavePrefabs;
	}

	void EditorPreferences::SetAutoSavePrefabs(bool value) {
		if (S().AutoSavePrefabs == value) return;
		S().AutoSavePrefabs = value;
		Save();
	}

	bool EditorPreferences::GetAutoReloadExternalSceneChanges() {
		return S().AutoReloadExternalSceneChanges;
	}

	void EditorPreferences::SetAutoReloadExternalSceneChanges(bool value) {
		if (S().AutoReloadExternalSceneChanges == value) return;
		S().AutoReloadExternalSceneChanges = value;
		Save();
	}

	bool EditorPreferences::GetConfirmOnDeleteEntity() {
		return S().ConfirmOnDeleteEntity;
	}

	void EditorPreferences::SetConfirmOnDeleteEntity(bool value) {
		if (S().ConfirmOnDeleteEntity == value) return;
		S().ConfirmOnDeleteEntity = value;
		Save();
	}

	bool EditorPreferences::GetConfirmOnDeleteAsset() {
		return S().ConfirmOnDeleteAsset;
	}

	void EditorPreferences::SetConfirmOnDeleteAsset(bool value) {
		if (S().ConfirmOnDeleteAsset == value) return;
		S().ConfirmOnDeleteAsset = value;
		Save();
	}

	EditorCamera3DSettings EditorPreferences::GetCamera3DSettings() {
		return S().Camera3D;
	}

	void EditorPreferences::SetCamera3DSettings(const EditorCamera3DSettings& settings) {
		EditorCamera3DSettings sanitized = settings;
		sanitized.Sanitize();
		if (S().Camera3D == sanitized) return;
		S().Camera3D = sanitized;
		Save();
	}

	void EditorPreferences::ResetCamera3DSettings() {
		SetCamera3DSettings(EditorCamera3DSettings{});
	}

	bool EditorPreferences::GetGridSnapEnabled() {
		return S().GridSnapEnabled;
	}

	void EditorPreferences::SetGridSnapEnabled(bool value) {
		if (S().GridSnapEnabled == value) return;
		S().GridSnapEnabled = value;
		Save();
	}

	float EditorPreferences::GetGridSizeX() {
		return S().GridSizeX;
	}

	void EditorPreferences::SetGridSizeX(float value) {
		const float clamped = (value < k_MinGridSize) ? k_MinGridSize : value;
		if (S().GridSizeX == clamped) return;
		S().GridSizeX = clamped;
		Save();
	}

	float EditorPreferences::GetGridSizeY() {
		return S().GridSizeY;
	}

	void EditorPreferences::SetGridSizeY(float value) {
		const float clamped = (value < k_MinGridSize) ? k_MinGridSize : value;
		if (S().GridSizeY == clamped) return;
		S().GridSizeY = clamped;
		Save();
	}

	bool EditorPreferences::GetGridSnapLinkXY() {
		return S().GridSnapLinkXY;
	}

	void EditorPreferences::SetGridSnapLinkXY(bool value) {
		if (S().GridSnapLinkXY == value) return;
		S().GridSnapLinkXY = value;
		Save();
	}

	bool EditorPreferences::GetRotationSnapEnabled() {
		return S().RotationSnapEnabled;
	}

	void EditorPreferences::SetRotationSnapEnabled(bool value) {
		if (S().RotationSnapEnabled == value) return;
		S().RotationSnapEnabled = value;
		Save();
	}

	float EditorPreferences::GetRotationSnapDegrees() {
		return S().RotationSnapDegrees;
	}

	void EditorPreferences::SetRotationSnapDegrees(float value) {
		const float clamped = (value < k_MinRotationSnap) ? k_MinRotationSnap : value;
		if (S().RotationSnapDegrees == clamped) return;
		S().RotationSnapDegrees = clamped;
		Save();
	}

	bool EditorPreferences::GetScaleSnapEnabled() {
		return S().ScaleSnapEnabled;
	}

	void EditorPreferences::SetScaleSnapEnabled(bool value) {
		if (S().ScaleSnapEnabled == value) return;
		S().ScaleSnapEnabled = value;
		Save();
	}

	float EditorPreferences::GetScaleSnap() {
		return S().ScaleSnap;
	}

	void EditorPreferences::SetScaleSnap(float value) {
		const float clamped = (value < k_MinScaleSnap) ? k_MinScaleSnap : value;
		if (S().ScaleSnap == clamped) return;
		S().ScaleSnap = clamped;
		Save();
	}

	bool EditorPreferences::GetAlignmentGuidesEnabled() {
		return S().AlignmentGuidesEnabled;
	}

	void EditorPreferences::SetAlignmentGuidesEnabled(bool value) {
		if (S().AlignmentGuidesEnabled == value) return;
		S().AlignmentGuidesEnabled = value;
		Save();
	}

	float EditorPreferences::GetAlignmentSnapThreshold() {
		return S().AlignmentSnapThreshold;
	}

	void EditorPreferences::SetAlignmentSnapThreshold(float value) {
		const float clamped = (value < k_MinAlignmentSnapThreshold) ? k_MinAlignmentSnapThreshold : value;
		if (S().AlignmentSnapThreshold == clamped) return;
		S().AlignmentSnapThreshold = clamped;
		Save();
	}

	bool EditorPreferences::IsEditorViewToolbarItemVisible(EditorViewToolbarItem item) {
		const size_t index = static_cast<size_t>(item);
		return index >= k_EditorViewToolbarItemCount || S().EditorViewToolbar[index];
	}

	void EditorPreferences::SetEditorViewToolbarItemVisible(EditorViewToolbarItem item, bool visible) {
		const size_t index = static_cast<size_t>(item);
		if (index >= k_EditorViewToolbarItemCount || S().EditorViewToolbar[index] == visible) return;
		S().EditorViewToolbar[index] = visible;
		Save();
	}

	bool EditorPreferences::IsEditorViewToolbarDefault() {
		return S().EditorViewToolbar == AllToolbarItemsVisible();
	}

	void EditorPreferences::ResetEditorViewToolbar() {
		if (IsEditorViewToolbarDefault()) return;
		S().EditorViewToolbar = AllToolbarItemsVisible();
		Save();
	}

	bool EditorPreferences::IsGameViewToolbarItemVisible(GameViewToolbarItem item) {
		const size_t index = static_cast<size_t>(item);
		return index >= k_GameViewToolbarItemCount || S().GameViewToolbar[index];
	}

	void EditorPreferences::SetGameViewToolbarItemVisible(GameViewToolbarItem item, bool visible) {
		const size_t index = static_cast<size_t>(item);
		if (index >= k_GameViewToolbarItemCount || S().GameViewToolbar[index] == visible) return;
		S().GameViewToolbar[index] = visible;
		Save();
	}

	bool EditorPreferences::IsGameViewToolbarDefault() {
		return S().GameViewToolbar == AllGameViewToolbarItemsVisible();
	}

	void EditorPreferences::ResetGameViewToolbar() {
		if (IsGameViewToolbarDefault()) return;
		S().GameViewToolbar = AllGameViewToolbarItemsVisible();
		Save();
	}

}
