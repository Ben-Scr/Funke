#include "pch.hpp"
#include "DevTools/RunAutomation.hpp"

#include "Core/Application.hpp"
#include "Core/KeyCode.hpp"
#include "Core/Log.hpp"
#include "Core/Platform/SDLInputMapping.hpp"
#include "Core/Window.hpp"
#include "DevTools/DevConsole.hpp"
#include "DevTools/ScreenCapture.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Json.hpp"
#include "Utils/CharConv.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Funke {

	namespace {
		struct PendingRelease {
			RunAutomation::Step::Kind Type;
			int Code = 0;
			Vec2 Position{ -1.0f };
			int64_t Finger = 0;
		};

		struct State {
			RunAutomation::Options Options;
			std::vector<RunAutomation::Step> Steps;
			std::vector<bool> Done;
			std::vector<PendingRelease> Releases;
			float FixedStep = 0.0f;
			float FixedStepOverride = 0.0f;
			// The finger SDL's touch-to-mouse emulation would follow; -1 while no finger is down.
			int64_t MouseFinger = -1;
			double ClockSeconds = 0.0;
			int64_t ClockFrame = 0;
			bool Active = false;
			bool Background = false;
			bool FrameActive = false;
			bool ExecRan = false;
			bool WarnedHeadless = false;
		};
		State g_State;

		std::string ToLower(std::string_view value) {
			std::string out(value);
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		bool ParseDouble(std::string_view text, double& out) {
			const char* begin = text.data();
			const char* end = text.data() + text.size();
			const auto result = CharConv::ParseFloat(begin, end, out);
			return result.ec == std::errc{} && result.ptr == end;
		}


		bool IsDue(const RunAutomation::Moment& at) {
			if (at.Frame >= 0) return g_State.ClockFrame >= at.Frame;
			// Half a microsecond of slack so "1s" at a 1/60 step lands on frame 60, not 61.
			return g_State.ClockSeconds + 5e-7 >= at.Seconds;
		}

		std::string ScreenshotName(const RunAutomation::Moment& at) {
			std::string name = "at-" + (at.Text.empty() ? std::to_string(at.Frame >= 0 ? at.Frame : static_cast<int64_t>(at.Seconds)) : at.Text);
			for (char& c : name) {
				if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '.' && c != '_') c = '_';
			}
			return name + ".png";
		}

		SDL_Window* NativeWindow() {
			Window* window = Application::GetWindow();
			return window ? window->GetNativeWindow() : nullptr;
		}

		Vec2 ToWindowPixels(SDL_Window* window, Vec2 normalized) {
			int width = 0, height = 0;
			SDL_GetWindowSize(window, &width, &height);
			return { normalized.x * static_cast<float>(width), normalized.y * static_cast<float>(height) };
		}

		void PushKey(SDL_Window* window, int keyCode, bool down) {
			const int scancode = SDLInputMapping::ScancodeFromKeyCode(keyCode);
			if (scancode < 0) return;
			SDL_Event event{};
			event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
			event.key.timestamp = SDL_GetTicksNS();
			event.key.windowID = SDL_GetWindowID(window);
			event.key.scancode = static_cast<SDL_Scancode>(scancode);
			event.key.key = SDL_GetKeyFromScancode(event.key.scancode, SDL_KMOD_NONE, false);
			event.key.down = down;
			SDL_PushEvent(&event);
		}

		void PushMouseMove(SDL_Window* window, Vec2 normalized) {
			const Vec2 pixels = ToWindowPixels(window, normalized);
			SDL_Event event{};
			event.type = SDL_EVENT_MOUSE_MOTION;
			event.motion.timestamp = SDL_GetTicksNS();
			event.motion.windowID = SDL_GetWindowID(window);
			event.motion.x = pixels.x;
			event.motion.y = pixels.y;
			SDL_PushEvent(&event);
		}

		void PushMouseButton(SDL_Window* window, int button, Vec2 normalized, bool down) {
			static constexpr uint8_t k_SdlButtons[] = { SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE };
			SDL_Event event{};
			event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
			event.button.timestamp = SDL_GetTicksNS();
			event.button.windowID = SDL_GetWindowID(window);
			event.button.button = k_SdlButtons[std::clamp(button, 0, 2)];
			event.button.down = down;
			event.button.clicks = 1;
			if (normalized.x >= 0.0f) {
				const Vec2 pixels = ToWindowPixels(window, normalized);
				event.button.x = pixels.x;
				event.button.y = pixels.y;
			}
			else {
				SDL_GetMouseState(&event.button.x, &event.button.y);
			}
			SDL_PushEvent(&event);
		}

		void PushTouch(SDL_Window* window, int64_t finger, Vec2 normalized, SDL_EventType type) {
			SDL_Event event{};
			event.type = type;
			event.tfinger.timestamp = SDL_GetTicksNS();
			event.tfinger.windowID = SDL_GetWindowID(window);
			event.tfinger.touchID = 1; // a virtual touch screen; the engine keys fingers by touch and finger id
			event.tfinger.fingerID = static_cast<SDL_FingerID>(finger + 1);
			event.tfinger.x = std::clamp(normalized.x, 0.0f, 1.0f);
			event.tfinger.y = std::clamp(normalized.y, 0.0f, 1.0f);
			event.tfinger.pressure = type == SDL_EVENT_FINGER_UP ? 0.0f : 1.0f;
			SDL_PushEvent(&event);

			// A real touch screen's first finger also drives the mouse (SDL's touch emulation), which is what UI
			// buttons and scroll views listen to; pushed finger events skip that emulation, so mirror it here.
			if (type == SDL_EVENT_FINGER_DOWN && g_State.MouseFinger < 0) g_State.MouseFinger = finger;
			if (finger != g_State.MouseFinger) return;
			const Vec2 clamped{ std::clamp(normalized.x, 0.0f, 1.0f), std::clamp(normalized.y, 0.0f, 1.0f) };
			PushMouseMove(window, clamped);
			if (type == SDL_EVENT_FINGER_DOWN) PushMouseButton(window, 0, clamped, true);
			else if (type == SDL_EVENT_FINGER_UP) {
				PushMouseButton(window, 0, clamped, false);
				g_State.MouseFinger = -1;
			}
		}

		void RunStep(const RunAutomation::Step& step) {
			using Kind = RunAutomation::Step::Kind;
			using Action = RunAutomation::Step::Action;
			switch (step.Type) {
			case Kind::Command:
				DevConsole::Execute(step.Text);
				return;
			case Kind::Screenshot:
				ScreenCapture::Request(step.Text.empty() ? ScreenshotName(step.At) : step.Text);
				return;
			case Kind::Log:
				FNK_CORE_INFO_TAG("Automation", "[{}] {}", step.At.Text, step.Text);
				return;
			case Kind::Quit:
				FNK_CORE_INFO_TAG("Automation", "Quit at frame {} ({:.3f}s).", g_State.ClockFrame, g_State.ClockSeconds);
				Application::RequestQuit();
				return;
			default:
				break;
			}

			SDL_Window* window = NativeWindow();
			if (!window) {
				if (!g_State.WarnedHeadless) FNK_CORE_WARN_TAG("Automation", "No window: scripted input is skipped.");
				g_State.WarnedHeadless = true;
				return;
			}
			const bool press = step.Act == Action::Press;
			switch (step.Type) {
			case Kind::Key:
				PushKey(window, step.Code, step.Act != Action::Up);
				if (press) g_State.Releases.push_back({ Kind::Key, step.Code });
				break;
			case Kind::MouseMove:
				PushMouseMove(window, step.Position);
				break;
			case Kind::MouseButton:
				if (step.Position.x >= 0.0f) PushMouseMove(window, step.Position);
				PushMouseButton(window, step.Code, step.Position, step.Act != Action::Up);
				if (press) g_State.Releases.push_back({ Kind::MouseButton, step.Code, step.Position });
				break;
			case Kind::Touch: {
				const SDL_EventType type = step.Act == Action::Up ? SDL_EVENT_FINGER_UP
					: step.Act == Action::Move ? SDL_EVENT_FINGER_MOTION : SDL_EVENT_FINGER_DOWN;
				PushTouch(window, step.Finger, step.Position, type);
				if (press) g_State.Releases.push_back({ Kind::Touch, 0, step.Position, step.Finger });
				break;
			}
			default:
				break;
			}
		}

		bool ReadPosition(const Json::Value& value, Vec2& out) {
			if (!value.IsArray() || value.GetArray().size() != 2) return false;
			const auto& array = value.GetArray();
			if (!array[0].IsNumber() || !array[1].IsNumber()) return false;
			out = { static_cast<float>(array[0].AsDoubleOr(0.0)), static_cast<float>(array[1].AsDoubleOr(0.0)) };
			return true;
		}

		bool ReadAction(const Json::Value& step, RunAutomation::Step::Action& out, bool allowMove) {
			const Json::Value* value = step.FindMember("action");
			if (!value) return true;
			const std::string action = ToLower(value->AsStringOr());
			if (action == "press" || action == "click" || action == "tap") out = RunAutomation::Step::Action::Press;
			else if (action == "down") out = RunAutomation::Step::Action::Down;
			else if (action == "up") out = RunAutomation::Step::Action::Up;
			else if (allowMove && action == "move") out = RunAutomation::Step::Action::Move;
			else return false;
			return true;
		}
	}

	bool RunAutomation::ParseMoment(std::string_view text, Moment& out) {
		out = Moment{};
		std::string trimmed;
		for (const char c : text) {
			if (!std::isspace(static_cast<unsigned char>(c))) trimmed.push_back(c);
		}
		if (trimmed.empty()) return false;
		out.Text = trimmed;
		const std::string lower = ToLower(trimmed);
		double value = 0.0;
		if (lower.back() == 'f') {
			if (!ParseDouble(std::string_view(lower).substr(0, lower.size() - 1), value) || value < 0.0 || std::floor(value) != value) return false;
			out.Frame = static_cast<int64_t>(value);
			return true;
		}
		std::string_view number = lower;
		double scale = 1.0;
		if (lower.size() > 2 && lower.compare(lower.size() - 2, 2, "ms") == 0) {
			number = std::string_view(lower).substr(0, lower.size() - 2);
			scale = 0.001;
		}
		else if (lower.back() == 's') {
			number = std::string_view(lower).substr(0, lower.size() - 1);
		}
		if (!ParseDouble(number, value) || value < 0.0) return false;
		out.Seconds = value * scale;
		return true;
	}

	int RunAutomation::ParseKeyName(std::string_view name) {
		struct NamedKey { const char* Name; KeyCode Key; };
		static constexpr NamedKey k_Names[] = {
			{ "space", KeyCode::Space }, { "enter", KeyCode::Enter }, { "return", KeyCode::Enter },
			{ "esc", KeyCode::Esc }, { "escape", KeyCode::Esc }, { "tab", KeyCode::Tab },
			{ "backspace", KeyCode::Backspace }, { "insert", KeyCode::Insert }, { "delete", KeyCode::Delete },
			{ "left", KeyCode::Left }, { "right", KeyCode::Right }, { "up", KeyCode::Up },
			{ "down", KeyCode::Bottom }, { "bottom", KeyCode::Bottom },
			{ "leftarrow", KeyCode::Left }, { "rightarrow", KeyCode::Right }, { "uparrow", KeyCode::Up }, { "downarrow", KeyCode::Bottom },
			{ "pageup", KeyCode::PageUp }, { "pagedown", KeyCode::PageDown }, { "home", KeyCode::Home }, { "end", KeyCode::End },
			{ "shift", KeyCode::LeftShift }, { "leftshift", KeyCode::LeftShift }, { "rightshift", KeyCode::RightShift },
			{ "ctrl", KeyCode::LeftControl }, { "control", KeyCode::LeftControl }, { "leftcontrol", KeyCode::LeftControl },
			{ "rightcontrol", KeyCode::RightControl }, { "alt", KeyCode::LeftAlt }, { "leftalt", KeyCode::LeftAlt },
			{ "rightalt", KeyCode::RightAlt }, { "comma", KeyCode::Comma }, { "period", KeyCode::Period },
			{ "minus", KeyCode::Minus }, { "equal", KeyCode::Equal }, { "slash", KeyCode::Slash },
			{ "semicolon", KeyCode::Semicolon }, { "apostrophe", KeyCode::Apostrophe }, { "graveaccent", KeyCode::GraveAccent },
			{ "leftbracket", KeyCode::LeftBracket }, { "rightbracket", KeyCode::RightBracket }, { "backslash", KeyCode::Backslash },
			{ "capslock", KeyCode::CapsLock }, { "pause", KeyCode::Pause },
		};
		const std::string lower = ToLower(name);
		if (lower.empty()) return -1;
		for (const NamedKey& entry : k_Names) {
			if (lower == entry.Name) return static_cast<int>(entry.Key);
		}
		if (lower.size() == 1 && lower[0] >= 'a' && lower[0] <= 'z') return static_cast<int>(KeyCode::A) + (lower[0] - 'a');
		if (lower.size() == 1 && lower[0] >= '0' && lower[0] <= '9') return static_cast<int>(KeyCode::Alpha0) + (lower[0] - '0');
		if (lower.size() == 6 && lower.rfind("alpha", 0) == 0 && std::isdigit(static_cast<unsigned char>(lower[5]))) {
			return static_cast<int>(KeyCode::Alpha0) + (lower[5] - '0');
		}
		if (lower.size() == 3 && lower.rfind("kp", 0) == 0 && std::isdigit(static_cast<unsigned char>(lower[2]))) {
			return static_cast<int>(KeyCode::Kp0) + (lower[2] - '0');
		}
		if (lower.size() >= 2 && lower[0] == 'f') {
			int number = 0;
			const auto result = std::from_chars(lower.data() + 1, lower.data() + lower.size(), number);
			if (result.ec == std::errc{} && result.ptr == lower.data() + lower.size() && number >= 1 && number <= 25) {
				return static_cast<int>(KeyCode::F1) + number - 1;
			}
		}
		return -1;
	}

	int RunAutomation::ParseMouseButton(std::string_view name) {
		const std::string lower = ToLower(name);
		if (lower == "left" || lower == "0") return 0;
		if (lower == "right" || lower == "1") return 1;
		if (lower == "middle" || lower == "2") return 2;
		return -1;
	}

	bool RunAutomation::ParseFixedStep(std::string_view text, float& outSecondsPerFrame) {
		double value = 0.0;
		if (!ParseDouble(text, value) || value <= 0.0) return false;
		outSecondsPerFrame = static_cast<float>(value >= 1.0 ? 1.0 / value : value);
		return true;
	}

	void RunAutomation::SetFixedStepOverride(float secondsPerFrame) {
		g_State.FixedStepOverride = std::max(secondsPerFrame, 0.0f);
	}

	bool RunAutomation::ParseScript(std::string_view json, Script& out, std::string& outError) {
		out = Script{};
		Json::Value root;
		std::string parseError;
		if (!Json::TryParse(json, root, &parseError) || !root.IsObject()) {
			outError = parseError.empty() ? std::string("the script must be a JSON object") : parseError;
			return false;
		}
		if (const Json::Value* fixedStep = root.FindMember("fixedStep")) {
			if (!fixedStep->IsNumber() || !ParseFixedStep(std::to_string(fixedStep->AsDoubleOr(0.0)), out.FixedStep)) {
				outError = "'fixedStep' must be a positive number (frames per second)";
				return false;
			}
		}
		const Json::Value* steps = root.FindMember("steps");
		if (!steps || !steps->IsArray()) {
			outError = "the script needs a 'steps' array";
			return false;
		}
		size_t index = 0;
		for (const Json::Value& entry : steps->GetArray()) {
			++index;
			const std::string where = "step " + std::to_string(index) + ": ";
			if (!entry.IsObject()) {
				outError = where + "must be an object";
				return false;
			}
			Step step;
			if (const Json::Value* frame = entry.FindMember("frame")) {
				const double value = frame->AsDoubleOr(-1.0);
				if (!frame->IsNumber() || value < 0.0 || std::floor(value) != value) {
					outError = where + "'frame' must be a whole number >= 0";
					return false;
				}
				step.At.Frame = static_cast<int64_t>(value);
				step.At.Text = std::to_string(step.At.Frame) + "f";
			}
			else if (const Json::Value* at = entry.FindMember("at")) {
				const std::string text = at->IsNumber() ? std::to_string(at->AsDoubleOr(-1.0)) + "s" : at->AsStringOr();
				if (!ParseMoment(text, step.At)) {
					outError = where + "'at' must be a time like 2.5, \"2.5s\", \"500ms\" or a frame like \"120f\"";
					return false;
				}
				if (at->IsNumber()) step.At.Text = at->AsStringOr(Json::Stringify(*at)) + "s";
			}
			else {
				outError = where + "needs 'at' or 'frame'";
				return false;
			}

			if (const Json::Value* key = entry.FindMember("key")) {
				step.Type = Step::Kind::Key;
				step.Code = ParseKeyName(key->AsStringOr());
				if (step.Code < 0) {
					outError = where + "unknown key '" + key->AsStringOr() + "'";
					return false;
				}
				if (!ReadAction(entry, step.Act, false)) {
					outError = where + "'action' must be press, down or up";
					return false;
				}
			}
			else if (const Json::Value* mouse = entry.FindMember("mouse")) {
				if (!ReadPosition(*mouse, step.Position)) {
					outError = where + "'mouse' must be [x, y] in 0..1 of the window";
					return false;
				}
				if (const Json::Value* button = entry.FindMember("button")) {
					step.Type = Step::Kind::MouseButton;
					step.Code = ParseMouseButton(button->AsStringOr());
					if (step.Code < 0 || !ReadAction(entry, step.Act, false)) {
						outError = where + "'button' must be left, right or middle; 'action' press, down or up";
						return false;
					}
				}
				else {
					step.Type = Step::Kind::MouseMove;
					step.Act = Step::Action::Move;
				}
			}
			else if (const Json::Value* button = entry.FindMember("button")) {
				step.Type = Step::Kind::MouseButton;
				step.Position = Vec2{ -1.0f };
				step.Code = ParseMouseButton(button->AsStringOr());
				if (step.Code < 0 || !ReadAction(entry, step.Act, false)) {
					outError = where + "'button' must be left, right or middle; 'action' press, down or up";
					return false;
				}
			}
			else if (const Json::Value* touch = entry.FindMember("touch")) {
				step.Type = Step::Kind::Touch;
				if (!ReadPosition(*touch, step.Position)) {
					outError = where + "'touch' must be [x, y] in 0..1 of the window";
					return false;
				}
				if (const Json::Value* finger = entry.FindMember("finger")) step.Finger = finger->AsIntOr(0);
				if (!ReadAction(entry, step.Act, true)) {
					outError = where + "'action' must be tap, down, move or up";
					return false;
				}
			}
			else if (const Json::Value* command = entry.FindMember("command")) {
				step.Type = Step::Kind::Command;
				step.Text = command->AsStringOr();
			}
			else if (const Json::Value* screenshot = entry.FindMember("screenshot")) {
				step.Type = Step::Kind::Screenshot;
				step.Text = screenshot->IsString() ? screenshot->AsStringOr() : std::string();
			}
			else if (const Json::Value* log = entry.FindMember("log")) {
				step.Type = Step::Kind::Log;
				step.Text = log->AsStringOr();
			}
			else if (entry.FindMember("quit")) {
				step.Type = Step::Kind::Quit;
			}
			else {
				outError = where + "needs one of key, mouse, button, touch, command, screenshot, log or quit";
				return false;
			}
			out.Steps.push_back(std::move(step));
		}
		return true;
	}

	RunAutomation::Options RunAutomation::ParseOptions(int argc, const char* const* argv) {
		Options options;
		auto valueOf = [](std::string_view arg, std::string_view flag, std::string& out) {
			if (arg.size() <= flag.size() + 1 || arg.compare(0, flag.size(), flag) != 0 || arg[flag.size()] != '=') return false;
			out.assign(arg.substr(flag.size() + 1));
			if (out.size() >= 2 && out.front() == '"' && out.back() == '"') out = out.substr(1, out.size() - 2);
			return true;
		};
		for (int i = 1; i < argc; ++i) {
			if (!argv[i]) continue;
			const std::string_view arg(argv[i]);
			std::string value;
			if (arg == "--background") options.Background = true;
			else if (arg == "--sound") options.Sound = true;
			else if (arg == "--mute") options.Mute = true;
			// An empty --prefs-profile= turns a build's isolated profile off: that is a player's run.
			else if (valueOf(arg, "--prefs-profile", value)) options.PrefsProfile = !value.empty();
			else if (arg == "--prefs-profile" && i + 1 < argc && argv[i + 1] && *argv[i + 1]) options.PrefsProfile = true;
			else if (valueOf(arg, "--input-script", value)) options.InputScriptPath = value;
			else if (valueOf(arg, "--screenshot-dir", value)) options.ScreenshotDirectory = value;
			else if (valueOf(arg, "--exec", value)) options.Exec = value;
			else if (valueOf(arg, "--fixed-step", value)) {
				if (!ParseFixedStep(value, options.FixedStep)) FNK_CORE_WARN_TAG("Automation", "Ignoring invalid {}", arg);
			}
			else if (valueOf(arg, "--quit-at", value)) {
				Moment moment;
				if (ParseMoment(value, moment)) options.QuitAt = moment;
				else FNK_CORE_WARN_TAG("Automation", "Ignoring invalid {}", arg);
			}
			else if (valueOf(arg, "--screenshot-at", value)) {
				size_t start = 0;
				while (start <= value.size()) {
					const size_t comma = value.find(',', start);
					const std::string part = value.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
					Moment moment;
					if (ParseMoment(part, moment)) options.ScreenshotAt.push_back(moment);
					else if (!part.empty()) FNK_CORE_WARN_TAG("Automation", "Ignoring invalid screenshot time '{}'", part);
					if (comma == std::string::npos) break;
					start = comma + 1;
				}
			}
		}
		return options;
	}

	void RunAutomation::Configure(const Options& options) {
		Reset();
		g_State.Options = options;
		g_State.FixedStep = options.FixedStep;
		if (!options.ScreenshotDirectory.empty()) ScreenCapture::SetDirectory(options.ScreenshotDirectory);

		if (!options.InputScriptPath.empty()) {
			if (!File::Exists(options.InputScriptPath)) {
				FNK_CORE_ERROR_TAG("Automation", "Input script not found: {}", options.InputScriptPath);
			}
			else {
				Script script;
				std::string error;
				if (ParseScript(File::ReadAllText(options.InputScriptPath), script, error)) {
					g_State.Steps = std::move(script.Steps);
					if (g_State.FixedStep <= 0.0f) g_State.FixedStep = script.FixedStep;
				}
				else {
					FNK_CORE_ERROR_TAG("Automation", "Input script {}: {}", options.InputScriptPath, error);
				}
			}
		}
		for (const Moment& moment : options.ScreenshotAt) {
			Step step;
			step.Type = Step::Kind::Screenshot;
			step.At = moment;
			g_State.Steps.push_back(std::move(step));
		}
		if (options.QuitAt) {
			Step step;
			step.Type = Step::Kind::Quit;
			step.At = *options.QuitAt;
			g_State.Steps.push_back(std::move(step));
		}
		g_State.Done.assign(g_State.Steps.size(), false);
		g_State.Active = !g_State.Steps.empty() || g_State.FixedStep > 0.0f || !options.Exec.empty();
		g_State.Background = options.Background;
		if (g_State.Background) {
			SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0");
			SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, "0");
			FNK_CORE_INFO_TAG("Automation", "Background run: windows open behind the others and never take focus");
		}
		if (g_State.Active || g_State.Background) {
			// A fullscreen test run that loses focus (another window opens) would minimize, pause and stall its clock.
			SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
		}
		if (g_State.Active) {
			FNK_CORE_INFO_TAG("Automation", "{} scripted step(s), {}, screenshots in {}", g_State.Steps.size(),
				g_State.FixedStep > 0.0f ? fmt::format("fixed step {:.5f}s", g_State.FixedStep) : std::string("real-time steps"),
				ScreenCapture::GetDirectory());
		}
	}

	bool RunAutomation::IsActive() { return g_State.Active; }
	bool RunAutomation::IsBackground() { return g_State.Background; }
	bool RunAutomation::IsAutomatedRun() { return g_State.Active || g_State.Background || g_State.Options.PrefsProfile; }
	bool RunAutomation::IsMuted() { return g_State.Options.Mute || (IsAutomatedRun() && !g_State.Options.Sound); }
	float RunAutomation::GetFixedStep() { return g_State.FixedStepOverride > 0.0f ? g_State.FixedStepOverride : g_State.FixedStep; }
	double RunAutomation::GetClockSeconds() { return g_State.ClockSeconds; }
	int64_t RunAutomation::GetClockFrame() { return g_State.ClockFrame; }

	std::vector<RunAutomation::Step> RunAutomation::TakeDueSteps() {
		std::vector<Step> due;
		for (size_t i = 0; i < g_State.Steps.size(); ++i) {
			if (g_State.Done[i] || !IsDue(g_State.Steps[i].At)) continue;
			g_State.Done[i] = true;
			due.push_back(g_State.Steps[i]);
		}
		return due;
	}

	void RunAutomation::BeginFrame(bool gameStarted) {
		g_State.FrameActive = g_State.Active && gameStarted;
		if (!g_State.FrameActive) return;

		// A "press" holds for exactly one frame, so GetKeyDown and GetKey both see it.
		if (!g_State.Releases.empty()) {
			std::vector<PendingRelease> releases;
			releases.swap(g_State.Releases);
			if (SDL_Window* window = NativeWindow()) {
				for (const PendingRelease& release : releases) {
					switch (release.Type) {
					case Step::Kind::Key: PushKey(window, release.Code, false); break;
					case Step::Kind::MouseButton: PushMouseButton(window, release.Code, release.Position, false); break;
					case Step::Kind::Touch: PushTouch(window, release.Finger, release.Position, SDL_EVENT_FINGER_UP); break;
					default: break;
					}
				}
			}
		}
		if (!g_State.ExecRan) {
			g_State.ExecRan = true;
			if (!g_State.Options.Exec.empty()) DevConsole::Execute(g_State.Options.Exec);
		}
		for (const Step& step : TakeDueSteps()) {
			RunStep(step);
		}
	}

	void RunAutomation::EndFrame(float unscaledDeltaTime) {
		if (!g_State.FrameActive) return;
		g_State.ClockSeconds += unscaledDeltaTime;
		++g_State.ClockFrame;
	}

	void RunAutomation::Reset() {
		g_State = State{};
	}

} // namespace Funke
