#include "Systems/ImGuiEditorLayer.hpp"
#include "Audio/AudioStreams.hpp"
#include "Components/Audio/AudioSourceComponent.hpp"
#include "Components/General/UUIDComponent.hpp"
#include "Components/Graphics/ParticleSystem2DComponent.hpp"
#include "Components/Graphics/ParticleSystem3DComponent.hpp"
#include "Components/Tags.hpp"
#include "Core/Application.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#include "Editor/ApplicationEditorAccess.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Graphics/RenderSettings.hpp"
#include "Localization/GameLocalization.hpp"
#include "Physics/PhysicsSystem2D.hpp"
#include "Profiling/Profiler.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ManagedScriptCompiler.hpp"
#include "Scripting/ScriptComponent.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/SceneSerializer.hpp"
#include "Undo/InspectorUndoRecorder.hpp"
#include "Utils/Process.hpp"
#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace Funke {
	namespace {
		// Awakens entities that already exist when play begins; runtime-spawned ones are
		// handled at instantiation (SceneSerializer::InstantiatePrefab). Both share the
		// per-component PlayOnAwakeIfEnabled() rule.
		void StartPlayOnAwakeComponents(Scene& scene)
		{
			for (auto [ent, audio] : scene.GetRegistry().view<AudioSourceComponent>(entt::exclude<DisabledTag>).each())
				audio.PlayOnAwakeIfEnabled();

			for (auto [ent, particleSystem] : scene.GetRegistry().view<ParticleSystem2DComponent>(entt::exclude<DisabledTag>).each())
				particleSystem.PlayOnAwakeIfEnabled();

			for (auto [ent, particleSystem] : scene.GetRegistry().view<ParticleSystem3DComponent>(entt::exclude<DisabledTag>).each())
				particleSystem.PlayOnAwakeIfEnabled();
		}
	}

	bool ImGuiEditorLayer::SaveDirtyPrefabsBeforeRun(std::string_view actionName) {
		if (m_PrefabEditScene && (m_PrefabEditScene->IsDirty() || m_PrefabEditDirty)
			&& !SavePrefabEditChanges()) {
			FNK_ERROR_TAG("Editor", "{} aborted because the prefab edit could not be saved.", actionName);
			return false;
		}
		if (m_PrefabInspector.IsOpen() && m_PrefabInspector.HasUnsavedChanges()
			&& !SavePrefabInspectorChanges()) {
			FNK_ERROR_TAG("Editor", "{} aborted because the prefab inspector could not be saved.", actionName);
			return false;
		}
		return true;
	}

	bool ImGuiEditorLayer::ValidateUniquePersistentEntityIds(
		const std::vector<Scene*>& scenes,
		std::string_view actionName) const {
		std::unordered_map<uint64_t, std::string> owners;
		for (const Scene* scene : scenes) {
			if (!scene) continue;
			const auto view = scene->GetRegistry().view<UUIDComponent>();
			for (const EntityHandle entity : view) {
				const uint64_t persistentId = scene->GetEntityPersistentID(entity);
				if (persistentId == 0) {
					FNK_ERROR_TAG("Editor",
						"{} aborted: scene '{}' contains an entity without a persistent ID.",
						actionName, scene->GetName());
					return false;
				}
				const auto [owner, inserted] = owners.emplace(persistentId, scene->GetName());
				if (!inserted) {
					FNK_ERROR_TAG("Editor",
						"{} aborted: persistent entity ID {} is shared by scenes '{}' and '{}'.",
						actionName, persistentId, owner->second, scene->GetName());
					return false;
				}
			}
		}
		return true;
	}

	bool ImGuiEditorLayer::PreparePlayModeSnapshot(Scene& scene) {
		GameLocalization::SetPreviewLanguage({});
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			FNK_ERROR_TAG("PlayMode", "Play Mode requires an active project.");
			return false;
		}
		std::vector<Scene*> loadedScenes;
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			const std::shared_ptr<Scene> loadedScene = weakScene.lock();
			if (!loadedScene) continue;
			if (loadedScene->IsPersistent()) {
				FNK_ERROR_TAG("PlayMode", "Play Mode cannot snapshot persistent scene '{}'.", loadedScene->GetName());
				return false;
			}
			if (loadedScene->IsLifecycleSuspended()) {
				FNK_ERROR_TAG("PlayMode", "Play Mode cannot snapshot suspended recovery scene '{}'.", loadedScene->GetName());
				return false;
			}
			loadedScenes.push_back(loadedScene.get());
		}
		if (!ValidateUniquePersistentEntityIds(loadedScenes, "Play Mode")) {
			return false;
		}
		if (!SaveDirtyPrefabsBeforeRun("Play Mode")) {
			return false;
		}
		if (!ValidateUniquePersistentEntityIds(loadedScenes, "Play Mode")) {
			return false;
		}

		std::vector<std::pair<std::string, std::string>> playModeScenes;
		std::unordered_set<std::string> playModeSceneNames;
		std::unordered_set<std::string> playModeScenePaths;
		std::unordered_set<std::string> dirtySceneNames;
		bool sceneSnapshotFailed = false;
		SceneManager::ForeachLoadedScene([&](Scene& loadedScene) {
			if (sceneSnapshotFailed) return;
			if (!playModeSceneNames.insert(loadedScene.GetName()).second) {
				sceneSnapshotFailed = true;
				FNK_ERROR_TAG("PlayMode", "Loaded scenes must have unique names; '{}' is duplicated.", loadedScene.GetName());
				return;
			}
			const std::string scenePath = project->GetSceneFilePath(loadedScene.GetName());
			if (scenePath.empty()) {
				sceneSnapshotFailed = true;
				FNK_ERROR_TAG("PlayMode", "Scene '{}' has no writable project path.", loadedScene.GetName());
				return;
			}
			if (!playModeScenePaths.insert(scenePath).second) {
				sceneSnapshotFailed = true;
				FNK_ERROR_TAG("PlayMode", "Multiple loaded scenes resolve to snapshot path '{}'.", scenePath);
				return;
			}
			// Freeze even clean scenes so external file edits during Play cannot invalidate the retained history.
			const bool wasDirty = loadedScene.IsDirty();
			const std::filesystem::path snapshotDir = Path::FromUtf8(project->RootDirectory) / ".funke" / "PlayMode";
			std::error_code dirError;
			std::filesystem::create_directories(snapshotDir, dirError);
			const std::string snapshotPath = Path::ToUtf8(snapshotDir / Path::FromUtf8(loadedScene.GetName() + ".scene"));
			if (dirError || !SceneSerializer::SaveToFile(loadedScene, snapshotPath, false)) {
				sceneSnapshotFailed = true;
				FNK_ERROR_TAG("PlayMode", "Failed to snapshot scene '{}'. Play Mode was not started.", loadedScene.GetName());
				return;
			}
			if (wasDirty) {
				loadedScene.MarkDirty();
				dirtySceneNames.insert(loadedScene.GetName());
			}
			if (!File::Exists(snapshotPath)) {
				sceneSnapshotFailed = true;
				FNK_ERROR_TAG("PlayMode", "Scene snapshot is missing: {}", snapshotPath);
				return;
			}
			playModeScenes.push_back({ loadedScene.GetName(), snapshotPath });
		});
		if (sceneSnapshotFailed || playModeScenes.empty()) {
			return false;
		}
		const std::string previousLastOpenedScene = project->LastOpenedScene;
		project->LastOpenedScene = scene.GetName();
		if (!project->Save()) {
			project->LastOpenedScene = previousLastOpenedScene;
			FNK_ERROR_TAG("PlayMode", "Failed to save project metadata. Play Mode was not started.");
			return false;
		}

		m_PlayModeScenes = std::move(playModeScenes);
		m_PlayModeDirtyScenes = std::move(dirtySceneNames);
		m_PlayModeActiveScene = scene.GetName();
		return true;
	}

	void ImGuiEditorLayer::ReportPlayModeBuildFailure() {
		DrainPendingLogEntries();
		auto appendMissing = [this](const std::string& message) {
			if (message.empty()) return;
			const bool visible = std::any_of(m_LogEntries.begin(), m_LogEntries.end(), [&](const LogEntry& entry) {
				return entry.Message.find(message) != std::string::npos;
			});
			if (!visible) AppendLogEntry({ message,
				message.find("warning ") != std::string::npos ? Log::Level::Warn : Log::Level::Error });
		};
		std::istringstream output(ManagedScriptCompiler::LimitOutput(ScriptSystem::GetLastRebuildOutput()));
		std::string line;
		while (std::getline(output, line)) {
			appendMissing(line);
		}
		appendMissing("Script compilation or assembly reload failed. Play Mode was not started.");
	}

	void ImGuiEditorLayer::BeginPlayModeRequest(Scene& scene) {
		if (m_SceneOpenOperation != 0 || !m_SceneOpenFallbackPath.empty()) return;
		if (!PreparePlayModeSnapshot(scene)) {
			m_PlayModeRecompileScene.reset();
			m_PlayModeScenes.clear();
			m_PlayModeActiveScene.clear();
			m_PlayModeScriptFields.clear();
			return;
		}
		CaptureScriptFieldOverrides(/*merge=*/false);

		ApplicationEditorAccess::SetPlaymodePaused(false);

		ScriptSystem* scriptSys = scene.HasSystem<ScriptSystem>() ? scene.GetSystem<ScriptSystem>() : nullptr;
		if (scriptSys && EditorPreferences::GetRecompileScriptsOnPlay()) {
			const bool rebuildStarted = scriptSys->RequestRebuildAndReloadAll();
			if (rebuildStarted || scriptSys->IsRebuilding()) {
				m_PlayModeRecompileScene = SceneManager::GetLoadedScene(scene.GetName());
				m_PlayModeRecompilePending = true;
				return;
			}
		}
		if (scriptSys && scriptSys->IsRebuilding()) {
			m_PlayModeRecompileScene = SceneManager::GetLoadedScene(scene.GetName());
			m_PlayModeRecompilePending = true;
			return;
		}
		if (scriptSys && !scriptSys->DidLastRebuildSucceed()) {
			m_PlayModeRecompileScene.reset();
			m_PlayModeScenes.clear();
			m_PlayModeActiveScene.clear();
			m_PlayModeScriptFields.clear();
			ReportPlayModeBuildFailure();
			return;
		}

		CompletePlayModeEntry(scene);
	}

	void ImGuiEditorLayer::CompletePlayModeEntry(Scene& scene) {
		m_PlayModeRecompilePending = false;
		m_PlayModeRecompileScene.reset();
		if (EditorPreferences::GetResetRegisteredStateOnPlay() && !ScriptEngine::ResetRegisteredPlayState()) {
			m_PlayModeScenes.clear();
			m_PlayModeActiveScene.clear();
			m_PlayModeScriptFields.clear();
			FNK_ERROR_TAG("PlayMode", "Reset of registered game state failed. Play Mode was not started; see the reset method error above.");
			return;
		}
		m_LogEntries.clear();
		m_SelectedLogIndex = -1;
		m_LogScrollToEnd = m_LogAutoScroll;
		m_PlayModeRecompilePending = false;
		m_PlayModeRecompileScene.reset();
		m_PlayModeSelection = CaptureLoadedSceneSelection();
		if (Scene* context = GetContextScene()) FinishInspectorRename(*context);
		m_UndoHistory.SuspendForPlay(SceneManager::GetLoadedScenes());
		// Step counter is panel-frame state, not playmode state — without a reset
		// here a Step that didn't finish draining before the previous Stop would
		// trip the bottom-of-RenderToolbar auto-pause on this fresh session's first
		// frame, freezing the new Play instantly.
		m_StepFrames = 0;
		ApplicationEditorAccess::SetPlaymodePaused(false);
		SceneManager::ForeachLoadedScene([](Scene& loadedScene) { loadedScene.ClearDirty(); });
		Application::SetIsPlaying(true);
		m_FocusGameViewOnPlay = true;
		// Reset Time.TimeSinceStartup / Time.RealtimeSinceStartup so each
		// play session starts at t=0 from a script's perspective.
		ApplicationEditorAccess::MarkGameStart();
		// Re-seed the fixed timestep from the project default (see FunkeProject::PhysicsTickRate)
		// so a prior session's scripted Time.fixedDeltaTime override doesn't leak across
		// Stop/Play. Built games get this for free at Application::Initialize.
		if (const FunkeProject* project = ProjectManager::GetCurrentProject()) {
			Time::SetFixedDeltaTime(project->GetFixedDeltaTimeSeconds());
			ProjectManager::ApplyPhysicsDefaults();
			RenderSettings::Apply(project->Graphics);
		}
		for (const auto& weakScene : SceneManager::GetLoadedScenes()) {
			if (auto loaded = weakScene.lock(); loaded && loaded->IsLoaded() && !loaded->IsLifecycleSuspended())
				loaded->StartManagedSceneSystemsForPlayMode();
		}
		// Reset Box2D sleep timers so bodies that sat through editor
		// idle don't immediately freeze on the first physics step.
		if (PhysicsSystem2D::IsInitialized()) {
			PhysicsSystem2D::WakeAllBodies();
		}
		StartPlayOnAwakeComponents(scene);
	}

	void ImGuiEditorLayer::PollPendingPlayModeRequest() {
		if (!m_PlayModeRecompilePending || Application::GetIsPlaying()) {
			return;
		}

		const std::shared_ptr<Scene> rebuildScene = m_PlayModeRecompileScene.lock();
		ScriptSystem* scriptSys = rebuildScene && rebuildScene->HasSystem<ScriptSystem>()
			? rebuildScene->GetSystem<ScriptSystem>() : nullptr;
		if (scriptSys && scriptSys->IsRebuilding()) {
			return;
		}

		if (!scriptSys || !scriptSys->DidLastRebuildSucceed()) {
			m_PlayModeRecompilePending = false;
			m_PlayModeRecompileScene.reset();
			m_PlayModeScenes.clear();
			m_PlayModeActiveScene.clear();
			m_PlayModeScriptFields.clear();
			if (scriptSys) {
				ReportPlayModeBuildFailure();
			}
			else {
				FNK_ERROR_TAG("PlayMode", "The scene that requested script compilation is no longer loaded. Play Mode was not started.");
			}
			return;
		}

		Scene* activeScene = SceneManager::GetActiveScene();
		if (!activeScene || !PreparePlayModeSnapshot(*activeScene)) {
			m_PlayModeRecompilePending = false;
			m_PlayModeRecompileScene.reset();
			m_PlayModeScenes.clear();
			m_PlayModeActiveScene.clear();
			m_PlayModeScriptFields.clear();
			return;
		}
		CaptureScriptFieldOverrides(/*merge=*/true);
		CompletePlayModeEntry(*activeScene);
	}

	void ImGuiEditorLayer::LaunchStandalone() {
		FunkeProject* project = ProjectManager::GetCurrentProject();
		if (!project) {
			FNK_CORE_WARN_TAG("Editor",
				"LaunchStandalone: no project loaded — nothing to launch.");
			return;
		}
		if (m_ReserializeState != 0) {
			FNK_CORE_WARN_TAG("Editor", "LaunchStandalone: asset reserialization is still in progress.");
			return;
		}
		std::vector<Scene*> loadedScenes;
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			const std::shared_ptr<Scene> loadedScene = weakScene.lock();
			if (!loadedScene) continue;
			if (loadedScene->IsLifecycleSuspended()) {
				FNK_ERROR_TAG("Editor", "Launch Standalone aborted: scene '{}' is suspended for recovery.", loadedScene->GetName());
				return;
			}
			loadedScenes.push_back(loadedScene.get());
		}
		if (!ValidateUniquePersistentEntityIds(loadedScenes, "Launch Standalone")) {
			return;
		}

		// Tries two layouts: dev-tree sibling dir, then same dir as editor exe.
		const std::filesystem::path exeDir = Path::FromUtf8(Path::ExecutableDir());
#ifdef FNK_PLATFORM_WINDOWS
		constexpr const char* k_RuntimeExe = "Funke-Runtime.exe";
#else
		constexpr const char* k_RuntimeExe = "Funke-Runtime";
#endif
		std::filesystem::path runtimePath = exeDir.parent_path() / "Funke-Runtime" / k_RuntimeExe;
		if (!std::filesystem::exists(runtimePath)) {
			runtimePath = exeDir / k_RuntimeExe;
		}
		if (!std::filesystem::exists(runtimePath)) {
			FNK_CORE_ERROR_TAG("Editor",
				"LaunchStandalone: Funke-Runtime executable not found near editor "
				"(tried '{}' and '{}'). Build the engine's Runtime project first.",
				Path::ToUtf8(exeDir.parent_path() / "Funke-Runtime" / k_RuntimeExe),
				Path::ToUtf8(exeDir / k_RuntimeExe));
			return;
		}
		if (!SaveDirtyPrefabsBeforeRun("Launch Standalone")) {
			return;
		}
		if (!ValidateUniquePersistentEntityIds(loadedScenes, "Launch Standalone")) {
			return;
		}

		const std::string previousLastOpenedScene = project->LastOpenedScene;
		if (!Application::GetIsPlaying()) {
			bool saveFailed = false;
			bool sawLoadedScene = false;
			SceneManager::ForeachLoadedScene([&](Scene& loadedScene) {
				if (saveFailed) return;
				sawLoadedScene = true;
				const std::string scenePath = project->GetSceneFilePath(loadedScene.GetName());
				if (scenePath.empty()) {
					saveFailed = true;
					FNK_ERROR_TAG("Editor", "LaunchStandalone: scene '{}' has no writable project path.", loadedScene.GetName());
					return;
				}
				if (loadedScene.IsDirty() && !SceneSerializer::SaveToFile(loadedScene, scenePath)) {
					saveFailed = true;
					FNK_ERROR_TAG("Editor", "LaunchStandalone: failed to save scene '{}'.", loadedScene.GetName());
					return;
				}
				if (!File::Exists(scenePath)) {
					saveFailed = true;
					FNK_ERROR_TAG("Editor", "LaunchStandalone: scene snapshot is missing: {}", scenePath);
				}
			});
			if (saveFailed || !sawLoadedScene) {
				if (!sawLoadedScene) {
					FNK_ERROR_TAG("Editor", "LaunchStandalone: no loaded scene is available.");
				}
				return;
			}
		}

		if (!Application::GetIsPlaying()) {
			if (Scene* active = SceneManager::GetActiveScene()) {
				project->LastOpenedScene = active->GetName();
			}
		}
		if (!project->Save()) {
			project->LastOpenedScene = previousLastOpenedScene;
			FNK_ERROR_TAG("Editor", "LaunchStandalone: failed to save project metadata.");
			return;
		}

		const std::string runtimeStr = Path::ToUtf8(runtimePath);
		const std::string projectArg = "--project=" + project->RootDirectory;
		FNK_CORE_INFO_TAG("Editor",
			"LaunchStandalone: spawning '{}' for project '{}'",
			runtimeStr, project->Name);

		unsigned long runtimePid = 0;
		std::string loaderError;
		if (!Process::LaunchDetachedChecked({ runtimeStr, projectArg },
			runtimePath.parent_path(), runtimePid, loaderError))
		{
			if (!loaderError.empty()) {
				FNK_CORE_ERROR_TAG("Editor", "LaunchStandalone: '{}' did not start. {}", runtimeStr, loaderError);
			}
			else {
				FNK_CORE_ERROR_TAG("Editor",
					"LaunchStandalone: failed to spawn '{}' (Process::LaunchDetached "
					"returned false). See earlier log for the OS-level reason.",
					runtimeStr);
			}
		}
	}

	void ImGuiEditorLayer::CaptureScriptFieldOverrides(bool merge) {
		if (!merge) {
			m_PlayModeScriptFields.clear();
		}
		int captured = 0;
		SceneManager::ForeachLoadedScene([&](Scene& s) {
			auto view = s.GetRegistry().view<ScriptComponent>();
			for (EntityHandle entity : view) {
				const ScriptComponent& sc = view.get<ScriptComponent>(entity);
				if (sc.PendingFieldValues.empty()) {
					continue;
				}
				const uint64_t uuid = s.GetEntityPersistentID(entity);
				if (uuid == 0) {
					continue;
				}
				auto& fields = m_PlayModeScriptFields[s.GetName()][uuid];
				for (const auto& [key, value] : sc.PendingFieldValues) {
					fields[key] = value;
					++captured;
				}
			}
		});
		FNK_CORE_INFO_TAG("PlayFix", "CaptureScriptFieldOverrides(merge={}) captured {} field(s)", merge, captured);
	}

	void ImGuiEditorLayer::ReapplyScriptFieldOverrides(const std::vector<std::shared_ptr<Scene>>& scenes) {
		FNK_CORE_INFO_TAG("PlayFix", "ReapplyScriptFieldOverrides: {} scene(s) in snapshot", m_PlayModeScriptFields.size());
		for (const std::shared_ptr<Scene>& loaded : scenes) {
			if (!loaded) {
				continue;
			}
			const std::string& sceneName = loaded->GetName();
			const auto sceneFields = m_PlayModeScriptFields.find(sceneName);
			if (sceneFields == m_PlayModeScriptFields.end()) continue;
			const auto& entities = sceneFields->second;
			for (const auto& [uuid, fields] : entities) {
				EntityHandle handle = entt::null;
				if (!loaded->TryResolveEntityRef(uuid, handle)) {
					FNK_CORE_WARN_TAG("PlayFix", "reapply: scene '{}' uuid={} did NOT resolve to an entity — skipping", sceneName, uuid);
					continue;
				}
				if (!loaded->HasComponent<ScriptComponent>(handle)) {
					FNK_CORE_WARN_TAG("PlayFix", "reapply: scene '{}' uuid={} has no ScriptComponent — skipping", sceneName, uuid);
					continue;
				}
				ScriptComponent& sc = loaded->GetComponent<ScriptComponent>(handle);
				for (const auto& [key, value] : fields) {
					sc.PendingFieldValues[key] = value;
				}
			}
		}
	}

	void ImGuiEditorLayer::RequestPlayModeStop(std::string sceneToOpen) {
		if (!m_PlayModeStopRequested) m_PlayModeStopRequestTime = std::chrono::steady_clock::now();
		m_PlayModeStopRequested = true;
		if (!sceneToOpen.empty()) {
			m_PlayModeStopOpenScene = std::move(sceneToOpen);
		}
	}

	void ImGuiEditorLayer::ProcessPendingPlayModeStop() {
		if (!m_PlayModeStopRequested) {
			return;
		}
		m_PlayModeStopRequested = false;
		std::string sceneToOpen = std::exchange(m_PlayModeStopOpenScene, {});
		if (!Application::GetIsPlaying()) {
			return;
		}

		if (Scene* act = SceneManager::GetActiveScene()) {
			for (auto [ent, audio] : act->GetRegistry().view<AudioSourceComponent>().each()) {
				if (audio.IsPlaying() || audio.IsPaused()) audio.Stop();
			}
		}
		// The game can't release a mouse it locked any more; give it back before the editor scene returns.
		if (Window* window = Application::GetWindow()) window->SetCursorMode(0);
		const bool restored = RestoreEditorSceneAfterPlaymode();
		if (restored) m_PlayModeStopFramesToMeasure = 2;
		if (restored && !sceneToOpen.empty()) {
			RequestEditorAction(PendingEditorAction::OpenScene, std::move(sceneToOpen));
		}
	}

	bool ImGuiEditorLayer::RestoreEditorSceneAfterPlaymode() {
		if (m_PlayModeScenes.empty()) {
			FNK_ERROR_TAG("PlayMode", "No edit-mode scene snapshot is available.");
			return false;
		}

		if (!ScriptEngine::PrepareResourceRelease(true)) {
			FNK_ERROR_TAG("PlayMode", "Stop refused: script jobs are still running.");
			return false;
		}
		struct ResumeScriptScope {
			~ResumeScriptScope() { ScriptEngine::ResumeResourceScope(true); }
		} resumeScriptScope;

		FUNKE_PROFILE_SCOPE("PlayMode Stop");
		using StopClock = std::chrono::steady_clock;
		const StopClock::time_point stopStart = StopClock::now();
		StopClock::time_point stageStart = stopStart;
		auto stageMs = [&stageStart]() {
			const StopClock::time_point now = StopClock::now();
			const double ms = std::chrono::duration<double, std::milli>(now - stageStart).count();
			stageStart = now;
			return ms;
		};
		double reloadMs = 0.0, releaseMs = 0.0, scriptsMs = 0.0;

		const bool inPrefabEdit = IsInPrefabEditMode();
		const LoadedSceneSelectionSnapshot interruptedPlaySelection = CaptureLoadedSceneSelection();
		std::vector<std::shared_ptr<Scene>> playScenes;
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			if (std::shared_ptr<Scene> scene = weakScene.lock()) {
				playScenes.push_back(std::move(scene));
			}
		}

		// No capture here: values edited in the inspector during play are discarded like every other play-time change.
		struct StagedScene {
			std::string Name;
			std::string DefinitionName;
			std::shared_ptr<Scene> SceneInstance;
		};
		std::vector<StagedScene> stagedScenes;
		stagedScenes.reserve(m_PlayModeScenes.size());
		struct StagedSceneCleanupGuard {
			std::vector<StagedScene>& Scenes;
			bool Armed = true;
			~StagedSceneCleanupGuard() {
				if (!Armed) return;
				for (const StagedScene& staged : Scenes) {
					try { SceneManager::UnregisterScene(staged.DefinitionName); }
					catch (...) {}
				}
			}
		};
		StagedSceneCleanupGuard stagedSceneCleanup{ stagedScenes };
		struct PlayModeRestoreGuard {
			bool PreviousState;
			bool Armed = true;
			~PlayModeRestoreGuard() {
				if (Armed) Application::SetIsPlaying(PreviousState);
			}
		};
		PlayModeRestoreGuard playModeRestore{ Application::GetIsPlaying() };
		Application::SetIsPlaying(false);
		auto discardStagedScenes = [&]() {
			for (const StagedScene& staged : stagedScenes) {
				SceneManager::UnregisterScene(staged.DefinitionName);
			}
			stagedScenes.clear();
		};
		for (const auto& [sceneName, scenePath] : m_PlayModeScenes) {
			std::shared_ptr<Scene> stagedScene;
			std::string definitionName;
			if (!LoadEditorScene(scenePath, true, stagedScene, definitionName, true, true)) {
				discardStagedScenes();
				FNK_ERROR_TAG("PlayMode", "Failed to restore scene '{}'. Play Mode remains active.", sceneName);
				return false;
			}
			if (stagedScene->GetName() != sceneName) {
				const std::string serializedName = stagedScene->GetName();
				SceneManager::UnregisterScene(definitionName);
				discardStagedScenes();
				FNK_ERROR_TAG("PlayMode",
					"Scene snapshot '{}' identifies itself as '{}'. Play Mode remains active.",
					sceneName, serializedName);
				return false;
			}
			try {
				stagedScenes.push_back({ sceneName, definitionName, stagedScene });
			}
			catch (...) {
				SceneManager::UnregisterScene(definitionName);
				throw;
			}
		}

		reloadMs = stageMs();
		std::vector<Scene*> stagedScenePointers;
		std::vector<std::shared_ptr<Scene>> restoredScenes;
		std::unordered_set<std::string> restoredNames;
		std::unordered_set<std::string> restoredDefinitions;
		std::unordered_map<std::string, std::string> restoredDefinitionsByName;
		stagedScenePointers.reserve(stagedScenes.size());
		restoredScenes.reserve(stagedScenes.size());
		restoredNames.reserve(stagedScenes.size());
		restoredDefinitions.reserve(stagedScenes.size());
		restoredDefinitionsByName.reserve(stagedScenes.size());
		std::shared_ptr<Scene> restoredActiveScene;
		for (const StagedScene& staged : stagedScenes) {
			if (!restoredNames.insert(staged.SceneInstance->GetName()).second) {
				discardStagedScenes();
				FNK_ERROR_TAG("PlayMode", "Restored scene name '{}' is duplicated. Play Mode remains active.", staged.Name);
				return false;
			}
			stagedScenePointers.push_back(staged.SceneInstance.get());
			restoredScenes.push_back(staged.SceneInstance);
			restoredDefinitions.insert(staged.DefinitionName);
			restoredDefinitionsByName.emplace(staged.SceneInstance->GetName(), staged.DefinitionName);
			if (staged.Name == m_PlayModeActiveScene) {
				restoredActiveScene = staged.SceneInstance;
			}
		}
		if (!ValidateUniquePersistentEntityIds(stagedScenePointers, "Play Mode restore")) {
			discardStagedScenes();
			return false;
		}
		if (!restoredActiveScene && !restoredScenes.empty()) {
			restoredActiveScene = restoredScenes.front();
		}
		if (!restoredActiveScene) {
			discardStagedScenes();
			FNK_ERROR_TAG("PlayMode", "Restored scene set has no active-scene candidate. Play Mode remains active.");
			return false;
		}

		const std::unordered_set<std::string> previousDefinitions = m_EditorOwnedSceneDefinitions;
		std::vector<std::shared_ptr<Scene>> activatedScenes;
		activatedScenes.reserve(restoredScenes.size());
		Application::SetIsPlaying(playModeRestore.PreviousState);
		for (const std::shared_ptr<Scene>& playScene : playScenes) {
			if (SceneManager::SuspendLoadedSceneForRelease(playScene)) continue;

			ScriptEngine::ResumeResourceScope(true);

			discardStagedScenes();
			Application::SetIsPlaying(playModeRestore.PreviousState);
			bool playScenesRecovered = true;
			std::vector<std::shared_ptr<Scene>> recoveredPlayScenes;
			recoveredPlayScenes.reserve(playScenes.size());
			for (const std::shared_ptr<Scene>& recoveryScene : playScenes) {
				if (!recoveryScene->IsLifecycleSuspended()) continue;
				if (!SceneManager::RollbackLoadedSceneRecovery(recoveryScene)) {
					playScenesRecovered = false;
					continue;
				}
				recoveredPlayScenes.push_back(recoveryScene);
			}
			std::vector<std::shared_ptr<Scene>> reactivatedPlayScenes;
			if (playScenesRecovered) {
				reactivatedPlayScenes.reserve(recoveredPlayScenes.size());
				for (const std::shared_ptr<Scene>& recoveryScene : recoveredPlayScenes) {
					if (!SceneManager::ActivateSuspendedScene(recoveryScene)) {
						playScenesRecovered = false;
						break;
					}
					reactivatedPlayScenes.push_back(recoveryScene);
				}
			}
			if (playScenesRecovered) {
				for (const std::shared_ptr<Scene>& recoveryScene : reactivatedPlayScenes) {
					if (!SceneManager::CommitSuspendedSceneActivation(recoveryScene)) {
						playScenesRecovered = false;
					}
				}
			}
			if (!playScenesRecovered) {
				for (auto it = reactivatedPlayScenes.rbegin(); it != reactivatedPlayScenes.rend(); ++it) {
					if (!(*it)->IsLifecycleSuspended()) {
						SceneManager::ResuspendSceneForRecovery(*it);
					}
				}
				Application::SetIsPlaying(false);
				std::shared_ptr<Scene> recoveryActiveScene;
				for (const std::shared_ptr<Scene>& recoveryScene : playScenes) {
					if (recoveryScene->IsLifecycleSuspended()) {
						recoveryActiveScene = recoveryScene;
						break;
					}
				}
				if (recoveryActiveScene) {
					SceneManager::SetActiveSuspendedSceneForRecovery(recoveryActiveScene);
				}
			}
			RestoreLoadedSceneSelection(interruptedPlaySelection);
			if (!playScenesRecovered) {
				playModeRestore.Armed = false;
				m_PlayModeRestoreError = "Play Mode could not stop because scene lifecycle teardown failed, and the live Play scenes could not be fully resumed. Inspect the loaded scenes before saving.";
				m_ShowPlayModeRestoreError = true;
				FNK_ERROR_TAG("PlayMode", "{}", m_PlayModeRestoreError);
			}
			else {
				FNK_ERROR_TAG("PlayMode", "Play Mode could not stop because scene '{}' could not suspend safely.", playScene->GetName());
			}
			return false;
		}
		Application::SetIsPlaying(false);
		stagedSceneCleanup.Armed = false;
		playModeRestore.Armed = false;
		bool restoreSucceeded = true;
		std::string restoreFailure;
		const double suspendMs = stageMs();
		for (const std::shared_ptr<Scene>& playScene : playScenes) {
			if (!SceneManager::ReleaseLoadedSceneInstance(playScene, false, false) && playScene->IsLoaded()) {
				restoreSucceeded = false;
				restoreFailure = "A Play Mode scene could not be released safely.";
				FNK_ERROR_TAG("PlayMode", "Failed to release Play Mode scene instance '{}'.", playScene->GetName());
			}
		}
		for (const std::string& definitionName : previousDefinitions) {
			SceneManager::UnregisterScene(definitionName);
		}
		// Scripts own their streams; one that wasn't disposed would keep playing in edit mode.
		AudioStreams::CloseAll();
		releaseMs = stageMs();

		try {
			ScriptEngine::OnPlayModeExited();
			scriptsMs = stageMs();
		}
		catch (const std::exception& exception) {
			restoreSucceeded = false;
			restoreFailure = "Play Mode script cleanup failed: " + std::string(exception.what());
			FNK_ERROR_TAG("PlayMode", "{}", restoreFailure);
		}
		catch (...) {
			restoreSucceeded = false;
			restoreFailure = "Play Mode script cleanup failed.";
			FNK_ERROR_TAG("PlayMode", "{}", restoreFailure);
		}

		if (restoreSucceeded) {
			try {
				Time::SetTimeScale(1.0f);
				if (const FunkeProject* project = ProjectManager::GetCurrentProject()) {
					Time::SetFixedDeltaTime(project->GetFixedDeltaTimeSeconds());
					ProjectManager::ApplyPhysicsDefaults();
					RenderSettings::Apply(project->Graphics);
				}
				ReapplyScriptFieldOverrides(restoredScenes);
			}
			catch (const std::exception& exception) {
				restoreSucceeded = false;
				restoreFailure = "Could not restore inspector-authored script fields: " + std::string(exception.what());
				FNK_ERROR_TAG("PlayMode", "{}", restoreFailure);
			}
			catch (...) {
				restoreSucceeded = false;
				restoreFailure = "Could not restore inspector-authored script fields.";
				FNK_ERROR_TAG("PlayMode", "{}", restoreFailure);
			}
		}

		if (restoreSucceeded) {
			for (const std::shared_ptr<Scene>& restoredScene : restoredScenes) {
				// Just deserialized from the Play snapshot, so the rollback baseline needs no extra round-trip check.
				if (!SceneManager::ActivateSuspendedScene(restoredScene, false)) {
					restoreSucceeded = false;
					restoreFailure = "Restored scene '" + restoredScene->GetName() + "' could not be activated.";
					break;
				}
				activatedScenes.push_back(restoredScene);
			}
		}
		if (restoreSucceeded) {
			try {
				restoreSucceeded = SceneManager::SetActiveScene(*restoredActiveScene);
			}
			catch (...) {
				restoreSucceeded = false;
			}
			if (!restoreSucceeded) {
				restoreFailure = "The restored active scene could not be selected.";
			}
		}

		bool recoveryExact = true;
		if (!restoreSucceeded) {
			for (auto it = activatedScenes.rbegin(); it != activatedScenes.rend(); ++it) {
				if (!SceneManager::ResuspendSceneForRecovery(*it)) {
					recoveryExact = false;
					FNK_ERROR_TAG("PlayMode", "Failed to restore suspended recovery state for scene '{}'.", (*it)->GetName());
				}
			}
			std::shared_ptr<Scene> recoveryActiveScene = restoredActiveScene;
			if (!recoveryActiveScene->IsLifecycleSuspended()) {
				recoveryActiveScene.reset();
				for (const std::shared_ptr<Scene>& restoredScene : restoredScenes) {
					if (restoredScene->IsLifecycleSuspended()) {
						recoveryActiveScene = restoredScene;
						break;
					}
				}
			}
			if (!recoveryActiveScene
				|| !SceneManager::SetActiveSuspendedSceneForRecovery(recoveryActiveScene)) {
				recoveryExact = false;
				FNK_ERROR_TAG("PlayMode", "Recovered edit scenes could not be selected for inspection.");
			}
			else {
				restoredActiveScene = std::move(recoveryActiveScene);
			}
		}
		else {
			for (const std::shared_ptr<Scene>& restoredScene : restoredScenes) {
				if (!SceneManager::CommitSuspendedSceneActivation(restoredScene)) {
					restoreSucceeded = false;
					recoveryExact = false;
					restoreFailure = "A restored scene activation could not be finalized.";
					FNK_ERROR_TAG("PlayMode", "Failed to finalize restored scene activation for '{}'.", restoredScene->GetName());
				}
			}
		}

		m_EditorOwnedSceneDefinitions.swap(restoredDefinitions);
		m_EditorSceneDefinitionsBySceneName.swap(restoredDefinitionsByName);
		if (!m_UndoHistory.RestoreAfterPlay(SceneManager::GetLoadedScenes(), restoreSucceeded)) {
			ShowHistoryStatus("Undo/redo history ended", "Play-mode scene restoration failed. Suspended edit histories were discarded; inspect the recovered scenes before saving.");
		}
		m_GizmoDragActive = false;
		m_GizmoDragBefore.clear();
		m_EditorPausedAudioEntities.clear();
		m_PlayModeRecompilePending = false;
		m_PlayModeRecompileScene.reset();
		m_StepFrames = 0;
		ApplicationEditorAccess::SetPlaymodePaused(false);

		if (!inPrefabEdit) {
			ClearEntitySelection();
			m_CutEntities.clear();
			GetHierarchyState().CollapsedEntities.clear();
			GetHierarchyState().EntityOrder.clear();
			GetHierarchyState().Dirty = true;
			m_ShowEntityDeleteConfirm = false;
			m_PendingEntityDeleteSceneId = 0;
			m_PendingEntityDeleteCount = 0;
			m_PendingEntityDeleteLabel.clear();

			if (!m_PlayModeSelection.SceneName.empty()
				&& SceneManager::GetLoadedScene(m_PlayModeSelection.SceneName).lock()) {
				RestoreLoadedSceneSelection(m_PlayModeSelection);
			}
			else {
				m_LastInteractedSceneName = restoredActiveScene->GetName();
			}
		}

		// Scenes restored from a private snapshot still differ from their file on disk.
		for (const std::string& dirtyName : m_PlayModeDirtyScenes) {
			if (std::shared_ptr<Scene> restored = SceneManager::GetLoadedScene(dirtyName).lock()) {
				restored->MarkDirty();
			}
		}
		m_PlayModeDirtyScenes.clear();
		m_PlayModeScenes.clear();
		m_PlayModeActiveScene.clear();
		m_PlayModeSelection = {};
		m_PlayModeScriptFields.clear();

		if (!restoreSucceeded) {
			if (restoreFailure.empty()) restoreFailure = "The edit-scene restore did not complete.";
			m_PlayModeRestoreError = restoreFailure
				+ (recoveryExact
					? " Play Mode stopped and the recovered edit scenes remain loaded for inspection."
					: " Play Mode stopped, but recovery was incomplete. Inspect the loaded scenes before saving.");
			m_ShowPlayModeRestoreError = true;
			FNK_ERROR_TAG("PlayMode", "{}", m_PlayModeRestoreError);
			return false;
		}
		const double activateMs = stageMs();
		FNK_INFO_TAG("PlayMode", "Stopped in {:.1f} ms (reload {:.1f}, suspend {:.1f}, release {:.1f}, scripts {:.1f}, activate {:.1f})",
			std::chrono::duration<double, std::milli>(StopClock::now() - stopStart).count(),
			reloadMs, suspendMs, releaseMs, scriptsMs, activateMs);
		return true;
	}
}
