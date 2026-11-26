#include "Systems/LauncherLayer.hpp"
#include "Gui/LauncherUI.hpp"
#include "Gui/ImGuiDialog.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Localization/Localization.hpp"
#include "Core/Log.hpp"
#include "Serialization/Path.hpp"
#include "Core/Application.hpp"
#include "Core/Platform.hpp"
#include "Gui/ImGuiContextLayer.hpp"
#include "Gui/ImGuiFonts.hpp"
#include "Gui/ThemePicker.hpp"
#include "Utils/Process.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Funke {
	using namespace LauncherUI;

	// Restart is the only way to add CJK glyphs: the atlas is baked statically at startup (no RendererHasTextures).
	static std::string ResolveOwnExecutablePath() {
		return Path::ToUtf8(Platform::GetExecutablePath());
	}

	// Spawns a new instance of the current binary and then quits the
	// running one. Used by the CJK-font restart prompt.
	static void RestartLauncherProcess() {
		const std::string exePath = ResolveOwnExecutablePath();
		if (exePath.empty()) {
			FNK_CORE_WARN_TAG("Launcher",
				"Cannot relaunch: failed to resolve own executable path.");
			return;
		}
		if (!Process::LaunchDetached({ exePath })) {
			FNK_CORE_WARN_TAG("Launcher",
				"Cannot relaunch: LaunchDetached failed for '{}'.", exePath);
			return;
		}
		FNK_CORE_INFO_TAG("Launcher", "Relaunching '{}'.", exePath);
		Application::Quit();
	}

	float LauncherLayer::GetEffectiveFontScale() const {
		switch (m_Settings.Scale) {
			case FontScale::Auto:   return 1.0f;
			case FontScale::P75:    return 0.75f;
			case FontScale::P100:   return 1.0f;
			case FontScale::P125:   return 1.25f;
			case FontScale::P150:   return 1.50f;
			case FontScale::P175:   return 1.75f;
			case FontScale::P200:   return 2.00f;
		}
		return 1.0f;
	}

	bool LauncherLayer::IsEffectiveThemeDark() const {
		switch (m_Settings.Theme) {
			case ThemeSetting::Auto:
				return ImGuiContextLayer::GetSystemTheme() == ImGuiContextLayer::Theme::Dark;
			case ThemeSetting::Custom: {
				const auto color = m_Settings.CustomColors.find("WindowBg");
				if (color == m_Settings.CustomColors.end()) return true;
				const auto& c = color->second;
				return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2] < 0.5f;
			}
			case ThemeSetting::Dark:
				return true;
			case ThemeSetting::Light:
				return false;
		}
		return true;
	}

	void LauncherLayer::ApplyLauncherThemeIfNeeded() {
		const bool dark = IsEffectiveThemeDark();
		if (m_LastAppliedDarkTheme.has_value() && *m_LastAppliedDarkTheme == dark) {
			return;
		}

		ImGuiContextLayer::ApplyFunkeTheme(dark
			? ImGuiContextLayer::Theme::Dark
			: ImGuiContextLayer::Theme::Light);
		if (m_Settings.Theme == ThemeSetting::Custom) {
			// Missing roles have deterministic defaults, including after ImGui adds colors.
			ImGuiContextLayer::ConfigureFunkeThemeColors(ImGui::GetStyle(), true);
			for (int i = 0; i < ImGuiCol_COUNT; ++i) {
				const auto it = m_Settings.CustomColors.find(ImGui::GetStyleColorName(i));
				if (it == m_Settings.CustomColors.end()) continue;
				const auto& c = it->second;
				ImGui::GetStyle().Colors[i] = ImVec4(c[0], c[1], c[2], c[3]);
			}
			ImGuiContextLayer::RefreshNativeTitlebar();
		}
		m_LastAppliedDarkTheme = dark;
	}

	void LauncherLayer::RenderLanguageRestartModal() {
		constexpr const char* k_RestartId = "###LauncherLanguageRestart";

		if (m_OpenLanguageRestartPopup) {
			ImGui::OpenPopup(k_RestartId);
			m_OpenLanguageRestartPopup = false;
		}

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		// English fallback throughout: this prompt only appears right after selecting a
		// CJK language whose font isn't loaded until restart, so localized text would be boxes.
		const std::string title = Localization::GetFallback("launcher.settings.language.restart_title") + std::string(k_RestartId);
		if (!ImGuiDialog::BeginModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
			return;
		}

		ImGuiDialog::Message("##RestartRequired", Localization::GetFallback("launcher.settings.language.restart_required").c_str());
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::Button(Localization::GetFallback("launcher.settings.language.restart_now").c_str(), ImVec2(160, 0))) {
			RestartLauncherProcess();
		}
		ImGui::SameLine();
		if (ImGui::Button(Localization::GetFallback("launcher.settings.language.restart_later").c_str(), ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	void LauncherLayer::SaveProjectDefaults() {
		if (!m_SettingsLocationDirty && !m_SettingsCompanyDirty) {
			return;
		}
		const std::string engineDefault = FunkeProject::GetDefaultProjectsDir();
		if (m_SettingsLocationDirty) {
			const std::string typed(m_SettingsLocationBuffer);
			m_Settings.DefaultProjectsLocation = typed == engineDefault ? std::string{} : typed;
		}
		if (m_SettingsCompanyDirty) {
			m_Settings.DefaultCompany = m_SettingsCompanyBuffer;
		}
		m_Settings.Save();
		m_SettingsLocationDirty = false;
		m_SettingsCompanyDirty = false;
		std::snprintf(m_SettingsLocationBuffer, sizeof(m_SettingsLocationBuffer), "%s",
			m_Settings.DefaultProjectsLocation.empty() ? engineDefault.c_str() : m_Settings.DefaultProjectsLocation.c_str());
	}

	void LauncherLayer::RenderSettingsPage() {
		PageHeader(FNK_TR("launcher.nav.settings"), FNK_TR("launcher.settings.subtitle"));
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.6f));
		ImGui::BeginChild("##SettingsScroll", ImVec2(0.0f, 0.0f));

		const std::string effectiveDefault = m_Settings.DefaultProjectsLocation.empty()
			? FunkeProject::GetDefaultProjectsDir()
			: m_Settings.DefaultProjectsLocation;

		BeginSettingsSection("##SettingsProjects", FNK_TR("launcher.settings.section.projects"));

		const std::string currentLine = Localization::Format("launcher.settings.current", effectiveDefault)
			+ (m_Settings.DefaultProjectsLocation.empty() ? FNK_TR("launcher.settings.engine_default_suffix") : std::string{});
		BeginFormRow(FNK_TR("launcher.settings.default_location"),
			FNK_TR("launcher.settings.default_location_subtitle"), true);
		{
			const float folderW = ImGui::GetFrameHeight();
			ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - folderW - ImGui::GetStyle().ItemInnerSpacing.x);
			const bool submitted = ImGui::InputText("##DefaultLocation", m_SettingsLocationBuffer,
				sizeof(m_SettingsLocationBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
			if (ImGui::IsItemEdited()) m_SettingsLocationDirty = true;
			if (submitted || ImGui::IsItemDeactivatedAfterEdit()) SaveProjectDefaults();
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", currentLine.c_str());
			ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
			if (Icons::IconButton("##BrowseDefaultLocation", Icons::Type::OpenFolder, ImVec2(folderW, folderW))) {
				BrowseForDefaultProjectsLocation();
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", FNK_TR("launcher.settings.browse").c_str());
		}
		EndFormRow();

		BeginFormRow(FNK_TR("launcher.settings.default_company"),
			FNK_TR("launcher.settings.default_company_subtitle"), true);
		{
			const bool submitted = ImGui::InputText("##DefaultCompany", m_SettingsCompanyBuffer,
				sizeof(m_SettingsCompanyBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
			if (ImGui::IsItemEdited()) m_SettingsCompanyDirty = true;
			if (submitted || ImGui::IsItemDeactivatedAfterEdit()) SaveProjectDefaults();
		}
		EndFormRow();

		BeginFormRow(FNK_TR("launcher.settings.directory_name_convention"), {}, true);
		{
			static constexpr DirectoryNameConvention k_Entries[] = {
				DirectoryNameConvention::None,
				DirectoryNameConvention::TitleCase,
				DirectoryNameConvention::TitleDashCase,
				DirectoryNameConvention::TitleCamelCase,
				DirectoryNameConvention::TitleSnakeCase,
				DirectoryNameConvention::TitleKebabCase,
			};

			const char* preview = LauncherSettings::DirectoryNameConventionLabel(m_Settings.DirectoryConvention);
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##LauncherDirectoryNameConventionCombo", preview)) {
				for (DirectoryNameConvention convention : k_Entries) {
					const bool selected = convention == m_Settings.DirectoryConvention;
					const char* label = LauncherSettings::DirectoryNameConventionLabel(convention);
					if (ImGui::Selectable(label, selected)) {
						m_Settings.DirectoryConvention = convention;
						m_Settings.Save();
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		EndFormRow();

		ImGui::EndChild();
		ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.6f));
		BeginSettingsSection("##SettingsAppearance", FNK_TR("launcher.settings.section.appearance"));

		ImGui::TextUnformatted(FNK_TR("launcher.settings.theme").c_str());
		static const ImGuiStyle light = [] { ImGuiStyle style; ImGuiContextLayer::ConfigureFunkeThemeColors(style, false); return style; }();
		static const ImGuiStyle dark = [] { ImGuiStyle style; ImGuiContextLayer::ConfigureFunkeThemeColors(style, true); return style; }();
		ImGuiStyle custom = dark;
		for (int i = 0; i < ImGuiCol_COUNT; ++i) {
			const auto it = m_Settings.CustomColors.find(ImGui::GetStyleColorName(i));
			if (it == m_Settings.CustomColors.end()) continue;
			const auto& c = it->second;
			custom.Colors[i] = ImVec4(c[0], c[1], c[2], c[3]);
		}
		constexpr ThemeSetting modes[] = { ThemeSetting::Light, ThemeSetting::Dark, ThemeSetting::Auto, ThemeSetting::Custom };
		const std::string labelStrings[] = { FNK_TR("launcher.settings.theme.light"), FNK_TR("launcher.settings.theme.dark"),
			FNK_TR("launcher.settings.theme.system"), FNK_TR("launcher.settings.theme.custom") };
		const std::string systemHelp = FNK_TR("launcher.settings.theme.system_help");
		const std::string customHelp = FNK_TR("launcher.settings.theme.custom_help");
		const char* labels[] = { labelStrings[0].c_str(), labelStrings[1].c_str(), labelStrings[2].c_str(), labelStrings[3].c_str() };
		const char* tooltips[] = { labels[0], labels[1], systemHelp.c_str(), customHelp.c_str() };
		int current = 0;
		for (int i = 0; i < 4; ++i) if (modes[i] == m_Settings.Theme) current = i;
		const auto chosen = ThemePicker::Draw(static_cast<ThemePicker::Mode>(current), light, dark, custom,
			ImGui::ColorConvertU32ToFloat4(GetLauncherPalette().Accent), [](const char* name, int size) {
				const Icons::Type icon = std::strcmp(name, "theme_light") == 0 ? Icons::Type::ThemeLight
					: std::strcmp(name, "theme_dark") == 0 ? Icons::Type::ThemeDark
					: std::strcmp(name, "monitor") == 0 ? Icons::Type::Monitor : Icons::Type::Pen;
				return Icons::Get(icon, size);
			}, labels, tooltips);
		bool openCustomColors = false;
		if (modes[static_cast<int>(chosen)] != m_Settings.Theme) {
			m_Settings.Theme = modes[static_cast<int>(chosen)];
			m_LastAppliedDarkTheme.reset();
			m_Settings.Save();
			openCustomColors = m_Settings.Theme == ThemeSetting::Custom;
		}
		ImGui::TextWrapped("%s", m_Settings.Theme == ThemeSetting::Auto ? systemHelp.c_str()
			: m_Settings.Theme == ThemeSetting::Custom ? customHelp.c_str() : FNK_TR("launcher.settings.theme.immediate").c_str());
		ImGui::Spacing();
		const bool customActive = m_Settings.Theme == ThemeSetting::Custom;
		if (openCustomColors) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		if (ImGui::CollapsingHeader(FNK_TR("launcher.settings.theme.custom_colors").c_str())) {
			ImGui::BeginDisabled(!customActive);
			ImGui::BeginChild("##CustomColorsScroll", ImVec2(0, ImGui::GetFontSize() * 18.0f), ImGuiChildFlags_Borders);
			for (int i = 0; i < ImGuiCol_COUNT; ++i) {
				const char* name = ImGui::GetStyleColorName(i);
				ImVec4 color = custom.Colors[i];
				ImGui::PushID(i);
				BeginFormRow(name, {}, true);
				if (ImGui::ColorEdit4("##Color", &color.x,
					ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
					m_Settings.CustomColors[name] = { color.x, color.y, color.z, color.w };
					m_LastAppliedDarkTheme.reset();
					m_Settings.Save();
				}
				EndFormRow();
				ImGui::PopID();
			}
			ImGui::EndChild();
			ImGui::EndDisabled();
		}
		ImGui::Spacing();

		BeginFormRow(FNK_TR("launcher.settings.font_scale"), {}, true);
		{
			struct FontScaleEntry {
				FontScale Value;
				const char* Label;  // nullptr → use translated "Auto (xxx%)"
			};
			static constexpr FontScaleEntry k_Entries[] = {
				{ FontScale::Auto, nullptr },
				{ FontScale::P75,  "75%"   },
				{ FontScale::P100, "100%"  },
				{ FontScale::P125, "125%"  },
				{ FontScale::P150, "150%"  },
				{ FontScale::P175, "175%"  },
				{ FontScale::P200, "200%"  },
			};

			// Auto always resolves to 100% today; format the {0} so future
			// DPI-aware behaviour can change the displayed percentage without
			// touching the translation files.
			auto labelFor = [](const FontScaleEntry& e) -> std::string {
				if (e.Value == FontScale::Auto) {
					return Localization::Format("launcher.settings.font_scale.auto_format", 100);
				}
				return std::string(e.Label);
			};

			std::string preview;
			for (const auto& e : k_Entries) {
				if (e.Value == m_Settings.Scale) {
					preview = labelFor(e);
					break;
				}
			}

			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::BeginCombo("##LauncherFontScaleCombo", preview.c_str())) {
				for (const auto& e : k_Entries) {
					const bool selected = (e.Value == m_Settings.Scale);
					const std::string label = labelFor(e);
					if (ImGui::Selectable(label.c_str(), selected)) {
						m_Settings.Scale = e.Value;
						m_Settings.Save();
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		EndFormRow();

		BeginFormRow(FNK_TR("launcher.settings.language"), {}, true);
		const auto& languages = Localization::GetAvailableLanguages();
		const std::string& currentCode = Localization::GetCurrentLanguage();
		const std::string notInstalledSuffix = FNK_TR("launcher.settings.language.not_installed_suffix");

		const std::string sysCode = Localization::GetSystemLanguage();
		std::string sysDisplay = sysCode;
		for (const auto& lang : languages) {
			if (lang.Code == sysCode) { sysDisplay = lang.DisplayName; break; }
		}
		const std::string autoLabel = Localization::Format("launcher.settings.language.auto_format", sysDisplay);

		std::string previewName;
		if (m_Settings.LanguageAuto) {
			previewName = autoLabel;
		}
		else {
			for (const auto& lang : languages) {
				if (lang.Code == currentCode) { previewName = lang.DisplayName; break; }
			}
		}

		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::BeginCombo("##LauncherLanguageCombo", previewName.c_str())) {
			// Auto entry always at the top — matches the screenshot the user
			// referenced and lets a returning user re-engage system-language
			// tracking without hunting for a checkbox.
			if (ImGui::Selectable(autoLabel.c_str(), m_Settings.LanguageAuto)) {
				m_Settings.LanguageAuto = true;
				Localization::SetLanguage(sysCode);
				m_Settings.Save();
			}
			if (m_Settings.LanguageAuto) ImGui::SetItemDefaultFocus();

			ImGui::Separator();

			for (size_t i = 0; i < languages.size(); ++i) {
				const bool selected = !m_Settings.LanguageAuto && (languages[i].Code == currentCode);
				std::string label = languages[i].DisplayName;
				if (languages[i].Status != Localization::LanguageStatus::Installed) {
					label += notInstalledSuffix;
				}
				if (ImGui::Selectable(label.c_str(), selected)) {
					m_Settings.LanguageAuto = false;
					Localization::SetLanguage(languages[i].Code);
					m_Settings.Save();
				}
				if (selected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		EndFormRow();

		ImGui::EndChild();
		ImGui::EndChild();
	}

	void LauncherLayer::BrowseForDefaultProjectsLocation() {
		if (std::string path = PickFolder("Default projects location"); !path.empty()) {
			std::snprintf(m_SettingsLocationBuffer, sizeof(m_SettingsLocationBuffer), "%s", path.c_str());
			m_SettingsLocationDirty = true;
			SaveProjectDefaults();
		}
	}
}
