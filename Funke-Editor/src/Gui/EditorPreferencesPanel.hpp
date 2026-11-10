#pragma once
#include "Core/Export.hpp"
#include "Gui/EditorDockLayout.hpp"

#include <string>
#include <vector>

namespace Funke {

	class EditorPreferencesPanel {
	public:
		void Initialize();
		void Shutdown();
		void OpenLayouts() { m_OpenLayoutsRequest = true; m_SearchBuffer[0] = '\0'; }
		// `pOpen` mirrors the BeginPopupModal / Begin pattern used by the
		// other panels. The close-X button on the window's title bar
		// flips it false; the caller suppresses the call next frame.
		void Render(bool* pOpen);

	private:
		void RenderAppearanceTab();
		void RenderScriptingTab();
		void RenderLayoutsTab();
		void RefreshLayoutPresets();
		void RenderBehaviorTab();
		void RenderCameraTab();

		void RenderLayoutModals();
		bool IsPreferenceSearchActive() const;
		bool PreferenceSectionVisible(const char* section, const char* keywords) const;

		// Layout modal state lives on the panel (not TU-static) so OpenPopup and BeginPopupModal share the same id-stack scope.
		char m_SaveLayoutBuffer[65]{};
		std::string m_LayoutError;
		bool m_OpenSaveLayoutRequest = false;
		std::string m_PendingDeleteLayoutName;
		bool m_OpenDeleteLayoutRequest = false;
		struct LayoutPreset {
			std::string Name;
			EditorDockLayout::Preview Preview;
		};
		std::vector<LayoutPreset> m_LayoutPresets;
		bool m_LayoutPresetsDirty = true;
		int m_LastLayoutsFrame = -1;
		bool m_ShowLayoutStorage = false;
		bool m_OpenLayoutsRequest = false;

		char m_SearchBuffer[256]{};

		// True the frame the panel becomes visible. Used to refresh
		// detection of external script editors so plugging in a new IDE
		// without restarting the editor still surfaces it.
		bool m_WasOpenLastFrame = false;
	};

}
