#pragma once
#include "Editor/PlatformBuilder.hpp"
#include "Project/IndexBuildProfile.hpp"

#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Index {

	class BuildProfilesPanel {
	public:
		void Initialize();
		void Shutdown();
		// `pOpen` flips false when the user hits the title bar X.
		void Render(bool* pOpen);
		// Re-reads every .build file on the next Render, e.g. after one was edited outside the editor.
		void ReloadFromDisk() { m_Dirty = true; }

	private:
		void RefreshProfilesIfDirty();
		void RescanFromDisk();
		void RenderProfileList();
		void RenderProfileRow(int index);
		void RenderProfilePopups();
		void RenderInspector();
		void RenderPlatformTiles(IndexBuildProfile& profile);
		void RenderPlatformPackage(IndexBuildProfile& profile);
		void RenderTesting(IndexBuildProfile& profile);

		bool HasSelection() const;
		bool IsActiveProfile(const std::string& name) const;
		void SetSelectedActive();
		void RequestRenameSelected();
		void RequestDeleteSelected();

		bool CreateProfile(const std::string& name);
		bool DuplicateSelected();
		bool DeletePendingProfile();
		bool RenameSelected(const std::string& newName);
		void SaveSelected();

		std::string BuildFilePath(const std::string& profileName) const;
		bool IsValidProfileName(const std::string& name) const;

		std::vector<IndexBuildProfile> m_Profiles;
		int  m_SelectedIndex = -1;
		bool m_Dirty = true;

		// Name-entry buffer for the "+" new-profile popup.
		char m_NewProfileNameBuffer[128]{};
		bool m_FocusNewProfileInput = false;
		bool m_NewProfileFailed = false;
		// Selected after the next rescan (new/duplicated profiles only exist on disk until then).
		std::string m_PendingSelectName;
		// Rename buffer — populated from the selected profile each frame
		// the user opens the rename popup.
		char m_RenameBuffer[128]{};
		// Deferred opens: requests come from rows/context menus in other ID scopes than the modals.
		bool m_OpenRenamePopup = false;
		bool m_OpenDeletePopup = false;
		std::string m_PendingDeleteName;
		std::string m_PendingDeleteProjectFilePath;

		// Builder `describe` / `check` answers per package platform; both run a Python process.
		struct PackageState {
			std::string PackageName;
			std::future<PlatformBuilder::Description> DescribeTask;
			std::future<PlatformBuilder::CheckResult> CheckTask;
			std::optional<PlatformBuilder::Description> Described;
			std::optional<PlatformBuilder::CheckResult> Checked;
		};
		std::map<BuildPlatform, PackageState> m_PackageStates;

		// Status strip at the bottom of the panel.
		std::string m_StatusMessage;
		bool m_StatusIsError = false;
	};

} // namespace Index
