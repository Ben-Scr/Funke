#include <pch.hpp>
#include "Bridge/EditorBridgeLayer.hpp"

#include "Core/Application.hpp"
#include "Core/Engine.hpp"
#include "Core/Time.hpp"
#include "Core/Window.hpp"
#include "DevTools/DevConsole.hpp"
#include "DevTools/ScreenCapture.hpp"
#include "Editor/ApplicationEditorAccess.hpp"
#include "Project/ProjectManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/SceneManager.hpp"
#include "Scripting/ScriptSystem.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"
#include "Systems/ImGuiEditorLayer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace Funke {

	namespace {
		constexpr size_t k_MaxLogLines = 4000;
		// The virtual surface sits this far right and down of the window, out of reach of the real mouse.
		constexpr int k_VirtualSurfaceOrigin = 100000;
		constexpr int k_DefaultSurfaceWidth = 1280;
		constexpr int k_DefaultSurfaceHeight = 720;

		std::string ArgString(const Json::Value& args, std::string_view key) {
			const Json::Value* value = args.FindMember(key);
			if (!value || value->IsNull()) return {};
			return value->IsString() ? value->AsStringOr() : Json::Stringify(*value);
		}

		bool ArgBool(const Json::Value& args, std::string_view key, bool fallback = false) {
			const Json::Value* value = args.FindMember(key);
			return value && value->IsBool() ? value->AsBoolOr(fallback) : fallback;
		}

		double ArgNumber(const Json::Value& args, std::string_view key, double fallback) {
			const Json::Value* value = args.FindMember(key);
			return value && value->IsNumber() ? value->AsDoubleOr(fallback) : fallback;
		}

		std::string LowerCopy(std::string_view text) {
			std::string out(text);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		const char* LevelName(Log::Level level) {
			switch (level) {
			case Log::Level::Trace: return "trace";
			case Log::Level::Info: return "info";
			case Log::Level::Warn: return "warning";
			case Log::Level::Error: return "error";
			case Log::Level::Critical: return "critical";
			}
			return "info";
		}

		bool ParseLevel(std::string_view text, Log::Level& out) {
			const std::string lower = LowerCopy(text);
			if (lower == "trace") out = Log::Level::Trace;
			else if (lower == "info") out = Log::Level::Info;
			else if (lower == "warn" || lower == "warning") out = Log::Level::Warn;
			else if (lower == "error") out = Log::Level::Error;
			else if (lower == "critical" || lower == "fatal") out = Log::Level::Critical;
			else return false;
			return true;
		}

		std::string ClockTime() {
			const auto now = std::chrono::system_clock::now();
			const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
			const int millis = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
			std::tm local{};
#if defined(FNK_PLATFORM_WINDOWS)
			localtime_s(&local, &seconds);
#else
			localtime_r(&seconds, &local);
#endif
			char buffer[32];
			std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min, local.tm_sec, millis);
			return buffer;
		}

		int64_t UnixMillis() {
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		}

		Json::Value SizeJson(int width, int height) {
			Json::Value size = Json::Value::MakeObject();
			size.AddMember("width", width);
			size.AddMember("height", height);
			return size;
		}

		std::string ScreenshotFileName(std::string_view at) {
			std::string name = "at-" + std::string(at);
			for (char& c : name) {
				if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '.' && c != '_') c = '_';
			}
			return name + ".png";
		}
	}

	EditorBridgeLayer::EditorBridgeLayer(ImGuiEditorLayer& editor)
		: Layer("EditorBridge"), m_Editor(editor) {}

	EditorBridgeLayer::~EditorBridgeLayer() {
		m_Server.Stop();
	}

	void EditorBridgeLayer::OnAttach(Application& app) {
		(void)app;
		m_Logs = std::make_shared<LogBuffer>();
		std::weak_ptr<LogBuffer> weakLogs = m_Logs;
		m_LogSubscription = Log::OnLog.Add([weakLogs](const Log::Entry& entry) {
			std::shared_ptr<LogBuffer> logs = weakLogs.lock();
			if (!logs) return;
			LogLine line{ 0, ClockTime(), entry.Level, entry.Source == Log::Type::Core, entry.Message };
			std::lock_guard lock(logs->Mutex);
			line.Seq = logs->NextSeq++;
			logs->Lines.push_back(std::move(line));
			if (logs->Lines.size() > k_MaxLogLines) logs->Lines.pop_front();
		});

		if (const char* disabled = std::getenv("FUNKE_EDITOR_BRIDGE"); disabled && std::string_view(disabled) == "0") {
			FNK_CORE_INFO_TAG("EditorBridge", "Disabled by FUNKE_EDITOR_BRIDGE=0.");
			return;
		}
		m_Token = EditorBridge::GenerateToken();
		const std::string endpoint = EditorBridge::MakeEndpointName(EditorBridge::GetCurrentProcessId(), EditorBridge::GenerateToken());
		std::string error;
		if (!m_Server.Start(endpoint, m_Token, error)) {
			FNK_CORE_WARN_TAG("EditorBridge", "Not available, `funke editor` can't reach this editor: {}", error);
			return;
		}
		WriteDiscoveryFile();
		FNK_CORE_INFO_TAG("EditorBridge", "Listening for `funke editor` commands on {}", endpoint);
	}

	void EditorBridgeLayer::OnDetach(Application& app) {
		(void)app;
		if (m_Job) FailJob("the editor is closing");
		m_Server.Stop();
		RemoveDiscoveryFile();
		EndPlaySession();
		if (m_LogSubscription.value != 0) {
			Log::OnLog.Remove(m_LogSubscription);
			m_LogSubscription = EventId{};
		}
		m_Logs.reset();
		KeepAwake(false);
	}

	void EditorBridgeLayer::OnEnginePaused() {
		if (m_Job || m_Server.HasRequests()) KeepAwake(true);
	}

	void EditorBridgeLayer::KeepAwake(bool needed) {
		if (needed) {
			if (ApplicationEditorAccess::IsMinimizedPauseActive()) {
				ApplicationEditorAccess::SetMinimizedPauseActive(false);
				m_KeptAwake = true;
			}
			return;
		}
		if (!m_KeptAwake) return;
		m_KeptAwake = false;
		const Window* window = Application::GetWindow();
		if (window && window->IsMinimized()) ApplicationEditorAccess::SetMinimizedPauseActive(true);
	}

	void EditorBridgeLayer::OnUpdate(Application& app, float dt) {
		(void)app;
		(void)dt;
		const bool playing = Application::GetIsPlaying();
		if (m_WasPlaying && !playing) EndPlaySession();
		m_WasPlaying = playing;

		for (const uint64_t ticket : m_Server.TakeAbandoned()) {
			if (m_Job && m_Job->Ticket == ticket) FailJob("the client stopped waiting");
		}
		// The surface first: input steps map their positions through it.
		EnsureGameSurface();
		for (EditorBridge::Server::Incoming& incoming : m_Server.TakeRequests()) {
			// One client at a time: a new request means the previous client gave up waiting.
			if (m_Job) FailJob("superseded by a newer command");
			Dispatch(incoming.Ticket, incoming.Message);
			EnsureGameSurface();
		}
		UpdateJob();

		if (m_InputGrantFrames > 0) {
			// The Game View only hands input to a game it has focus on; injected input doesn't need that.
			ApplicationEditorAccess::SetGameInputEnabled(true);
			--m_InputGrantFrames;
		}
		KeepAwake(m_Job.has_value() || m_Server.HasRequests());
		if (m_KeptAwake) {
			// Nothing presents (and so nothing waits for vsync) while minimized; hold the loop near 60 fps.
			const Clock::time_point next = m_LastAwakeFrame + std::chrono::microseconds(16667);
			const Clock::time_point now = Clock::now();
			if (next > now && next - now < std::chrono::milliseconds(100)) std::this_thread::sleep_until(next);
			m_LastAwakeFrame = Clock::now();
		}
	}

	void EditorBridgeLayer::OnPreRender(Application& app) {
		(void)app;
		// The editor rebuilt the UI region for this frame: active means the Game View drew the game.
		m_VirtualSurface = false;
		if (!m_Job) return;
		Job& job = *m_Job;
		ProcessCaptures(job);
		if (job.Kind == JobKind::Screenshot) {
			if (!job.CaptureError.empty()) {
				FailJob(job.CaptureError);
				return;
			}
			Json::Value result = job.Screenshots.GetArray().empty() ? Json::Value::MakeObject() : job.Screenshots.GetArray().front();
			FinishJob(result);
			return;
		}
		if (job.Kind == JobKind::Input && IsInputComplete(job)) {
			Json::Value result = Json::Value::MakeObject();
			result.AddMember("steps", static_cast<int>(job.Steps.size()));
			result.AddMember("frames", job.InputFrame);
			result.AddMember("seconds", job.InputClock);
			result.AddMember("commands", job.Commands);
			result.AddMember("screenshots", job.Screenshots);
			result.AddMember("quit", job.QuitRequested);
			FinishJob(result);
		}
	}

	void EditorBridgeLayer::Dispatch(uint64_t ticket, const EditorBridge::Request& request) {
		const std::string& command = request.Command;
		const Json::Value& args = request.Args;
		const std::string& id = request.Id;
		auto fail = [&](const std::string& message, bool busy = false) { Fail(ticket, id, message, busy); };
		auto startJob = [&](JobKind kind) -> Job& {
			m_Job.emplace();
			m_Job->Kind = kind;
			m_Job->Ticket = ticket;
			m_Job->Id = id;
			return *m_Job;
		};
		const bool playing = Application::GetIsPlaying();

		if (command == "status") return Reply(ticket, id, BuildStatus());
		if (command == "logs") return Reply(ticket, id, ReadLogs(args));

		if (const std::string busy = m_Editor.GetBridgeBusyReason(); !busy.empty()) return fail("busy: " + busy, true);

		if (command == "play") {
			int width = 0, height = 0;
			if (const std::string size = ArgString(args, "size"); !size.empty() && !EditorBridge::ParseSize(size, width, height)) {
				return fail("--size must look like 1080x2400");
			}
			float fixedStep = 0.0f;
			if (const std::string step = ArgString(args, "fixedStep"); !step.empty() && !RunAutomation::ParseFixedStep(step, fixedStep)) {
				return fail("--fixed-step must be frames per second, e.g. 60");
			}
			const std::string sceneName = ArgString(args, "scene");
			Scene* active = SceneManager::GetActiveScene();
			if (!sceneName.empty() && (!active || active->GetName() != sceneName)) {
				if (playing) return fail("already playing '" + (active ? active->GetName() : std::string()) + "'; stop first");
				std::string error;
				if (!m_Editor.BridgeOpenScene(sceneName, ArgBool(args, "save"), ArgBool(args, "discard"), error)) return fail(error);
			}
			if (width > 0) {
				m_SurfaceWidth = width;
				m_SurfaceHeight = height;
				m_Editor.SetBridgeSurfaceSize(width, height);
			}
			if (fixedStep > 0.0f) {
				m_SessionFixedStep = fixedStep;
				RunAutomation::SetFixedStepOverride(fixedStep);
			}
			std::string error;
			if (!m_Editor.BridgeRequestPlay(error)) {
				if (!playing) EndPlaySession();
				return fail(error);
			}
			m_SessionDriven = true;
			Job& job = startJob(JobKind::Play);
			job.WaitSeconds = ArgNumber(args, "seconds", -1.0);
			job.WaitFrames = static_cast<int64_t>(ArgNumber(args, "frames", -1.0));
			if (!ArgBool(args, "wait", true)) FinishJob(BuildStatus());
			return;
		}
		if (command == "stop") {
			if (!playing && !m_Editor.IsPlayModeEntryPending()) return Reply(ticket, id, BuildStatus());
			m_Editor.BridgeRequestStop();
			startJob(JobKind::Stop);
			return;
		}
		if (command == "pause" || command == "resume") {
			if (!playing) return fail("not in Play mode");
			m_SessionDriven = true;
			m_Editor.SetPlayModePaused(command == "pause");
			return Reply(ticket, id, BuildStatus());
		}
		if (command == "step") {
			if (!playing) return fail("not in Play mode");
			m_SessionDriven = true;
			Job& job = startJob(JobKind::Step);
			job.WaitFrames = std::max<int64_t>(1, static_cast<int64_t>(ArgNumber(args, "frames", 1.0)));
			m_Editor.SetPlayModePaused(false);
			return;
		}
		if (command == "wait") {
			Job& job = startJob(JobKind::Wait);
			job.WaitSeconds = ArgNumber(args, "seconds", -1.0);
			job.WaitFrames = static_cast<int64_t>(ArgNumber(args, "frames", -1.0));
			job.GameTime = playing;
			if (job.WaitSeconds < 0.0 && job.WaitFrames < 0) {
				m_Job.reset();
				return fail("wait needs --seconds or --frames");
			}
			if (playing) m_SessionDriven = true;
			return;
		}
		if (command == "open-scene") {
			std::string error;
			if (!m_Editor.BridgeOpenScene(ArgString(args, "scene"), ArgBool(args, "save"), ArgBool(args, "discard"), error)) return fail(error);
			return Reply(ticket, id, BuildStatus());
		}
		if (command == "save-scene") {
			std::vector<std::string> saved;
			std::string error;
			if (!m_Editor.BridgeSaveScenes(saved, error)) return fail(error);
			Json::Value result = Json::Value::MakeObject();
			Json::Value names = Json::Value::MakeArray();
			for (const std::string& name : saved) names.Append(name);
			result.AddMember("saved", names);
			return Reply(ticket, id, result);
		}
		if (command == "exec") {
			const std::string line = ArgString(args, "command");
			if (line.empty()) return fail("exec needs a console command");
			if (playing) m_SessionDriven = true;
			const DevCommandResult result = DevConsole::Execute(line);
			Json::Value out = Json::Value::MakeObject();
			out.AddMember("ok", result.Result == DevCommandResult::Status::Ok);
			out.AddMember("status", result.Result == DevCommandResult::Status::Ok ? "ok"
				: result.Result == DevCommandResult::Status::NotFound ? "not-found" : "error");
			out.AddMember("output", result.Output);
			return Reply(ticket, id, out);
		}
		if (command == "recompile") {
			ScriptSystem* scripts = GetActiveScriptSystem();
			if (!scripts) return fail("the open scene has no script system");
			const uint64_t before = ScriptSystem::GetCompletedRebuildCount();
			const bool running = scripts->RequestRebuildAndReloadAll();
			if (!running && !scripts->IsRebuilding()) {
				Json::Value result = Json::Value::MakeObject();
				result.AddMember("succeeded", scripts->DidLastRebuildSucceed());
				result.AddMember("errors", ScriptSystem::GetLastRebuildOutput());
				return Reply(ticket, id, result);
			}
			if (!ArgBool(args, "wait", true)) {
				Json::Value result = Json::Value::MakeObject();
				result.AddMember("started", true);
				return Reply(ticket, id, result);
			}
			Job& job = startJob(JobKind::Recompile);
			job.RebuildsAtStart = before;
			job.WasPlaying = playing;
			return;
		}
		if (command == "screenshot") {
			Capture capture;
			capture.Path = ScreenCapture::ResolvePath(ArgString(args, "path").empty()
				? "editor-" + std::to_string(UnixMillis()) + ".png" : ArgString(args, "path"));
			capture.Width = static_cast<int>(ArgNumber(args, "width", 0.0));
			capture.Height = static_cast<int>(ArgNumber(args, "height", 0.0));
			if ((capture.Width > 0) != (capture.Height > 0) || capture.Width > 16384 || capture.Height > 16384) {
				return fail("give both --width and --height (up to 16384)");
			}
			if (playing) m_SessionDriven = true;
			startJob(JobKind::Screenshot).PendingCaptures.push_back(std::move(capture));
			return;
		}
		if (command == "input") {
			if (!playing) return fail("not in Play mode (run `funke editor play` first)");
			const Json::Value* scriptValue = args.FindMember("script");
			if (!scriptValue || !scriptValue->IsObject()) return fail("input needs a script object");
			RunAutomation::Script script;
			std::string error;
			if (!RunAutomation::ParseScript(Json::Stringify(*scriptValue), script, error)) return fail("input script: " + error);
			float fixedStep = script.FixedStep;
			if (const std::string step = ArgString(args, "fixedStep"); !step.empty() && !RunAutomation::ParseFixedStep(step, fixedStep)) {
				return fail("--fixed-step must be frames per second, e.g. 60");
			}
			m_SessionDriven = true;
			EnsureGameSurface();
			Job& job = startJob(JobKind::Input);
			job.Steps = std::move(script.Steps);
			job.StepDone.assign(job.Steps.size(), false);
			job.ScreenshotDirectory = ArgString(args, "screenshotDir");
			if (job.ScreenshotDirectory.empty()) job.ScreenshotDirectory = ScreenCapture::GetDirectory();
			if (fixedStep > 0.0f) {
				RunAutomation::SetFixedStepOverride(fixedStep);
				job.RestoreFixedStep = true;
			}
			return;
		}
		if (command == "surface") {
			if (ArgBool(args, "clear")) {
				m_SurfaceWidth = m_SurfaceHeight = 0;
				m_Editor.SetBridgeSurfaceSize(0, 0);
			}
			else {
				int width = 0, height = 0;
				if (!EditorBridge::ParseSize(ArgString(args, "size"), width, height)) return fail("surface needs a size like 1080x2400, or --clear");
				m_SurfaceWidth = width;
				m_SurfaceHeight = height;
				m_Editor.SetBridgeSurfaceSize(width, height);
			}
			const auto [width, height] = GetSurfaceSize();
			return Reply(ticket, id, SizeJson(width, height));
		}
		fail("unknown command '" + command + "' (see `funke help editor`)");
	}

	void EditorBridgeLayer::UpdateJob() {
		if (!m_Job) return;
		Job& job = *m_Job;
		const bool playing = Application::GetIsPlaying();
		const bool gameRunning = playing && !ApplicationEditorAccess::IsPlaymodePaused();

		switch (job.Kind) {
		case JobKind::Play:
			if (playing) {
				if (job.WaitSeconds < 0.0 && job.WaitFrames < 0) {
					FinishJob(BuildStatus());
					return;
				}
				// Counting starts with the first frame the game updates.
				job.Kind = JobKind::Wait;
				job.GameTime = true;
				return;
			}
			if (!m_Editor.IsPlayModeEntryPending()) {
				EndPlaySession();
				FailJob("Play mode did not start (script errors? see `funke editor logs --level warning`)");
			}
			return;
		case JobKind::Stop:
			if (!playing && !m_Editor.IsPlayModeEntryPending()) FinishJob(BuildStatus());
			return;
		case JobKind::Recompile: {
			ScriptSystem* scripts = GetActiveScriptSystem();
			if (scripts && scripts->IsRebuilding()) {
				job.IdleFrames = 0;
				return;
			}
			if (ScriptSystem::GetCompletedRebuildCount() <= job.RebuildsAtStart) {
				// Leaving Play mode (Exit Play Mode On Recompilation) tears the scripts down and drops a build
				// that is still running; once the edit scene is back, start it again.
				if (++job.IdleFrames < 10 || !scripts) return;
				if (job.Retried) {
					FailJob("the script build was interrupted; run `funke editor recompile` again");
					return;
				}
				job.Retried = true;
				job.IdleFrames = 0;
				scripts->RequestRebuildAndReloadAll();
				return;
			}
			Json::Value result = Json::Value::MakeObject();
			result.AddMember("succeeded", scripts ? scripts->DidLastRebuildSucceed() : ScriptSystem::GetLastRebuildOutput().empty());
			result.AddMember("errors", ScriptSystem::GetLastRebuildOutput());
			result.AddMember("seconds", std::chrono::duration<double>(Clock::now() - job.Started).count());
			result.AddMember("stoppedPlayMode", job.WasPlaying && !playing);
			FinishJob(result);
			return;
		}
		case JobKind::Wait:
		case JobKind::Step: {
			if (job.GameTime && !gameRunning) {
				if (!playing) FailJob("Play mode ended while waiting");
				return;
			}
			++job.ElapsedFrames;
			job.ElapsedSeconds += Time::GetUnscaledDeltaTime();
			const bool framesDone = job.WaitFrames >= 0 && job.ElapsedFrames >= job.WaitFrames;
			const bool secondsDone = job.WaitSeconds >= 0.0 && job.ElapsedSeconds + 5e-7 >= job.WaitSeconds;
			if (!framesDone && !secondsDone) return;
			if (job.Kind == JobKind::Step) m_Editor.SetPlayModePaused(true);
			Json::Value result = BuildStatus();
			result.AddMember("waitedFrames", job.ElapsedFrames);
			result.AddMember("waitedSeconds", job.ElapsedSeconds);
			FinishJob(result);
			return;
		}
		case JobKind::Input:
			RunInputFrame(job);
			return;
		case JobKind::Screenshot:
			return;
		}
	}

	void EditorBridgeLayer::RunInputFrame(Job& job) {
		if (!Application::GetIsPlaying()) {
			if (job.QuitRequested) {
				Json::Value result = Json::Value::MakeObject();
				result.AddMember("steps", static_cast<int>(job.Steps.size()));
				result.AddMember("commands", job.Commands);
				result.AddMember("screenshots", job.Screenshots);
				result.AddMember("quit", true);
				FinishJob(result);
			}
			else {
				FailJob("Play mode ended during the input script");
			}
			return;
		}
		// The script's clock holds while the editor's Pause button holds the game.
		if (ApplicationEditorAccess::IsPlaymodePaused()) return;
		if (job.InputStarted) {
			job.InputClock += Time::GetUnscaledDeltaTime();
			++job.InputFrame;
		}
		job.InputStarted = true;

		// A "press" holds for exactly one frame, so GetKeyDown and GetKey both see it.
		std::vector<Held> releases;
		releases.swap(job.Releases);
		for (const Held& held : releases) ReleaseHeld(job, held);

		for (size_t i = 0; i < job.Steps.size() && !job.QuitRequested; ++i) {
			if (job.StepDone[i]) continue;
			const RunAutomation::Moment& at = job.Steps[i].At;
			const bool due = at.Frame >= 0 ? job.InputFrame >= at.Frame : job.InputClock + 5e-7 >= at.Seconds;
			if (!due) continue;
			job.StepDone[i] = true;
			RunInputStep(job, job.Steps[i]);
		}
		m_InputGrantFrames = 2;
	}

	void EditorBridgeLayer::RunInputStep(Job& job, const RunAutomation::Step& step) {
		using Kind = RunAutomation::Step::Kind;
		using Action = RunAutomation::Step::Action;
		const bool press = step.Act == Action::Press;
		switch (step.Type) {
		case Kind::Key:
			Window::InjectKey(step.Code, step.Act != Action::Up);
			if (press) job.Releases.push_back({ Kind::Key, step.Code });
			break;
		case Kind::MouseMove:
			Window::InjectMouseMove(SurfaceToWindow(step.Position));
			break;
		case Kind::MouseButton:
			if (step.Position.x >= 0.0f) Window::InjectMouseMove(SurfaceToWindow(step.Position));
			Window::InjectMouseButton(step.Code, step.Act != Action::Up);
			if (press) job.Releases.push_back({ Kind::MouseButton, step.Code, step.Position });
			break;
		case Kind::Touch: {
			const int phase = step.Act == Action::Up ? 2 : step.Act == Action::Move ? 1 : 0;
			InjectTouch(job, step.Finger, step.Position, phase);
			if (press) job.Releases.push_back({ Kind::Touch, 0, step.Position, step.Finger });
			break;
		}
		case Kind::Command: {
			const DevCommandResult result = DevConsole::Execute(step.Text);
			Json::Value entry = Json::Value::MakeObject();
			entry.AddMember("at", step.At.Text);
			entry.AddMember("command", step.Text);
			entry.AddMember("ok", result.Result == DevCommandResult::Status::Ok);
			entry.AddMember("output", result.Output);
			job.Commands.Append(std::move(entry));
			break;
		}
		case Kind::Screenshot: {
			std::filesystem::path path = Path::FromUtf8(step.Text.empty() ? ScreenshotFileName(step.At.Text) : step.Text);
			if (path.extension().empty()) path += ".png";
			if (path.is_relative()) path = Path::FromUtf8(job.ScreenshotDirectory) / path;
			job.PendingCaptures.push_back({ Path::ToUtf8(path.lexically_normal()), 0, 0, step.At.Text });
			break;
		}
		case Kind::Log:
			FNK_INFO_TAG("Automation", "[{}] {}", step.At.Text, step.Text);
			break;
		case Kind::Quit:
			job.QuitRequested = true;
			m_Editor.BridgeRequestStop();
			break;
		}
	}

	void EditorBridgeLayer::InjectTouch(Job& job, int64_t finger, Vec2 normalized, int phase) {
		const Vec2 position = SurfaceToWindow(normalized);
		Window::InjectTouch(finger + 1, position, phase);
		// A real touch screen's first finger also drives the mouse, which UI buttons and scroll views follow.
		if (phase == 0 && job.MouseFinger < 0) job.MouseFinger = finger;
		if (finger != job.MouseFinger) return;
		Window::InjectMouseMove(position);
		if (phase == 0) Window::InjectMouseButton(0, true);
		else if (phase == 2) {
			Window::InjectMouseButton(0, false);
			job.MouseFinger = -1;
		}
	}

	void EditorBridgeLayer::ReleaseHeld(Job& job, const Held& held) {
		using Kind = RunAutomation::Step::Kind;
		switch (held.Kind) {
		case Kind::Key: Window::InjectKey(held.Code, false); break;
		case Kind::MouseButton: Window::InjectMouseButton(held.Code, false); break;
		case Kind::Touch: InjectTouch(job, held.Finger, held.Position, 2); break;
		default: break;
		}
	}

	bool EditorBridgeLayer::IsInputComplete(const Job& job) const {
		if (!job.Releases.empty() || !job.PendingCaptures.empty() || job.QuitRequested) return false;
		return std::all_of(job.StepDone.begin(), job.StepDone.end(), [](bool done) { return done; });
	}

	void EditorBridgeLayer::ProcessCaptures(Job& job) {
		std::vector<Capture> captures;
		captures.swap(job.PendingCaptures);
		for (const Capture& capture : captures) {
			auto [width, height] = GetSurfaceSize();
			if (capture.Width > 0 && capture.Height > 0) {
				width = capture.Width;
				height = capture.Height;
			}
			std::vector<uint8_t> rgba;
			std::string error;
			if (!m_Editor.BridgeRenderGameView(width, height, rgba, error)) {
				error = "screenshot failed: " + error;
			}
			else if (!ScreenCapture::WritePng(capture.Path, static_cast<uint32_t>(width), static_cast<uint32_t>(height), rgba)) {
				error = "could not write " + capture.Path;
			}
			Json::Value entry = SizeJson(width, height);
			entry.AddMember("path", capture.Path);
			if (!capture.At.empty()) entry.AddMember("at", capture.At);
			if (!error.empty()) {
				entry.AddMember("error", error);
				job.CaptureError = error;
			}
			job.Screenshots.Append(std::move(entry));
			FNK_CORE_INFO_TAG("EditorBridge", "Screenshot {} ({}x{}){}", capture.Path, width, height,
				error.empty() ? std::string() : ": " + error);
		}
	}

	void EditorBridgeLayer::Reply(uint64_t ticket, const std::string& id, const Json::Value& result) {
		m_Server.Respond(ticket, EditorBridge::FormatResult(id, result));
	}

	void EditorBridgeLayer::Fail(uint64_t ticket, const std::string& id, const std::string& message, bool busy) {
		m_Server.Respond(ticket, EditorBridge::FormatError(id, message, busy));
	}

	void EditorBridgeLayer::FinishJob(const Json::Value& result) {
		if (!m_Job) return;
		Reply(m_Job->Ticket, m_Job->Id, result);
		EndJob();
	}

	void EditorBridgeLayer::FailJob(const std::string& message) {
		if (!m_Job) return;
		Fail(m_Job->Ticket, m_Job->Id, message);
		EndJob();
	}

	void EditorBridgeLayer::EndJob() {
		if (!m_Job) return;
		Job& job = *m_Job;
		if (Application::GetIsPlaying()) {
			for (const Held& held : std::exchange(job.Releases, {})) ReleaseHeld(job, held);
			if (job.MouseFinger >= 0) InjectTouch(job, job.MouseFinger, Vec2{ 0.5f }, 2);
		}
		if (job.RestoreFixedStep) RunAutomation::SetFixedStepOverride(m_SessionFixedStep);
		m_Job.reset();
	}

	void EditorBridgeLayer::EndPlaySession() {
		if (m_SessionFixedStep > 0.0f) RunAutomation::SetFixedStepOverride(0.0f);
		m_SessionFixedStep = 0.0f;
		if (m_SurfaceWidth > 0) m_Editor.SetBridgeSurfaceSize(0, 0);
		m_SurfaceWidth = m_SurfaceHeight = 0;
		m_SessionDriven = false;
	}

	void EditorBridgeLayer::EnsureGameSurface() {
		if (!Application::GetIsPlaying() || !m_SessionDriven) return;
		// Active: the Game View laid the game out last frame (or this method already ran). Otherwise (hidden tab,
		// minimized window) the game gets a surface of its last Game View size, where the real mouse can't point.
		if (Window::GetUIRegion().IsActive()) return;
		const auto [width, height] = GetSurfaceSize();
		Window::SetUIRegion(k_VirtualSurfaceOrigin, k_VirtualSurfaceOrigin, width, height);
		m_VirtualSurface = true;
	}

	std::pair<int, int> EditorBridgeLayer::GetSurfaceSize() const {
		if (m_SurfaceWidth > 0 && m_SurfaceHeight > 0) return { m_SurfaceWidth, m_SurfaceHeight };
		if (const Window::UIRegion region = Window::GetUIRegion(); region.IsActive()) return { region.Width, region.Height };
		int width = 0, height = 0;
		if (m_Editor.GetGameViewSurfaceSize(width, height)) return { width, height };
		return { k_DefaultSurfaceWidth, k_DefaultSurfaceHeight };
	}

	Vec2 EditorBridgeLayer::SurfaceToWindow(Vec2 normalized) const {
		const Window::UIRegion region = Window::GetUIRegion();
		const Vec2 clamped{ std::clamp(normalized.x, 0.0f, 1.0f), std::clamp(normalized.y, 0.0f, 1.0f) };
		// Half a pixel in from the far edges: the surface ends before x == width.
		const Vec2 size{ std::max(static_cast<float>(region.Width) - 0.5f, 0.0f), std::max(static_cast<float>(region.Height) - 0.5f, 0.0f) };
		return EditorBridge::SurfaceToWindow(clamped, { static_cast<float>(region.OffsetX), static_cast<float>(region.OffsetY) },
			size, region.WindowToRegionScale);
	}

	Json::Value EditorBridgeLayer::BuildStatus() const {
		Json::Value status = Json::Value::MakeObject();
		const FunkeProject* project = ProjectManager::GetCurrentProject();
		status.AddMember("project", project ? project->RootDirectory : std::string());
		status.AddMember("projectName", project ? project->Name : std::string());
		status.AddMember("pid", static_cast<int64_t>(EditorBridge::GetCurrentProcessId()));
		status.AddMember("version", std::string(Engine::GetVersionLong()));
		status.AddMember("protocol", EditorBridge::k_ProtocolVersion);

		const bool playing = Application::GetIsPlaying();
		const char* state = playing ? (ApplicationEditorAccess::IsPlaymodePaused() ? "paused" : "playing")
			: m_Editor.IsPlayModeEntryPending() ? "starting" : "edit";
		status.AddMember("state", state);
		Scene* active = SceneManager::GetActiveScene();
		status.AddMember("scene", active ? active->GetName() : std::string());
		Json::Value scenes = Json::Value::MakeArray();
		Json::Value dirty = Json::Value::MakeArray();
		for (const std::weak_ptr<Scene>& weakScene : SceneManager::GetLoadedScenes()) {
			if (std::shared_ptr<Scene> scene = weakScene.lock()) {
				scenes.Append(scene->GetName());
				if (scene->IsDirty()) dirty.Append(scene->GetName());
			}
		}
		status.AddMember("scenes", scenes);
		status.AddMember("unsavedScenes", dirty);

		Json::Value scripts = Json::Value::MakeObject();
		ScriptSystem* scriptSystem = GetActiveScriptSystem();
		scripts.AddMember("compiling", scriptSystem && scriptSystem->IsRebuilding());
		scripts.AddMember("lastBuildSucceeded", scriptSystem ? scriptSystem->DidLastRebuildSucceed() : true);
		scripts.AddMember("errors", ScriptSystem::GetLastRebuildOutput());
		status.AddMember("scripts", scripts);

		const std::string busy = m_Editor.GetBridgeBusyReason();
		status.AddMember("busy", busy.empty() ? Json::Value() : Json::Value(busy));
		const auto [width, height] = GetSurfaceSize();
		Json::Value surface = SizeJson(width, height);
		surface.AddMember("source", m_SurfaceWidth > 0 ? "fixed" : m_VirtualSurface ? "virtual"
			: Window::GetUIRegion().IsActive() ? "game-view" : "hidden-game-view");
		status.AddMember("surface", surface);
		status.AddMember("fixedStep", static_cast<double>(RunAutomation::GetFixedStep()));
		status.AddMember("frame", Time::GetFrameCount());
		status.AddMember("gameTime", playing ? static_cast<double>(Time::GetTimeSinceStartup()) : 0.0);
		return status;
	}

	Json::Value EditorBridgeLayer::ReadLogs(const Json::Value& args) const {
		const uint64_t since = static_cast<uint64_t>(std::max(0.0, ArgNumber(args, "since", 0.0)));
		const size_t tail = static_cast<size_t>(std::max(0.0, ArgNumber(args, "tail", 200.0)));
		Log::Level minimum = Log::Level::Trace;
		if (const std::string level = ArgString(args, "level"); !level.empty()) ParseLevel(level, minimum);
		const std::string contains = LowerCopy(ArgString(args, "contains"));

		std::vector<const LogLine*> matches;
		Json::Value result = Json::Value::MakeObject();
		Json::Value lines = Json::Value::MakeArray();
		uint64_t latest = 0;
		if (m_Logs) {
			std::lock_guard lock(m_Logs->Mutex);
			latest = m_Logs->NextSeq - 1;
			for (const LogLine& line : m_Logs->Lines) {
				if (line.Seq <= since || line.Level < minimum) continue;
				if (!contains.empty() && LowerCopy(line.Message).find(contains) == std::string::npos) continue;
				matches.push_back(&line);
			}
			const size_t first = tail > 0 && matches.size() > tail ? matches.size() - tail : 0;
			for (size_t i = first; i < matches.size(); ++i) {
				const LogLine& line = *matches[i];
				Json::Value entry = Json::Value::MakeObject();
				entry.AddMember("seq", static_cast<int64_t>(line.Seq));
				entry.AddMember("time", line.Time);
				entry.AddMember("level", LevelName(line.Level));
				entry.AddMember("source", line.Core ? "core" : "game");
				entry.AddMember("message", line.Message);
				lines.Append(std::move(entry));
			}
		}
		result.AddMember("lines", lines);
		result.AddMember("next", static_cast<int64_t>(latest));
		return result;
	}

	ScriptSystem* EditorBridgeLayer::GetActiveScriptSystem() {
		Scene* scene = SceneManager::GetActiveScene();
		return scene && scene->HasSystem<ScriptSystem>() ? scene->GetSystem<ScriptSystem>() : nullptr;
	}

	void EditorBridgeLayer::WriteDiscoveryFile() {
		const FunkeProject* project = ProjectManager::GetCurrentProject();
		EditorBridge::Instance instance;
		instance.Pid = EditorBridge::GetCurrentProcessId();
		instance.ProjectRoot = project ? project->RootDirectory : std::string();
		instance.ProjectName = project ? project->Name : std::string();
		instance.Endpoint = m_Server.GetEndpoint();
		instance.Token = m_Token;
		instance.Version = std::string(Engine::GetVersionLong());
		instance.StartedAtUnixMs = UnixMillis();

		const std::filesystem::path directory = EditorBridge::GetDiscoveryDirectory();
		std::error_code ec;
		std::filesystem::create_directories(directory, ec);
		const std::filesystem::path file = directory / (std::to_string(instance.Pid) + ".json");
		const std::filesystem::path temp = directory / (std::to_string(instance.Pid) + ".json.tmp");
		// Written aside and renamed, so a client never reads half a file.
		if (!File::WriteAllText(Path::ToUtf8(temp), EditorBridge::SerializeInstance(instance))) {
			FNK_CORE_WARN_TAG("EditorBridge", "Could not write {}", Path::ToUtf8(temp));
			return;
		}
#if !defined(FNK_PLATFORM_WINDOWS)
		// The file holds the token; %LOCALAPPDATA% is private on Windows, the data folder elsewhere may not be.
		std::filesystem::permissions(temp, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
			std::filesystem::perm_options::replace, ec);
#endif
		std::filesystem::rename(temp, file, ec);
		if (ec) {
			FNK_CORE_WARN_TAG("EditorBridge", "Could not write {}: {}", Path::ToUtf8(file), ec.message());
			return;
		}
		m_DiscoveryFile = Path::ToUtf8(file);
	}

	void EditorBridgeLayer::RemoveDiscoveryFile() {
		if (m_DiscoveryFile.empty()) return;
		std::error_code ec;
		std::filesystem::remove(Path::FromUtf8(m_DiscoveryFile), ec);
		m_DiscoveryFile.clear();
	}

} // namespace Funke
