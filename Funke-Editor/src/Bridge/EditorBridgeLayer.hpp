#pragma once
#include "Collections/Ids.hpp"
#include "Collections/Vec2.hpp"
#include "Core/Layer.hpp"
#include "Core/Log.hpp"
#include "DevTools/EditorBridge.hpp"
#include "DevTools/RunAutomation.hpp"
#include "Serialization/Json.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Funke {
	class ImGuiEditorLayer;
	class ScriptSystem;

	// Serves `funke editor ...` (DevTools/EditorBridge.hpp): a user-only local endpoint, a discovery file and the
	// commands. Pushed right after ImGuiEditorLayer, so injected input reaches the game after the editor's own
	// shortcuts ran and never triggers them. Never moves, focuses or resizes a window and sends no OS input.
	class EditorBridgeLayer : public Layer {
	public:
		explicit EditorBridgeLayer(ImGuiEditorLayer& editor);
		~EditorBridgeLayer() override;

		void OnAttach(Application& app) override;
		void OnDetach(Application& app) override;
		void OnUpdate(Application& app, float dt) override;
		void OnPreRender(Application& app) override;
		// Only this runs while the engine is paused (minimized window); it wakes the engine for pending work.
		void OnEnginePaused();

	private:
		using Clock = std::chrono::steady_clock;

		struct LogLine {
			uint64_t Seq = 0;
			std::string Time;
			Log::Level Level = Log::Level::Info;
			bool Core = false;
			std::string Message;
		};

		struct LogBuffer {
			std::mutex Mutex;
			std::deque<LogLine> Lines;
			uint64_t NextSeq = 1;
		};

		enum class JobKind : uint8_t { Play, Stop, Recompile, Wait, Step, Input, Screenshot };

		struct Capture {
			std::string Path;
			int Width = 0;  // 0: the game's current surface
			int Height = 0;
			std::string At;
		};

		// A "press" injected this frame, released on the next one.
		struct Held {
			RunAutomation::Step::Kind Kind = RunAutomation::Step::Kind::Key;
			int Code = 0;
			Vec2 Position{ 0.0f };
			int64_t Finger = 0;
		};

		struct Job {
			JobKind Kind = JobKind::Wait;
			uint64_t Ticket = 0;
			std::string Id;
			Clock::time_point Started = Clock::now();
			// Wait / Step; Play continues as a Wait when these are set.
			double WaitSeconds = -1.0;
			int64_t WaitFrames = -1;
			double ElapsedSeconds = 0.0;
			int64_t ElapsedFrames = 0;
			bool GameTime = true;
			// Recompile
			uint64_t RebuildsAtStart = 0;
			bool WasPlaying = false;
			int IdleFrames = 0;
			bool Retried = false;
			// Input
			std::vector<RunAutomation::Step> Steps;
			std::vector<bool> StepDone;
			bool InputStarted = false;
			double InputClock = 0.0;
			int64_t InputFrame = 0;
			std::vector<Held> Releases;
			int64_t MouseFinger = -1;
			std::string ScreenshotDirectory;
			bool QuitRequested = false;
			bool RestoreFixedStep = false;
			Json::Value Commands = Json::Value::MakeArray();
			// Screenshot jobs and input screenshot steps
			std::vector<Capture> PendingCaptures;
			Json::Value Screenshots = Json::Value::MakeArray();
			std::string CaptureError;
		};

		void Dispatch(uint64_t ticket, const EditorBridge::Request& request);
		void UpdateJob();
		void RunInputFrame(Job& job);
		void RunInputStep(Job& job, const RunAutomation::Step& step);
		void InjectTouch(Job& job, int64_t finger, Vec2 normalized, int phase);
		void ReleaseHeld(Job& job, const Held& held);
		void ProcessCaptures(Job& job);
		bool IsInputComplete(const Job& job) const;

		void Reply(uint64_t ticket, const std::string& id, const Json::Value& result);
		void Fail(uint64_t ticket, const std::string& id, const std::string& message, bool busy = false);
		void FinishJob(const Json::Value& result);
		void FailJob(const std::string& message);
		// Lets go of held keys and fingers and undoes the job's fixed step.
		void EndJob();
		void EndPlaySession();
		// A minimized window pauses the engine; keeps it running while a request or job needs frames.
		void KeepAwake(bool needed);

		// A game surface in Play mode even while the Game View is hidden or the window minimized.
		void EnsureGameSurface();
		std::pair<int, int> GetSurfaceSize() const;
		Vec2 SurfaceToWindow(Vec2 normalized) const;

		Json::Value BuildStatus() const;
		Json::Value ReadLogs(const Json::Value& args) const;
		static ScriptSystem* GetActiveScriptSystem();

		void WriteDiscoveryFile();
		void RemoveDiscoveryFile();

		ImGuiEditorLayer& m_Editor;
		EditorBridge::Server m_Server;
		std::string m_Token;
		std::string m_DiscoveryFile;
		std::shared_ptr<LogBuffer> m_Logs;
		EventId m_LogSubscription{};
		std::optional<Job> m_Job;

		bool m_WasPlaying = false;
		// A bridge command touched this Play session, so the game keeps a surface while the Game View is hidden.
		bool m_SessionDriven = false;
		bool m_VirtualSurface = false;
		int m_SurfaceWidth = 0;
		int m_SurfaceHeight = 0;
		float m_SessionFixedStep = 0.0f;
		// Frames the game keeps reading input after an injection, whatever the Game View's focus.
		int m_InputGrantFrames = 0;
		bool m_KeptAwake = false;
		Clock::time_point m_LastAwakeFrame{};
	};

} // namespace Funke
