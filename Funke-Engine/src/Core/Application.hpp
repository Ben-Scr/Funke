#pragma once
#include "Core/ApplicationConfig.hpp"
#include "Core/Export.hpp"
#include "Core/Layer.hpp"
#include "Core/LayerStack.hpp"
#include "Events/FunkeEvent.hpp"
#include "Events/EventBus.hpp"

#include "Input.hpp"
#include "Time.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if !FUNKE_WITH_APPLICATION
#error "Funke::Application is part of the full runtime surface. Enable the application module profile or include Funke/Core.hpp for standalone core usage."
#endif

namespace Funke {
	class GuiRenderer;
	class GizmoRenderer2D;
	class IPhysicsEngine;
	class IRenderer;
	class PhysicsSystem2D;
	class Renderer2D;
	class Renderer3D;
	class Scene;
	class SceneManager;
	class Window;

	class FUNKE_API Application {
		friend class Window;
		friend class SceneManager;
		friend class ApplicationEditorAccess;
		friend class ScriptBindings;

		using Clock = std::chrono::steady_clock;
		using DurationChrono = Clock::duration;

	public:
		struct CommandLineArgs {
			int Count = 0;
			char** Values = nullptr;

			const char* operator[](int index) const {
				if (!Values || index < 0 || index >= Count) {
					return nullptr;
				}

				return Values[index];
			}
		};

		Application();
		virtual ~Application();


		// Blocking main loop (desktop, mobile).
		int Run();

		// Stepped main loop for hosts that own the frame loop (the browser's requestAnimationFrame).
		// BeginRun false: startup failed. RunFrame false: the loop wants to stop; call FinishRunCycle,
		// which returns true when a reload started a new cycle and frames should continue.
		bool BeginRun();
		bool RunFrame();
		bool FinishRunCycle();
		int GetRunExitCode() const { return m_RunLoop.ExitCode != 0 ? m_RunLoop.ExitCode : m_ExitCode; }

		virtual ApplicationConfig GetConfiguration() const { return {}; }
		virtual void ConfigureScenes() {}
		virtual void ConfigureLayers() {}
		virtual void Start() {}
		virtual void Update() {}
		virtual void FixedUpdate() {}
		virtual void OnPaused() {}
		virtual void OnQuit() {}
		// The main loop caught an exception and is about to shut down; last chance to preserve user data.
		virtual void OnUnhandledFrameException(const std::string& message) { (void)message; }

		static void SetIsPlaying(bool enabled) { if (s_Instance) s_Instance->m_IsPlaying = enabled; }
		static bool GetIsPlaying() { if (s_Instance) return s_Instance->m_IsPlaying; return false; }
		// How far the frame is into the next fixed step (0..1): blend factor for rendering fixed-step state.
		static float GetFixedStepAlpha();

		void SetName(const std::string& s) { m_Name = s; }
		static void SetTargetFramerate(float framerate) { Time::SetTargetFramerate(framerate); }
		static void SetForceSingleInstance(bool value) { if (s_Instance) s_Instance->m_ForceSingleInstance = value; }
		static void SetRunInBackground(bool value) {
			if (s_Instance) {
				s_Instance->m_RunInBackground = value;
				s_Instance->RefreshBackgroundPauseState();
			}
		}

		const std::string& GetName() { return m_Name; }
		static float GetTargetFramerate() { return s_Instance ? (s_Instance->IsEnginePaused() ? k_PausedTargetFrameRate : Time::GetTargetFramerate()) : 0.0f; }
		static bool GetForceSingleInstance() { return s_Instance ? s_Instance->m_ForceSingleInstance : false; }
		static bool GetRunInBackground() { return s_Instance ? s_Instance->m_RunInBackground : false; }
		static Window* GetWindow() { return s_Instance ? s_Instance->m_Window.get() : nullptr; }
		static CommandLineArgs GetCommandLineArgs() { return s_CommandLineArgs; }
		static void SetCommandLineArgs(int argc, char** argv) { s_CommandLineArgs = { argc, argv }; }


		IRenderer* GetRenderer() { return m_Renderer2D.get(); }
		Renderer2D* GetRenderer2D();
#if FUNKE_WITH_3D
		Renderer3D* GetRenderer3D() { return m_Renderer3D.get(); }
#else
		Renderer3D* GetRenderer3D() { return nullptr; }
#endif
		GuiRenderer* GetGuiRenderer() { return m_GuiRenderer.get(); }
		Input& GetInput() { return m_Input; }
		Time& GetTime() { return m_Time; }
		const Time& GetTime() const { return m_Time; }

		static Application* GetInstance() { return s_Instance; }

		GraphicsSettings GetRequestedGraphicsSettings() const noexcept { return m_RequestedGraphicsSettings; }
		GraphicsSettings GetEffectiveGraphicsSettings() const noexcept { return m_EffectiveGraphicsSettings; }
		GraphicsApplyStatus ApplyGraphicsSettings(const GraphicsSettings& settings);
		GraphicsApplyStatus ResetGraphicsSettingsToDefaults();

		Scene* GetActiveScene();
		SceneManager* GetSceneManager() { return m_SceneManager.get(); }
		const SceneManager* GetSceneManager() const { return m_SceneManager.get(); }

		static void Quit();
		// Routes scripted Application.Quit() inside the editor to stop-play rather than closing the editor window.
		static void RequestEditorStopPlay() {
			if (s_Instance) s_Instance->m_EditorStopPlayRequested = true;
		}

		// MUST be called by splash overlay: deferred scene load begins only once SignalSplashDetached fires.
		static void SignalSplashAttached() {
			if (s_Instance) s_Instance->m_SplashLayerActive = true;
		}
		static void SignalSplashDetached() {
			if (s_Instance) s_Instance->m_SplashLayerActive = false;
		}
		// Editor/Launcher splash calls this once it has been presented, so the
		// deferred startup load runs *behind* the still-visible splash instead of
		// after it detaches (the runtime splash leaves this unset → legacy order).
		static void RequestStartupLoad() {
			if (s_Instance) s_Instance->m_StartupLoadRequested = true;
		}
		// True once the deferred startup load has finished (or there was nothing
		// to defer). The splash polls this to start fading out.
		static bool IsStartupLoadComplete() {
			return s_Instance ? s_Instance->m_StartupLoadComplete : true;
		}
		static void Pause(bool paused) { if (s_Instance) s_Instance->m_IsPaused = paused; }
		static void Reload() {
			if (s_Instance) {
				s_Instance->m_ShouldQuit = true;
				s_Instance->m_CanReload = true;
			}
		}
		static void Restart() {
			if (s_Instance) {
				s_Instance->m_ShouldQuit = true;
				s_Instance->m_CanReload = true;
				s_Instance->m_RestartWindow = true;
			}
		}
		static bool IsPaused() { return s_Instance ? s_Instance->IsEnginePaused() : false; }
		static bool IsShuttingDown() { return s_Instance ? s_Instance->m_IsShuttingDown : false; }
		static bool IsMainThread() { return !s_Instance || s_Instance->m_MainThreadId == std::this_thread::get_id(); }

		/// True for editor host, false for standalone runtime / launcher.
		/// Backed by a process-global static so editor entry points can mark the host
		/// before the Application exists — ProjectManager::SetCurrentProject runs from
		/// CreateApplication, before construction, and gates editor-only systems on this.
		static bool IsEditor() { return s_Instance ? s_Instance->m_IsEditorHost : s_IsEditorHostProcess; }
		static void SetEditorHost(bool isEditor) {
			s_IsEditorHostProcess = isEditor;
			if (s_Instance) { s_Instance->m_IsEditorHost = isEditor; }
		}

		/// Process exit code Run() returns after a graceful quit; a crash still returns 1.
		static void SetExitCode(int exitCode) { if (s_Instance) s_Instance->m_ExitCode = exitCode; }

		/// Signals a quit request that can be intercepted (e.g. to show a save dialog).
		static void RequestQuit();
		static bool IsQuitRequested();
		static void CancelQuit();
		static void ConfirmQuit();
		void RenderOnceForRefresh();

		template<typename TLayer, typename... Args>
		TLayer& PushLayer(Args&&... args) {
			static_assert(std::is_base_of_v<Layer, TLayer>, "TLayer must derive from Layer");

			auto layer = std::make_unique<TLayer>(std::forward<Args>(args)...);
			TLayer& layerRef = *layer;
			m_LayerStack.PushLayer(std::move(layer));
			layerRef.OnAttach(*this);
			return layerRef;
		}

		template<typename TLayer, typename... Args>
		TLayer& PushOverlay(Args&&... args) {
			static_assert(std::is_base_of_v<Layer, TLayer>, "TLayer must derive from Layer");

			auto layer = std::make_unique<TLayer>(std::forward<Args>(args)...);
			TLayer& layerRef = *layer;
			m_LayerStack.PushOverlay(std::move(layer));
			layerRef.OnAttach(*this);
			return layerRef;
		}

		// Dispatch-safe: erase is deferred while a dispatch is in flight so the caller's `this` stays valid for the rest of OnUpdate.
		bool PopOverlay(Layer* layer) {
			// Only detach an overlay that is actually in the stack (and not already popped): detaching a regular
			// layer here left it detached-but-live, and Shutdown detached it a second time.
			if (!layer || !m_LayerStack.ContainsOverlay(layer) || m_LayerStack.IsPendingPop(layer)) return false;
			layer->OnDetach(*this);
			return m_LayerStack.PopOverlay(layer);
		}

		template<typename TEvent, typename F>
		EventId SubscribeEvent(F&& callback) {
			return m_EventBus.Subscribe<TEvent>(std::forward<F>(callback));
		}

		template<typename TEvent, typename F>
		EventBus::Subscription SubscribeEventScoped(F&& callback) {
			return m_EventBus.SubscribeScoped<TEvent>(std::forward<F>(callback));
		}

		bool UnsubscribeEvent(EventId id) { return m_EventBus.Unsubscribe(id); }

		std::vector<std::string> TakePendingFileDrops() {
			std::vector<std::string> paths = std::move(m_PendingFileDrops);
			m_PendingFileDrops.clear();
			return paths;
		}

	private:
		// Returns by value: shared thread_local buffer was cleared by inner nested dispatches, invalidating the outer iterator.
		using LayerSnapshot = std::vector<Layer*>;

		LayerSnapshot SnapshotLayerOrder() const {
			LayerSnapshot snapshot;
			snapshot.reserve(m_LayerStack.Size());
			for (size_t i = 0; i < m_LayerStack.Size(); ++i) {
				Layer* layer = const_cast<LayerStack&>(m_LayerStack).At(i);
				if (layer && !m_LayerStack.IsPendingPop(layer)) {
					snapshot.push_back(layer);
				}
			}
			return snapshot;
		}

		LayerSnapshot SnapshotLayerOrderReversed() const {
			LayerSnapshot snapshot;
			snapshot.reserve(m_LayerStack.Size());
			for (size_t i = m_LayerStack.Size(); i > 0; --i) {
				Layer* layer = const_cast<LayerStack&>(m_LayerStack).At(i - 1);
				if (layer && !m_LayerStack.IsPendingPop(layer)) {
					snapshot.push_back(layer);
				}
			}
			return snapshot;
		}

		struct LayerDispatchGuard {
			explicit LayerDispatchGuard(LayerStack& stack) : Stack(stack) { Stack.BeginDispatch(); }
			~LayerDispatchGuard() { Stack.EndDispatch(); }
			LayerDispatchGuard(const LayerDispatchGuard&) = delete;
			LayerDispatchGuard& operator=(const LayerDispatchGuard&) = delete;
			LayerStack& Stack;
		};

	private:
		std::unique_ptr<Window> m_Window;
		std::unique_ptr<IRenderer> m_Renderer2D;
#if FUNKE_WITH_3D
		std::unique_ptr<Renderer3D> m_Renderer3D;
#endif
		std::unique_ptr<GuiRenderer> m_GuiRenderer;
		std::unique_ptr<GizmoRenderer2D> m_GizmoRenderer2D;
		std::unique_ptr<IPhysicsEngine> m_PhysicsSystem2D;
		std::unique_ptr<SceneManager> m_SceneManager;
		Input m_Input;
		Time m_Time;
		ApplicationConfig m_Configuration;
		GraphicsSettings m_DefaultGraphicsSettings{};
		GraphicsSettings m_RequestedGraphicsSettings{};
		GraphicsSettings m_EffectiveGraphicsSettings{};
		bool m_GraphicsSettingsPending = false;
		bool m_HasInitialized = false;
		bool m_HasInitializedOnce = false;

	    std::string m_Name;

		bool m_ForceSingleInstance = false;
		bool m_RunInBackground = false;
		bool m_ShouldQuit = false;
		int m_ExitCode = 0;

		struct RunLoopState {
			bool CiSmokeTest = false;
			float QuitAfterSeconds = -1.0f;
			int ExitCode = 0;
			int SmokeFramesRemaining = 3;
			bool SmokeQuitRequested = false;
			bool HasQuitAfterStart = false;
			std::chrono::steady_clock::time_point QuitAfterStart{};
			bool QuitAfterRequested = false;
			std::chrono::steady_clock::time_point FrameDeadline{};
		};
		RunLoopState m_RunLoop;
		bool StartRunCycle();

		bool m_CanReload = false;
		bool m_RestartWindow = false;
		bool m_IsEditorHost = false;
		bool m_IsPaused = false;
		std::atomic<bool> m_IsBackgroundPaused{ false };
		std::atomic<bool> m_IsMinimized{ false };
		std::atomic<bool> m_WindowHasFocus{ true };
		bool m_IsPlaying = true;
		bool m_IsGameplayPaused = false;
		bool m_IsScriptInputEnabled = true;
		bool m_EditorStopPlayRequested = false; // polled by editor pre-render to route quit-from-script through stop-play
		// Set when splash is active; deferred startup-scene trio runs only once splash pops (deferred-load gate).
		bool m_DeferredStartupScenes = false;
		bool m_SplashLayerActive = false;
		// Editor/Launcher splash opt-in: load behind the visible splash instead of after it detaches.
		bool m_StartupLoadRequested = false;
		bool m_StartupLoadComplete = false;
		bool m_WasEnginePaused = false;
		bool m_IsRenderingFrame = false;
		bool m_IsRenderingRefresh = false;
		bool m_IsShuttingDown = false;
		bool m_QuitRequested = false;
		int m_QuitRequestFrame = -1;

		static const float k_PausedTargetFrameRate;

		static Application* s_Instance;
		static CommandLineArgs s_CommandLineArgs;
		// Process-global (not instance-bound): editor entry points set it before any instance exists.
		static bool s_IsEditorHostProcess;


		std::vector<std::string> m_PendingFileDrops;
		double m_FixedUpdateAccumulator;
		// VSync delta snapping (see SnapVsyncDelta): display refresh interval and the real time not yet handed out.
		float m_RefreshInterval = 0.0f;
		int m_RefreshQueryCountdown = 0;
		double m_DeltaSnapDebt = 0.0;
		float SnapVsyncDelta(float deltaTime);
		Clock::time_point m_LastFrameTime = Clock::now();
		// Stamped at end of frame; next frame reads it to compute the VSync bucket.
		Clock::time_point m_LastFrameEndTime{};
		LayerStack m_LayerStack;
		EventBus m_EventBus;
		std::thread::id m_MainThreadId;

		void Initialize();
		void Shutdown(bool invokeOnQuit = true, bool forceWindowShutdown = false);
		void NotifyUnhandledFrameException(const std::string& message);
		void DispatchEvent(FunkeEvent& event);
		void RefreshBackgroundPauseState();
		// Never report paused before the startup scene load has finished. That load
		// (and the splash that requests it) run inside BeginFrame's `if (!enginePaused)`
		// block, so pausing during startup — e.g. the editor launched unfocused, or with
		// "Run in background" off before the window gains focus — would deadlock: the scene
		// never loads, leaving a blank window with no active scene. Pause normally afterward.
		bool IsEnginePaused() const { return m_StartupLoadComplete && (m_IsPaused || m_IsBackgroundPaused || m_IsMinimized); }
		void CoreInput();
		void ResetTimePoints();
		void TryCompleteQuitRequest();
		// Drains m_DeferredStartupScenes when the splash has popped.
		// Called from the main loop right after the layer OnUpdate
		// dispatch (which is where the splash actually detaches itself).
		void TickDeferredStartupScenes();
		void BeginFrame(bool enginePaused);
		void BeginFixedFrame();
		void EndFixedFrame();
		void EndFrame(bool enginePaused);
		void RenderPipelineOnly();
		void CommitGraphicsSettings();
		static void SetWindowFocused(bool focused) {
			if (s_Instance) {
				s_Instance->m_WindowHasFocus = focused;
				s_Instance->RefreshBackgroundPauseState();
			}
		}

		// From the window minimize/restore events (not framebuffer resize): resize-to-(0,0) doesn't fire reliably on all platforms/WMs.
		static void SetWindowMinimized(bool minimized) {
			if (s_Instance) {
				s_Instance->m_IsMinimized = minimized;
			}
		}

		// Mobile app lifecycle. Android and iOS suspend the main thread right after the background event,
		// so pausing audio and raising OnApplicationPaused cannot wait for the next frame.
		static void EnterBackground();
		static void EnterForeground();
	};

	inline Window* GetWindow() {
		auto* app = Application::GetInstance();
		if (!app) return nullptr;
		return app->GetWindow();
	}

	// To be defined in client
	Application* CreateApplication();
}
