#include "Systems/ImGuiEditorLayer.hpp"
#include "Gui/EditorUI.hpp"
#include "Gui/ImGuiImplWebGPU.hpp"
#include "Packages/GitHubSource.hpp"
#include "Packages/NuGetSource.hpp"
#include "Project/ProjectManager.hpp"
#include "Serialization/Directory.hpp"
#include "Serialization/Path.hpp"
#include <cfloat>

namespace Funke {
	void ImGuiEditorLayer::RenderProjectPanel() {
		if (!m_AssetBrowserInitialized) {
			std::string assetsRoot;
			FunkeProject* project = ProjectManager::GetCurrentProject();
			if (project) assetsRoot = project->AssetsDirectory;
			else assetsRoot = Path::Combine(Path::ExecutableDir(), "Assets");

			if (!Directory::Exists(assetsRoot)) {
				Directory::Create(assetsRoot);
			}

			m_AssetBrowser.Initialize(assetsRoot);
			m_AssetBrowserInitialized = true;
		}

		m_AssetBrowser.Render();
		if (m_AssetBrowser.TakeSelectionActivated() && !m_AssetBrowser.GetSelectedPath().empty()) {
			ClearEntitySelection();
		}
	}

	void ImGuiEditorLayer::RenderPackageManagerPanel() {
		if (!m_ShowPackageManager) return;
#if defined(FUNKE_WEB_EDITOR)
		EditorUI::NextWindowDefaultSize();
		ImGui::Begin("Package Manager", &m_ShowPackageManager);
		EditorUI::EmptyState("Package installation requires the desktop editor.", "C# scripts compile in the browser. Native extensions need a matching Web runtime build.");
		ImGui::End();
		return;
#endif

		if (!m_PackageManagerInitialized) {
			m_PackageManager.Initialize();

			if (m_PackageManager.IsReady()) {
				m_PackageManager.AddSource(std::make_unique<NuGetSource>(m_PackageManager.GetToolPath()));
				m_PackageManager.AddSource(std::make_unique<GitHubSource>(
					m_PackageManager.GetToolPath(),
					"https://raw.githubusercontent.com/Ben-Scr/index-packages/main/index.json",
					"Engine Packages"));
			}

			m_PackageManagerPanel.Initialize(&m_PackageManager);
			m_PackageManagerInitialized = true;
		}

		ImGui::SetNextWindowSizeConstraints(ImVec2(420, 320), ImVec2(FLT_MAX, FLT_MAX));
		ImGuiImplWebGPU::SetNextWindowAsNativeDialog();
		EditorUI::NextWindowDefaultSize();
		ImGui::Begin("Package Manager", &m_ShowPackageManager);
		m_PackageManagerPanel.Render();
		ImGui::End();
	}
}
