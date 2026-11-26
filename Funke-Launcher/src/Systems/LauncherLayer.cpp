#include "Systems/LauncherLayer.hpp"
#include "Gui/LauncherUI.hpp"
#include "Gui/ImGuiDialog.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Localization/Localization.hpp"
#include "Core/Log.hpp"
#include "Serialization/Path.hpp"
#include "Core/Application.hpp"
#include "Core/Platform.hpp"
#include "Core/Version.hpp"
#include "Gui/BuildProgressOverlay.hpp"
#include "Gui/CustomTitlebar.hpp"
#include <algorithm>
#include <cmath>

namespace Funke {
	using namespace LauncherUI;

	static void OpenUrlInBrowser(const char* url) {
		Platform::OpenUrl(url ? url : "");
	}

	void LauncherLayer::OnAttach(Application& app) {
		(void)app;
		Application::SetIsPlaying(false);
		if (const char* project = std::getenv("FUNKE_LAUNCHER_OPEN_PROJECT")) m_AutomationOpenProject = project;
		if (!m_AutomationOpenProject.empty()) {
			if (const char* report = std::getenv("FUNKE_LAUNCHER_OPEN_REPORT")) m_AutomationOpenReport = report;
		}

		// Settings drive the splash theme + UI defaults, so load them now; the
		// heavier registry scan is deferred to OnUpdate (runs behind the splash).
		m_Settings.Load();
		ApplyLauncherThemeIfNeeded();

		if (m_Settings.LanguageAuto) {
			Localization::SetLanguage(Localization::GetSystemLanguage());
		}

		const std::string defaultLocation = m_Settings.DefaultProjectsLocation.empty()
			? FunkeProject::GetDefaultProjectsDir()
			: m_Settings.DefaultProjectsLocation;
		std::snprintf(m_NewProjectLocation, sizeof(m_NewProjectLocation), "%s",
			defaultLocation.c_str());
		std::snprintf(m_SettingsLocationBuffer, sizeof(m_SettingsLocationBuffer), "%s",
			defaultLocation.c_str());
		std::snprintf(m_SettingsCompanyBuffer, sizeof(m_SettingsCompanyBuffer), "%s",
			m_Settings.DefaultCompany.c_str());

		if (const char* page = std::getenv("FUNKE_LAUNCHER_PAGE"); page && std::string_view(page) == "templates") {
			m_ActivePage = LauncherPage::Templates;
			RefreshProjectTemplates();
		}
		else if (page && std::string_view(page) == "assets") {
			m_ActivePage = LauncherPage::AssetLibrary;
		}
		else if (page && std::string_view(page) == "settings") {
			m_ActivePage = LauncherPage::Settings;
		}

		m_Splash.Begin();
		m_PrerequisiteProbe = std::async(std::launch::async, &ProbePrerequisites);
	}

	void LauncherLayer::RenderPrerequisiteBanner() {
		if (!m_Prerequisites.Checked && m_PrerequisiteProbe.valid()
			&& m_PrerequisiteProbe.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			m_Prerequisites = m_PrerequisiteProbe.get();
		}
		if (!m_Prerequisites.Checked || (m_Prerequisites.DotNetSdk
			&& (!m_Prerequisites.NativeDevelopment || m_Prerequisites.MsvcBuildTools))) return;

		const ImVec4 warning(0.96f, 0.68f, 0.26f, 1.0f);
		auto row = [&](const char* id, const std::string& text, const char* url) {
			ImGui::PushID(id);
			ImGui::PushStyleColor(ImGuiCol_Text, warning);
			ImGui::TextUnformatted(text.c_str());
			ImGui::PopStyleColor();
			ImGui::SameLine();
			if (ImGui::SmallButton(FNK_TR("launcher.prereq.download").c_str())) {
				OpenUrlInBrowser(url);
			}
			ImGui::PopID();
		};
		if (!m_Prerequisites.DotNetSdk) {
			row("dotnet", FNK_TR("launcher.prereq.dotnet_missing"), k_DotNetSdkDownloadUrl);
		}
		if (m_Prerequisites.NativeDevelopment && !m_Prerequisites.MsvcBuildTools) {
			row("msvc", FNK_TR("launcher.prereq.msvc_missing"), k_MsvcBuildToolsDownloadUrl);
		}
		ImGui::Spacing();
	}

	void LauncherLayer::OnUpdate(Application& app, float dt) {
		(void)app;
		(void)dt;
		// Heavy startup runs once, behind the visible splash. OnUpdate is NOT
		// called during Initialize's pre-loop refresh passes, so by the time it
		// fires the splash has already been presented. ValidateAll stats every
		// project directory on disk — the bulk of launcher startup cost.
		if (m_StartupWorkDone) {
			return;
		}
		m_Registry.Load();
		m_Registry.ValidateAll();
		m_Registry.Save();
		FNK_INFO_TAG("Launcher", "Funke Launcher opened ({} project(s))", m_Registry.GetProjects().size());
		m_StartupWorkDone = true;
	}

	void LauncherLayer::OnPreRender(Application& app) {
		m_LibraryThumbnails.Pump();
		ApplyLauncherThemeIfNeeded();
		// MUST precede Begin/Text calls: NewFrame already ran, so scale takes effect immediately for this frame's metrics.
		ImGui::GetIO().FontGlobalScale = GetEffectiveFontScale();

		// Splash holds (static) until the deferred startup work is done, then
		// fades. While opaque, skip building the launcher UI (registry not loaded
		// yet); the fade draws on top via the foreground draw list.
		if (m_Splash.IsActive()) {
			m_Splash.Render(app, m_StartupWorkDone);
			if (m_Splash.IsCovering()) {
				return;
			}
		}

		Localization::Poll();
		if (!m_AutomationOpenProject.empty() && !m_AutomationOpenStarted) {
			m_AutomationOpenStarted = true;
			OpenProject({ Path::ToUtf8(Path::FromUtf8(m_AutomationOpenProject).filename()), m_AutomationOpenProject });
		}

		// Language/font download results surface in secondary windows, never
		// inline in Settings: progress goes to the native progress window
		// (UpdateProgressPopup); the outcome lands here exactly once.
		if (const auto dl = Localization::GetActiveDownload()) {
			if (dl->Running) {
				m_LanguageDownloadWasRunning = true;
			}
			else if (m_LanguageDownloadWasRunning) {
				m_LanguageDownloadWasRunning = false;
				if (dl->Failed) {
					// English: the just-selected language's font isn't loaded yet, so a localized message would be unreadable boxes.
					ShowError(Localization::FormatFallback("launcher.settings.language.download_failed", dl->Error));
				}
				else if (dl->RestartRequired) {
					m_OpenLanguageRestartPopup = true;
				}
			}
		}

		PollCreateProjectTask();
		PollDuplicateProjectTask();
		PollSaveTemplateTask();
		PollRefreshTemplateTask();
		const std::string titlebarText = FNK_TR("launcher.title") + std::string(" ") + std::string(FNK_VERSION);
		EditorRuntime::RenderTitlebar(EditorRuntime::TitlebarConfig{ titlebarText, /*Centered=*/false });
		RenderLauncherPanel();
		UpdateProgressPopup();
	}

	void LauncherLayer::OnDetach(Application& app) {
		m_LibraryThumbnails.Shutdown();
		(void)app;
		SaveProjectDefaults();
		ResetCreateProjectTask();
		ResetDuplicateProjectTask();
		ResetTemplateTask(m_SaveTemplateTask);
		ResetTemplateTask(m_RefreshTemplateTask);
		if (m_OpenTask.Worker.joinable()) {
			m_OpenTask.Worker.join();
		}
		// Join before teardown: destructor would call terminate() if the thread is still joinable.
		if (m_AssetLibraryTask.Worker.joinable()) {
			m_AssetLibraryTask.Worker.join();
		}
		// Stopping all workers first lets their shutdown overlap.
		for (auto& [_, task] : m_ProjectSizeTasks) {
			if (task) {
				task->Worker.RequestStop();
			}
		}
		m_ProjectSizeTasks.clear();

		// MUST precede renderer teardown: Texture2D destructors access the WebGPU device.
		m_Splash.Shutdown();
		m_LogoTexture.Destroy();
		m_ProjectThumbnails.clear();
		Icons::Shutdown();
	}

	void LauncherLayer::RenderLauncherPanel() {
		// Pull async open-task results before we draw anything.
		PollOpenProjectTask();

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		// Offset the panel below the custom titlebar (0 when disabled).
		const float titlebarH = EditorRuntime::GetTitlebarRowHeight();
		ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + titlebarH));
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - titlebarH));

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking
			| ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar
			| ImGuiWindowFlags_NoScrollWithMouse;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::Begin("##Launcher", nullptr, flags);
		ImGui::PopStyleVar(3);

		// m_IsOpening holds for 1.5s after the worker reports success so LastOpened is committed after the editor window appears.
		bool taskRunning = false;
		{
			std::scoped_lock lock(m_OpenTask.Mutex);
			taskRunning = m_OpenTask.Running;
		}
		if (m_IsOpening && !taskRunning) {
			float elapsed = std::chrono::duration<float>(
				std::chrono::steady_clock::now() - m_OpenStartTime).count();
			if (elapsed >= 1.5f) {
				m_IsOpening = false;
				if (!m_DeferredUpdatePath.empty()) {
					m_Registry.UpdateLastOpened(m_DeferredUpdatePath);
					m_Registry.Save();
					m_DeferredUpdatePath.clear();
				}
			}
		}

		PollAssetLibraryTask();

		const LauncherPage previousPage = m_ActivePage;
		RenderSidebar();
		if (previousPage == LauncherPage::Settings && m_ActivePage != previousPage) {
			SaveProjectDefaults();
		}
		ImGui::SameLine(0.0f, 0.0f);

		const float em = ImGui::GetFontSize();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(em * 1.6f), std::round(em * 1.3f)));
		ImGui::BeginChild("##LauncherContent", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar();

		switch (m_ActivePage) {
			case LauncherPage::Projects:
				RenderMyProjectsTab();
				break;
			case LauncherPage::AssetLibrary:
				// First visit fetches; later visits hit the cache (Refresh re-fetches explicitly).
				if (!m_AssetLibraryAutoFetchAttempted && !m_AssetLibrary.Loaded && !m_AssetLibrary.FetchInFlight) {
					m_AssetLibraryAutoFetchAttempted = true;
					StartFetchAssetLibraryIndex();
				}
				RenderAssetLibraryTab();
				break;
			case LauncherPage::Templates:
				RenderTemplatesTab();
				break;
			case LauncherPage::Settings:
				RenderSettingsPage();
				break;
		}

		ImGui::EndChild();

		// Modals live at panel scope — independent of the active page.
		RenderCreateProjectPopup();
		RenderDeleteProjectPopups();
		RenderProjectInfoPopup();
		RenderErrorPopup();
		RenderRenameProjectPopup();
		RenderDuplicateProjectPopup();
		RenderSaveTemplatePopup();
		RenderEditTemplatePopup();
		RenderRefreshTemplatePopup();
		RenderDeleteTemplatePopup();
		RenderAssetLibraryDetailModal();
		RenderAssetLibraryTrustModal();
		RenderAssetLibraryDoneModal();
		RenderLanguageRestartModal();

		ImGui::End();
	}

	void LauncherLayer::RenderSidebar() {
		const LauncherPalette pal = GetLauncherPalette();
		const float em = ImGui::GetFontSize();
		const float width = std::round(em * 12.5f);

		ImGui::PushStyleColor(ImGuiCol_ChildBg, pal.Sidebar);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(std::round(em * 0.75f), std::round(em * 1.1f)));
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
		ImGui::BeginChild("##LauncherSidebar", ImVec2(width, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();

		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 winPos = ImGui::GetWindowPos();
		const ImVec2 winSize = ImGui::GetWindowSize();
		dl->AddLine(ImVec2(winPos.x + winSize.x - 1.0f, winPos.y),
			ImVec2(winPos.x + winSize.x - 1.0f, winPos.y + winSize.y), pal.SidebarBorder);

		// Brand block: logo + name + version.
		if (!m_LogoLoadAttempted) {
			m_LogoLoadAttempted = true;
			const std::string logoPath = Path::Combine(Path::ResolveFunkeAssets("Textures"), "icon.png");
			if (!logoPath.empty() && std::filesystem::exists(logoPath)) {
				m_LogoTexture = Texture2D(logoPath.c_str(), Filter::Bilinear, Wrap::Clamp, Wrap::Clamp);
			}
		}

		const float logoSize = std::round(em * 2.2f);
		const ImVec2 brandPos = ImGui::GetCursorScreenPos();
		const ImVec2 logoPos(brandPos.x + em * 0.4f, brandPos.y);
		if (m_LogoTexture.GetHandle() != 0) {
			dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_LogoTexture.GetHandle())),
				logoPos, ImVec2(logoPos.x + logoSize, logoPos.y + logoSize),
				ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), pal.LogoTint);
		}
		else {
			const ImVec2 c(logoPos.x + logoSize * 0.5f, logoPos.y + logoSize * 0.5f);
			const float r = logoSize * 0.5f;
			dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), pal.Accent);
		}

		const float textX = logoPos.x + logoSize + em * 0.65f;
		ImGui::PushFont(nullptr, BaseFontSize() * 1.35f);
		const float nameH = ImGui::GetTextLineHeight();
		ImGui::PopFont();
		const float versionH = ImGui::GetTextLineHeight();
		const float textTop = brandPos.y + (logoSize - nameH - versionH) * 0.5f;

		ImGui::SetCursorScreenPos(ImVec2(textX, textTop));
		ImGui::PushFont(nullptr, BaseFontSize() * 1.35f);
		ImGui::TextUnformatted("Funke");
		ImGui::PopFont();
		ImGui::SetCursorScreenPos(ImVec2(textX, textTop + nameH));
		ImGui::TextDisabled("%s", FNK_VERSION);

		ImGui::SetCursorScreenPos(ImVec2(brandPos.x, brandPos.y + logoSize + em * 1.4f));

		if (SidebarNavItem("##NavProjects", Icons::Type::FileManager, FNK_TR("launcher.nav.projects"),
			m_ActivePage == LauncherPage::Projects, pal)) {
			m_ActivePage = LauncherPage::Projects;
		}
		if (SidebarNavItem("##NavTemplates", Icons::Type::Templates, FNK_TR("launcher.nav.templates"),
			m_ActivePage == LauncherPage::Templates, pal)) {
			if (m_ActivePage != LauncherPage::Templates) {
				RefreshProjectTemplates();
			}
			m_ActivePage = LauncherPage::Templates;
		}
		if (SidebarNavItem("##NavAssetLibrary", Icons::Type::Download, FNK_TR("launcher.nav.asset_library"),
			m_ActivePage == LauncherPage::AssetLibrary, pal)) {
			m_ActivePage = LauncherPage::AssetLibrary;
		}

		// Settings pinned to the bottom of the sidebar.
		const float navH = std::round(em * 2.3f);
		const float bottomY = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - navH;
		if (ImGui::GetCursorPosY() < bottomY) {
			ImGui::SetCursorPosY(bottomY);
		}
		if (SidebarNavItem("##NavSettings", Icons::Type::Settings, FNK_TR("launcher.nav.settings"),
			m_ActivePage == LauncherPage::Settings, pal)) {
			m_ActivePage = LauncherPage::Settings;
		}

		ImGui::EndChild();
	}

	void LauncherLayer::ShowError(std::string message) {
		m_ErrorMessage = std::move(message);
		m_OpenErrorPopup = true;
	}

	void LauncherLayer::RenderErrorPopup() {
		constexpr const char* k_ErrorId = "###LauncherError";

		if (m_OpenErrorPopup) {
			ImGui::OpenPopup(k_ErrorId);
			m_OpenErrorPopup = false;
		}

		const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGuiDialog::NextMessageWindow();

		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		const std::string title = FNK_TR("launcher.error.title") + k_ErrorId;
		if (!ImGuiDialog::BeginModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		ImGuiDialog::Message("##ErrorMessage", m_ErrorMessage.c_str());
		const int action = DialogFooter({ FNK_TR("common.copy_details"), FNK_TR("launcher.error.ok") }, 1);
		if (action == 0) ImGui::SetClipboardText(m_ErrorMessage.c_str());
		if (action == 1 || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			m_ErrorMessage.clear();
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}
