#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Index {
	// Crash recovery for unsaved scene edits. Copies of dirty scenes are kept in
	// <project>/.index/Recovery and never touch the user's scene files until the
	// user explicitly restores them on the next start.
	namespace SceneRecovery {
		struct PendingScene {
			std::string SceneName;
			std::filesystem::path RecoveryFile;
			std::string ScenePath; // where the scene lives in the project (may not exist yet)
		};

		// Mirrors the recovery folder to the currently loaded, dirty, edit-mode scenes.
		void SnapshotDirtyScenes();
		std::vector<PendingScene> FindPending();
		// Copies each recovery file over its scene (keeping a .bak of the previous file) and clears the folder.
		bool RestoreAll(const std::vector<PendingScene>& pending, std::string* outError = nullptr);
		void DiscardAll();
	}
}
