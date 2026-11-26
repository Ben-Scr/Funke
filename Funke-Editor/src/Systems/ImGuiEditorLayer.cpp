#include "Systems/ImGuiEditorLayer.hpp"
#include "Assets/AssetRegistry.hpp"
#include "Core/Application.hpp"
#include "Core/Window.hpp"
#include "Editor/ApplicationEditorAccess.hpp"
#include "Editor/EditorPreferences.hpp"
#include "Editor/ExternalEditor.hpp"
#include "Events/EventDispatcher.hpp"
#include "Events/SceneEvents.hpp"
#include "Events/WindowFocusEvent.hpp"
#include "Events/WindowLostFocusEvent.hpp"
#include "Graphics/MeshAsset.hpp"
#include "Graphics/Renderer2D.hpp"
#include "Graphics/SkyAmbient.hpp"
#include "Graphics/SpriteUVResolver.hpp"
#include "Graphics/TextureManager.hpp"
#include "Gui/EditorDockLayout.hpp"
#include "Gui/EditorIcons.hpp"
#include "Gui/EditorShortcuts.hpp"
#include "Gui/GuiRenderer.hpp"
#include "Gui/Icons.hpp"
#include "Inspector/PropertyDrawer.hpp"
#include "Inspector/ReferencePicker.hpp"
#include "Localization/GameLocalization.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptComponentInspector.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Serialization/PrefabTemplateCache.hpp"
#include "Undo/InspectorUndoRecorder.hpp"
#include "Utils/Process.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

namespace Funke {
	namespace {
		std::string FormatLogClockTimestamp() {
			const std::time_t now = std::time(nullptr);
			std::tm local{};
#if defined(_WIN32)
			if (localtime_s(&local, &now) != 0) return "--:--:--";
#else
			if (!localtime_r(&now, &local)) return "--:--:--";
#endif
			char buffer[16];
			std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d",
				local.tm_hour, local.tm_min, local.tm_sec);
			return std::string(buffer);
		}

		std::string ToLowerCopy(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(),
				[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return value;
		}
	}

	void ImGuiEditorLayer::OnAttach(Application& app) {
		m_UndoHistory.SetBoundaryCallbacks([](UndoStack& stack) { InspectorUndoRecorder::Flush(stack, true); },
			[] { InspectorUndoRecorder::Reset(); });
		m_SceneOpenLifetime = std::make_shared<int>(0);
		// Start the splash first: SignalSplashAttached (inside Begin) must run
		// before Application's deferred-scene check so the project scene load is
		// held behind the splash.
		m_Splash.Begin();
		Application::SetIsPlaying(false);
		ApplicationEditorAccess::SetGameInputEnabled(false);
		// Scope the project's custom game cursor to the Game View: disable engine
		// cursor application now (before Application applies the project cursor in
		// Initialize) so it never goes editor-wide. OnUpdate re-enables it only
		// while the Game View is hovered/focused.
		if (Window* w = app.GetWindow()) {
			w->SetGameCursorEnabled(false);
		}
		Gizmo::SetShowInRuntime(false);
		if (app.GetRenderer2D()) {
			app.GetRenderer2D()->SetSkipBeginFrameRender(true);
		}
		// UI must render into the per-panel FBO, not the OS backbuffer; RenderSceneIntoFBO drives this manually.
		if (app.GetGuiRenderer()) {
			app.GetGuiRenderer()->SetSkipBeginFrameRender(true);
		}

		if (m_LogSubscriptionId.value != 0) {
			Log::OnLog.Remove(m_LogSubscriptionId);
		}

		m_ProfilerPanel.Initialize();
		m_SpriteEditorPanel.Initialize();
		m_ScriptWindows.Initialize();

		if (auto* settings = EditorDockLayout::WindowSettings::Get()) {
			const std::map<std::string, bool*> windows{
				{ "Build", &m_ShowBuildPanel }, { "Build Profiles", &m_ShowBuildProfilesPanel },
				{ "Project Settings", &m_ShowProjectSettings }, { "Package Manager", &m_ShowPackageManager },
				{ "Editor Preferences", &m_ShowEditorPreferences }, { "Profiler", &m_ShowProfiler },
                { "String Tables", &m_ShowStringTables },
				{ "Animation", &m_ShowAnimationWindow }, { "Animator", &m_ShowAnimatorWindow },
				{ "Sprite Editor", &m_ShowSpriteEditor }
			};
			settings->SetCallbacks([this, windows]() {
				EditorDockLayout::WindowVisibility state;
				for (const auto& [name, open] : windows) state[name] = *open;
				m_ScriptWindows.CaptureLayoutWindows(state);
				return state;
			}, [this, windows](const EditorDockLayout::WindowVisibility& state) {
				for (const auto& [name, open] : windows) {
					if (const auto found = state.find(name); found != state.end()) *open = found->second;
				}
				m_ScriptWindows.ApplyLayoutWindows(state);
			});
		}

		// Load BEFORE ApplyTheme: ImGuiContextLayer already ran ApplyFunkeTheme on its OnAttach; this reskins the live style with the user's saved choice.
		EditorPreferences::Load();
		EditorPreferences::ApplyTheme();
		m_EditorPreferencesPanel.Initialize();

		// One-time migration: seed EditorPreferences from legacy inline project fields if no prefs file existed yet.
		if (EditorPreferences::WasFreshlyCreated()) {
			if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
				const auto& lp = project->LegacyEditorPrefs;
				if (lp.ShowFileExtensionsPresent) {
					EditorPreferences::SetShowFileExtensions(lp.ShowFileExtensions);
				}
				if (lp.AutoSaveScenesPresent) {
					EditorPreferences::SetAutoSaveScenes(lp.AutoSaveScenes);
				}
				if (lp.AutoSaveIntervalSecondsPresent) {
					EditorPreferences::SetAutoSaveIntervalSeconds(lp.AutoSaveIntervalSeconds);
				}
				if (lp.AutoRecompileScriptsPresent) {
					EditorPreferences::SetAutoRecompileScripts(lp.AutoRecompileScripts);
				}
				if (lp.RecompileScriptsOnPlayPresent) {
					EditorPreferences::SetRecompileScriptsOnPlay(lp.RecompileScriptsOnPlay);
				}
			}
		}
		ScriptSystem::SetAutoRecompileEnabled(EditorPreferences::GetAutoRecompileScripts());

		ClearLogEntries();
		m_LogDispatchState = std::make_shared<LogDispatchState>();
		std::weak_ptr<LogDispatchState> weakLogDispatchState = m_LogDispatchState;
		m_LogSubscriptionId = Log::OnLog.Add([weakLogDispatchState](const Log::Entry& entry) {
			if (entry.Source == Log::Type::Core) return; // Core logs go to stdout only, not editor UI

			std::shared_ptr<LogDispatchState> state = weakLogDispatchState.lock();
			if (!state) return;

			// try_lock to avoid blocking worker threads; entries that miss the lock go to a per-thread buffer and are drained on the next successful lock (entries from threads that never log again are lost, acceptable).
			thread_local std::vector<LogEntry> tlBuf;

			// Stamp at receipt (emit time) so ordering survives the buffered/deferred path.
			LogEntry record{ entry.Message, entry.Level };
			record.SourceFilePath = entry.SourceFile;
			record.SourceIdentity = entry.SourceFile;
			record.SourceLine = entry.SourceLine;
			record.Timestamp = FormatLogClockTimestamp();

			if (state->Mutex.try_lock()) {
				std::lock_guard<std::mutex> guard(state->Mutex, std::adopt_lock);
				if (!tlBuf.empty()) {
					state->PendingEntries.insert(state->PendingEntries.end(),
						std::make_move_iterator(tlBuf.begin()),
						std::make_move_iterator(tlBuf.end()));
					tlBuf.clear();
				}
				state->PendingEntries.push_back(std::move(record));
			}
			else {
				tlBuf.push_back(std::move(record));
			}
			});
	}

	void ImGuiEditorLayer::OnDetach(Application& app) {
		if (auto* settings = EditorDockLayout::WindowSettings::Get()) settings->ClearCallbacks();
		GameLocalization::SetPreviewLanguage({});
		m_SceneOpenLifetime.reset();
		if (m_SceneOpenOperation != 0) SceneManager::CancelSceneLoad(m_SceneOpenOperation);
		m_SceneOpenOperation = 0;
		m_SceneOpenFallbackPath.clear();
		AsyncTexturePreviews::Get().Shutdown();
		PropertyDrawer::ClearTransientState();
		(void)app;
		ClearUndoHistory();
		m_UndoHistory.DiscardSuspended();
		// Drain the async workers first — their lambdas write member state (the progress mutexes) that
		// the imminent destructor would otherwise free out from under a still-running thread. The future
		// destructors already block for the same duration; this just joins while the members are alive.
		Process::CancelAll();
		auto joinWorker = [](std::future<void>& worker) {
			if (!worker.valid()) return;
			while (worker.wait_for(std::chrono::milliseconds(25)) != std::future_status::ready) {
				Process::CancelAll();
			}
			try { worker.get(); } catch (...) {}
		};
		joinWorker(m_ReserializeFuture);
		joinWorker(m_BuildFuture);
		// MUST precede WebGPU teardown: Texture2D destructor touches the device.
		m_Splash.Shutdown();
		m_ProfilerPanel.Shutdown();
		m_StringTablePanel.Shutdown();
		m_ScriptWindows.Shutdown();
		m_SpriteEditorPanel.Shutdown();
		m_EditorPreferencesPanel.Shutdown();
		ApplicationEditorAccess::SetGameInputEnabled(true);
		Gizmo::SetShowInRuntime(true);
		if (m_LogSubscriptionId.value != 0) {
			Log::OnLog.Remove(m_LogSubscriptionId);
			m_LogSubscriptionId = EventId{};
		}
		m_AssetWatcher.Stop();
		m_ContentWatcher.Stop();
		m_LocalizationWatcher.Stop();
		m_ProjectFileWatcher.Stop();
		m_AssetWatcherRoot.clear();
		m_LogDispatchState.reset();
		// Reset before further teardown: destructor unsubscribes from Log::OnLog, which must run while the layer is still live.
		m_GameViewLogOverlay.reset();
		ClearPreviewTextureCache();

		// Reverse construction order: panels → icons → FBOs.
		m_AssetPreview3D.Close();
		m_AssetBrowser.Shutdown();
		m_PackageManagerPanel.Shutdown();
		m_PackageManager.Shutdown();
		ExternalEditor::JoinPendingLaunchThreads();
		ReferencePicker::Shutdown();
		ScriptComponentInspector::Shutdown();
		EditorIcons::Shutdown();
		Icons::Shutdown();
		m_EditorViewFBO.Destroy();
		m_GameViewFBO.Destroy();
	}

	void ImGuiEditorLayer::OnEvent(Application& /*app*/, FunkeEvent& event) {
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowFocusEvent>([this](WindowFocusEvent&) {
			m_AssetBrowser.RequestRefresh();
			return false;
			});
		dispatcher.Dispatch<WindowLostFocusEvent>([this](WindowLostFocusEvent&) {
			// Coming back to the editor must not grab the mouse; a click into the Game View takes it again.
			if (Application::GetIsPlaying()) m_GameCursorReleased = true;
			return false;
			});

		// Scene swap: must clear all editor entity handles (selection, order cache, drag state) before any consumer sees the new world, or stale handles reach the hierarchy clipper and cause asserts.
		auto resetEditorEntityState = [this]() {
			m_SelectedEntity = entt::null;
			m_SelectedEntities.clear();
			m_SelectedEntitySet.clear();
			m_PressedEntity = entt::null;
			m_RenamingEntity = entt::null;
			m_EntityRenameFrameCounter = 0;
			GetHierarchyState().EntityOrder.clear();
			GetHierarchyState().VisibleEntityOrder.clear();
			GetHierarchyState().VisibleEntityDepths.clear();
			GetHierarchyState().Dirty = true;
			GetHierarchyState().CollapsedEntities.clear();
			m_CutEntities.clear();
			m_LastInteractedSceneName.clear();
			++m_SelectionVersion;
		};
		dispatcher.Dispatch<ScenePreStopEvent>([&resetEditorEntityState](ScenePreStopEvent&) {
			resetEditorEntityState();
			return false;
			});
		dispatcher.Dispatch<ScenePostStartEvent>([&resetEditorEntityState](ScenePostStartEvent&) {
			// Belt-and-braces: additive loads skip PreStop, so PostStart must also clear.
			resetEditorEntityState();
			return false;
			});
	}

	void ImGuiEditorLayer::OnUpdate(Application& app, float dt) {
		if (m_SceneOpenOperation != 0 || !m_SceneOpenFallbackPath.empty()) { DrainPendingLogEntries(); return; }
		// As in Unity, the game owns the mouse (lock/hide, custom cursor) only while it plays in a focused Game View.
		// Escape or switching apps hands the plain OS cursor back to the editor without touching what the game asked
		// for; focusing or clicking into the Game View applies the game's settings again.
		// Uses last frame's hover/focus — a 1-frame settle is imperceptible.
		if (Window* win = app.GetWindow()) {
			ImGuiIO& io = ImGui::GetIO();
			const bool playing = Application::GetIsPlaying();
			// Only a running game may hold the mouse; anything left locked outside play mode would trap the editor.
			if (!playing && win->GetCursorMode() != 0) win->SetCursorMode(0);
			const int requestedMode = win->GetCursorMode();
			const bool gameViewFocusGained = m_IsGameViewFocused && !m_WasGameViewFocused;
			// A fresh lock from the game (e.g. closing its pause menu with Escape) takes the mouse back too.
			const bool freshLock = m_IsGameViewFocused && requestedMode != 0 && m_CursorModeLastUpdate == 0;
			if (!playing || gameViewFocusGained || m_GameViewCanvasClicked || freshLock)
				m_GameCursorReleased = false;
			else if (requestedMode != 0 && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
				m_GameCursorReleased = true;
			m_GameViewCanvasClicked = false;
			m_WasGameViewFocused = m_IsGameViewFocused;
			m_CursorModeLastUpdate = requestedMode;
			win->SetCursorModeSuspended(!playing || !m_IsGameViewFocused || m_GameCursorReleased);

			// A locked/hidden cursor is parked on the Game View's centre (Window::GetCursorLockPoint); ImGui's pointer is
			// held there too, so clicks while playing reach the Game View and no other panel.
			const bool captured = win->GetAppliedCursorMode() != 0;
			if (captured) {
				Vec2 lockPoint = win->GetCursorLockPoint();
				if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
					const Vec2Int windowPosition = win->GetPosition();
					lockPoint.x += static_cast<float>(windowPosition.x);
					lockPoint.y += static_cast<float>(windowPosition.y);
				}
				io.AddMousePosEvent(lockPoint.x, lockPoint.y);
			}
			const bool customCursorHere = win->HasGameCursor() && !m_GameCursorReleased
				&& (m_IsGameViewHovered || m_IsGameViewFocused);
			// Without NoMouseCursorChange the SDL3 backend calls SDL_ShowCursor every frame and undoes a hidden cursor.
			if (customCursorHere || captured)
				io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
			else
				io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
			if (win->HasGameCursor())
				win->SetGameCursorEnabled(customCursorHere);
		}

		DrainPendingLogEntries();
		RunAutoSaveTick(app, dt);
		// Held back until the startup prompt is answered so it can't overwrite copies awaiting recovery.
		if (m_RecoveryChecked && m_PendingRecovery.empty()) {
			constexpr float k_RecoveryIntervalSeconds = 30.0f;
			m_RecoveryAccumulator += dt;
			if (m_RecoveryAccumulator >= k_RecoveryIntervalSeconds) {
				m_RecoveryAccumulator = 0.0f;
				SceneRecovery::SnapshotDirtyScenes();
			}
		}
		// MUST precede RunPrefabAutoSaveTick: if the prefab file was deleted, exit edit mode first so auto-save doesn't recreate it at a stale path.
		if (m_PrefabEditScene && !m_PrefabEditPath.empty() && !File::Exists(m_PrefabEditPath)) {
			ClosePrefabEditing(false);
		}
		RunPrefabAutoSaveTick();

		if (FunkeProject* project = ProjectManager::GetCurrentProject()) {
			if (!project->AssetsDirectory.empty() && project->AssetsDirectory != m_AssetWatcherRoot) {
				ClearPreviewTextureCache();
				m_SpriteEditorPanel.Shutdown();
				m_PendingSpriteEditorPath.clear();
				m_ShowSpriteEditor = false;
				m_AssetWatcherRoot = project->AssetsDirectory;
				m_AssetWatcher.Watch(
					std::vector<std::string>{ m_AssetWatcherRoot },
					// .meta too: a GUID read while its .meta was mid-write stays "missing" until the registry is marked dirty again.
					std::vector<std::string>{ ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".meta" },
					[this](const std::vector<std::string>& changedPaths) {
						if (m_AssetBrowser.IsImportingAssets()
							|| m_AssetBrowser.IsDeleting()
							|| m_AssetBrowser.ConsumeAssetWatcherImportRefreshSuppression()) {
							return;
						}
						std::unordered_map<std::string, bool> textures;
						bool registryChanged = false;
						for (const std::string& changed : changedPaths) {
							auto path = Path::FromUtf8(changed);
							const bool metadata = ToLowerCopy(Path::ToUtf8(path.extension())) == ".meta";
							if (metadata) path.replace_extension();
							registryChanged |= AssetRegistry::RefreshAssetPathIncremental(Path::ToUtf8(path), metadata);
							if (ThumbnailCache::GetAssetType(Path::ToUtf8(path.extension())) != AssetType::Image) continue;
							textures[Path::ToUtf8(path)] |= !metadata;
						}

						size_t reloaded = 0;
						for (const auto& [path, pixelsChanged] : textures) {
							if (pixelsChanged) reloaded += TextureManager::ReloadTexturePath(path);
							TextureManager::ApplyMetaSamplerToLoaded(path);
							m_AssetBrowser.InvalidateThumbnail(path);
							InvalidatePreviewTexture(Path::FromUtf8(path));
							const std::string& openSprite = m_SpriteEditorPanel.GetOpenAssetPath();
							std::error_code ec;
							if (!openSprite.empty() && (Path::FromUtf8(openSprite).lexically_normal() == Path::FromUtf8(path).lexically_normal()
								|| std::filesystem::equivalent(Path::FromUtf8(openSprite), Path::FromUtf8(path), ec))) {
								m_SpriteEditorPanel.RefreshTexture();
							}
						}
						if (!textures.empty()) {
							SkyAmbient::ClearCache();
							NotifySpriteSliceEpochBumped();
						}
						const size_t recovered = TextureManager::ResolveMissingSceneTextures();
						if (registryChanged || recovered != 0) PrefabTemplateCache::Get().InvalidateAll();
						m_AssetBrowser.RequestRefresh();
						// Prefab previews can embed changed textures; their dependency list is not tracked yet.
						m_AssetBrowser.InvalidateRenderedThumbnails();
						if (reloaded > 0) {
							FNK_INFO_TAG("Editor", "Hot-reloaded {} texture(s)", reloaded);
						}
					});
				m_ContentWatcher.Watch(
					std::vector<std::string>{ m_AssetWatcherRoot },
					std::vector<std::string>{ ".scene", ".prefab", ".wav", ".mp3", ".ogg", ".flac", ".ttf", ".otf", ".dataasset", ".material", ".obj", ".mtl", ".gltf", ".glb", ".bin", ".anim", ".animctrl" },
					[this](const std::vector<std::string>& changedPaths) {
						ReloadExternallyChangedContent();
						for (const std::string& path : changedPaths) {
							const std::string extension = ToLowerCopy(Path::ToUtf8(Path::FromUtf8(path).extension()));
							if (extension == ".material" || IsMeshFileExtension(extension) || extension == ".bin"
								|| extension == ".anim" || extension == ".animctrl") {
								// Inspector saves update the live cache before the watcher sees the file.
								m_AssetBrowser.InvalidateRenderedThumbnails();
								break;
							}
						}
					});
				// The whole Assets tree: the Localization folder may not exist yet. Reload is a no-op when the tables didn't change.
				m_LocalizationWatcher.Watch(
					std::vector<std::string>{ m_AssetWatcherRoot },
					std::vector<std::string>{ ".json" },
					[]() { GameLocalization::Reload(); });
				m_ProjectFileWatcher.Watch(
					std::vector<std::string>{ project->ProjectFilePath, project->GetBuildProfilesDirectory() },
					std::vector<std::string>{ "project.json", ".build" },
					[this]() { m_ExternalProjectCheckPending = true; },
					/*recursive=*/false);
			}
			if (!m_AssetBrowser.IsImportingAssets() && !m_AssetBrowser.IsDeleting()) {
				m_AssetWatcher.Poll(0.25f);
				m_ContentWatcher.Poll(0.25f);
			}
			m_LocalizationWatcher.Poll(0.25f);
			m_ProjectFileWatcher.Poll(0.25f);
			const bool canApplyExternalChanges = !Application::GetIsPlaying()
				&& m_BuildState == 0 && m_ReserializeState == 0;
			if (m_ExternalSceneCheckPending && canApplyExternalChanges) {
				m_ExternalSceneCheckPending = false;
				CheckExternalSceneChanges();
			}
			if (m_ExternalPrefabCheckPending && canApplyExternalChanges) {
				m_ExternalPrefabCheckPending = false;
				CheckExternalPrefabChanges();
			}
			if (m_ExternalProjectCheckPending && canApplyExternalChanges) {
				m_ExternalProjectCheckPending = false;
				CheckExternalProjectChanges();
			}
		}
		else if (!m_AssetWatcherRoot.empty()) {
			ClearPreviewTextureCache();
			m_SpriteEditorPanel.Shutdown();
			m_PendingSpriteEditorPath.clear();
			m_ShowSpriteEditor = false;
			m_AssetBrowser.InvalidateAllThumbnails();
			m_AssetWatcher.Stop();
			m_ContentWatcher.Stop();
			m_LocalizationWatcher.Stop();
			m_ProjectFileWatcher.Stop();
			m_AssetWatcherRoot.clear();
			m_ExternalSceneConflicts.clear();
			m_PendingPrefabRefreshes.clear();
		}

		// Script-driven Application.Quit() in editor routes through the same stop path as the Stop button.
		if (Application::GetIsPlaying() && ApplicationEditorAccess::ConsumeQuitStopPlayRequest()) {
			RequestPlayModeStop();
		}
		ProcessPendingPlayModeStop();
		// Click-to-edit-frame latency: the stop itself plus the first edit-mode frame (texture/pipeline warm-up).
		if (m_PlayModeStopFramesToMeasure > 0 && --m_PlayModeStopFramesToMeasure == 0) {
			FNK_INFO_TAG("PlayMode", "Stop to second edit frame: {:.1f} ms", std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - m_PlayModeStopRequestTime).count());
		}

		Scene* activeScene = app.GetSceneManager() ? SceneManager::GetActiveScene() : nullptr;
		if (!activeScene) {
			m_UndoHistory.Deactivate(false);
			ShowDiscardedHistories(m_UndoHistory.Retain({}));
			ClearUndoHistory();
			return;
		}

		// CRITICAL: validate against GetContextScene(), not activeScene — in prefab-edit mode the selection lives in the detached prefab registry; using activeScene would clear the selection every frame.
		Scene* selectionContextScene = GetContextScene();
		if (!selectionContextScene) selectionContextScene = activeScene;
		Scene& selectionScene = *selectionContextScene;

		UndoStack& history = GetUndoStack(selectionScene);
		ShowDiscardedHistories(m_UndoHistory.Retain(SceneManager::GetLoadedScenes()));
		InspectorUndoRecorder::Flush(history);

		// Advance the undo/redo toast fade (fade-in 0.15 + hold 1.3 + fade-out 0.5).
		if (m_UndoToastActive) {
			m_UndoToastElapsed += dt;
			if (m_UndoToastElapsed >= 1.95f) m_UndoToastActive = false;
		}

		if (m_SelectedEntity != entt::null && !selectionScene.IsValid(m_SelectedEntity)) {
			m_SelectedEntity = entt::null;
		}
		if (m_PressedEntity != entt::null && !selectionScene.IsValid(m_PressedEntity)) {
			m_PressedEntity = entt::null;
		}
		if (!m_SelectedEntities.empty()) {
			auto isStale = [&selectionScene](EntityHandle h) {
				return h == entt::null || !selectionScene.IsValid(h);
			};
			const std::size_t before = m_SelectedEntities.size();
			m_SelectedEntities.erase(
				std::remove_if(m_SelectedEntities.begin(), m_SelectedEntities.end(), isStale),
				m_SelectedEntities.end());
			if (m_SelectedEntities.size() != before) {
				RebuildSelectionSet();
			}
		}
		UpdateEditorCameraFocus(dt);
		const bool hasShortcutFocus = HasEntityShortcutFocus();
		const ImGuiIO& io = ImGui::GetIO();
		// Shortcut handlers use the context scene so they work on prefab entities in prefab-edit mode.
		Scene& shortcutScene = selectionScene;
		const bool hasEntitySelection = !GetSelectedEntities(shortcutScene).empty();

		if (!io.WantTextInput && !ImGui::IsAnyItemActive()
			&& ImGui::IsKeyChordPressed(ImGuiKey_F) && hasShortcutFocus && hasEntitySelection) {
			FocusSelectedEntity(shortcutScene);
		}

		if (!hasShortcutFocus) {
			return;
		}
		if (m_RenamingEntity != entt::null || io.WantTextInput || ImGui::IsAnyItemActive()) {
			return;
		}

		if (io.KeyShift && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper
			&& ImGui::IsKeyPressed(ImGuiKey_C, false)) {
			FinishCreatedEditorEntity(shortcutScene, Entity::Null, shortcutScene.CreateEmptyEntity());
			return;
		}

		// Arrow-key hierarchy navigation follows the currently rendered row list.
		const auto& visibleEntityOrder = GetHierarchyState().VisibleEntityOrder;
		if (m_IsEntitiesPanelFocused && hasEntitySelection && !visibleEntityOrder.empty()
			&& !io.KeyCtrl && !io.KeySuper && !io.KeyAlt) {
			const int direction = ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) ? 1
				: (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) ? -1 : 0);
			if (direction != 0) {
				int currentIndex = -1;
				for (int i = 0; i < static_cast<int>(visibleEntityOrder.size()); ++i) {
					if (visibleEntityOrder[static_cast<std::size_t>(i)] == m_SelectedEntity) {
						currentIndex = i;
						break;
					}
				}

				if (currentIndex < 0) {
					for (EntityHandle selected : GetSelectedEntities(shortcutScene)) {
						for (int i = 0; i < static_cast<int>(visibleEntityOrder.size()); ++i) {
							if (visibleEntityOrder[static_cast<std::size_t>(i)] == selected) {
								currentIndex = i;
								break;
							}
						}
						if (currentIndex >= 0) break;
					}
				}

				if (currentIndex >= 0) {
					const int targetIndex = std::clamp(
						currentIndex + direction,
						0,
						static_cast<int>(visibleEntityOrder.size()) - 1);
					const EntityHandle target = visibleEntityOrder[static_cast<std::size_t>(targetIndex)];
					if (target != entt::null && shortcutScene.IsValid(target)) {
						SetSingleEntitySelection(target, targetIndex);
					}
				}
			}
		}

		// Use io.KeyCtrl + ImGui::IsKeyPressed (not raw Input): covers right Ctrl/Cmd and prevents auto-repeat.
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && hasEntitySelection) {
			CopySelectedEntities(shortcutScene);
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false) && hasEntitySelection) {
			CutSelectedEntities(shortcutScene);
			return;
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
			PasteEntities(shortcutScene);
		}
		// Esc cancels a pending Cut: entities stay alive and un-dim. The
		// clipboard JSON itself is left intact so Ctrl+V still pastes a
		// copy, matching the behaviour of typical OS file managers.
		if (!m_CutEntities.empty() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)
			&& m_RenamingEntity == entt::null) {
			m_CutEntities.clear();
		}
		if (!hasEntitySelection) {
			return;
		}
		if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
			DuplicateSelectedEntity(shortcutScene);
		}
		if (EditorShortcuts::IsDeletePressed()) {
			RequestDeleteSelectedEntity(shortcutScene);
		}
		if (EditorShortcuts::IsRenamePressed()) {
			BeginRenameSelectedEntity(shortcutScene);
		}
	}
}
