#include <pch.hpp>
#include "Editor/SceneRecovery.hpp"

#include "Core/Application.hpp"
#include "Project/FunkeProject.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"

#include <system_error>
#include <unordered_set>

namespace Funke::SceneRecovery {
	namespace {
		constexpr const char* k_Extension = ".scene";

		std::filesystem::path RecoveryDirectory() {
			FunkeProject* project = ProjectManager::GetCurrentProject();
			if (!project || project->RootDirectory.empty()) return {};
			return Path::FromUtf8(project->RootDirectory) / ".funke" / "Recovery";
		}
	}

	void SnapshotDirtyScenes() {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		const std::filesystem::path directory = RecoveryDirectory();
		// Play mode mutates scenes at runtime; the pre-play state is already on disk.
		if (!project || directory.empty() || Application::GetIsPlaying()) return;

		std::error_code ec;
		std::unordered_set<std::string> keep;
		for (const std::weak_ptr<Scene>& weak : SceneManager::GetLoadedScenes()) {
			const std::shared_ptr<Scene> scene = weak.lock();
			if (!scene || !scene->IsLoaded() || scene->IsLifecycleSuspended() || !scene->IsDirty()) continue;

			const std::string fileName = scene->GetName() + k_Extension;
			keep.insert(fileName);
			std::filesystem::create_directories(directory, ec);
			if (SceneSerializer::SaveToFile(*scene, Path::ToUtf8(directory / Path::FromUtf8(fileName)), false)) {
				scene->MarkDirty();
			}
		}

		if (!std::filesystem::is_directory(directory, ec)) return;
		for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
			if (!keep.contains(Path::ToUtf8(entry.path().filename()))) {
				std::filesystem::remove(entry.path(), ec);
			}
		}
	}

	std::vector<PendingScene> FindPending() {
		std::vector<PendingScene> pending;
		FunkeProject* project = ProjectManager::GetCurrentProject();
		const std::filesystem::path directory = RecoveryDirectory();
		std::error_code ec;
		if (!project || directory.empty() || !std::filesystem::is_directory(directory, ec)) return pending;

		for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
			if (!entry.is_regular_file(ec) || entry.path().extension() != k_Extension) continue;
			PendingScene scene;
			scene.SceneName = Path::ToUtf8(entry.path().stem());
			scene.RecoveryFile = entry.path();
			scene.ScenePath = project->GetSceneFilePath(scene.SceneName);
			if (!scene.ScenePath.empty()) pending.push_back(std::move(scene));
		}
		return pending;
	}

	bool RestoreAll(const std::vector<PendingScene>& pending, std::string* outError) {
		std::error_code ec;
		for (const PendingScene& scene : pending) {
			const std::filesystem::path target = Path::FromUtf8(scene.ScenePath);
			if (std::filesystem::exists(target, ec)) {
				std::filesystem::path backup = target;
				backup += ".bak";
				std::filesystem::copy_file(target, backup, std::filesystem::copy_options::overwrite_existing, ec);
				if (ec) {
					if (outError) *outError = "Could not back up '" + scene.ScenePath + "': " + ec.message();
					return false;
				}
			}
			std::filesystem::create_directories(target.parent_path(), ec);
			std::filesystem::copy_file(scene.RecoveryFile, target, std::filesystem::copy_options::overwrite_existing, ec);
			if (ec) {
				if (outError) *outError = "Could not restore '" + scene.ScenePath + "': " + ec.message();
				return false;
			}
		}
		DiscardAll();
		return true;
	}

	void DiscardAll() {
		const std::filesystem::path directory = RecoveryDirectory();
		std::error_code ec;
		if (!directory.empty()) std::filesystem::remove_all(directory, ec);
	}
}
