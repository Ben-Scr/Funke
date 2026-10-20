#pragma once
#include "Core/Export.hpp"
#include "Collections/Vec2.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Funke {

	// Reproducible test runs of the runtime or a built game, driven from the command line:
	//   --input-script=<file.json>     timed keys, mouse, touch, console commands, screenshots, log lines, quit
	//   --screenshot-at=<t>[,<t>...]   game time ("3.5s", "3.5") or frame ("120f"); --screenshot-dir=<dir>
	//   --fixed-step=<fps>             every frame advances exactly 1/fps seconds, whatever the wall clock does
	//   --quit-at=<t>                  quit at a game time or frame
	//   --exec="<cmd>; <cmd>"          console commands on the first game frame
	//   --background                   never take focus: opens behind other windows, keeps running unfocused,
	//                                  fullscreen and maximize become a plain window filling the screen
	//   --sound / --mute               keep an automated run audible / silence any run
	// The clock starts on the first frame after the startup scenes loaded (after the splash).
	class FUNKE_API RunAutomation {
	public:
		struct Moment {
			double Seconds = -1.0; // game seconds, or
			int64_t Frame = -1;    // game frame; exactly one of the two is set
			std::string Text;      // as written, for screenshot names
		};

		struct Step {
			enum class Kind : uint8_t { Key, MouseButton, MouseMove, Touch, Command, Screenshot, Log, Quit };
			enum class Action : uint8_t { Press, Down, Up, Move };
			Kind Type = Kind::Command;
			Action Act = Action::Press;
			Moment At;
			int Code = 0;          // KeyCode, or mouse button (0 left, 1 right, 2 middle)
			Vec2 Position{ 0.0f }; // window position, normalized 0..1 from the top-left; negative = unset
			int64_t Finger = 0;
			std::string Text;      // command, screenshot file or log line
		};

		struct Script {
			std::vector<Step> Steps;
			float FixedStep = 0.0f; // seconds per frame, 0 = real time
		};

		struct Options {
			std::string InputScriptPath;
			std::vector<Moment> ScreenshotAt;
			std::string ScreenshotDirectory;
			float FixedStep = 0.0f;
			std::optional<Moment> QuitAt;
			std::string Exec;
			bool Background = false;
			bool PrefsProfile = false; // --prefs-profile=<name>: a run kept off the real saves
			bool Sound = false;
			bool Mute = false;
		};

		static bool ParseMoment(std::string_view text, Moment& out);
		static bool ParseScript(std::string_view json, Script& out, std::string& outError);
		// KeyCode value for a name ("Space", "A", "LeftShift", "Down", "F8", "1"); -1 when unknown.
		static int ParseKeyName(std::string_view name);
		// 0 left, 1 right, 2 middle; -1 when unknown.
		static int ParseMouseButton(std::string_view name);
		// ">= 1" reads as frames per second ("60"), below as seconds per frame ("0.0166").
		static bool ParseFixedStep(std::string_view text, float& outSecondsPerFrame);
		// Fixed frame step set from outside the command line (the Editor Bridge); 0 hands back to --fixed-step.
		static void SetFixedStepOverride(float secondsPerFrame);
		// Pulls the automation flags out of argv; unrelated arguments are ignored.
		static Options ParseOptions(int argc, const char* const* argv);

		// Application hooks. Configure reads the options and loads the script, logging problems.
		static void Configure(const Options& options);
		static bool IsActive();
		// --background: windows must neither take focus nor come to the front.
		static bool IsBackground();
		// Started by a test or an agent: any flag above, --background or --prefs-profile=<name> (C# Application.IsAutomatedRun).
		static bool IsAutomatedRun();
		// Automated runs are silent unless --sound; --mute silences any run.
		static bool IsMuted();
		static float GetFixedStep();
		// Before input polling: pushes this frame's synthetic input and runs its commands.
		static void BeginFrame(bool gameStarted);
		static void EndFrame(float unscaledDeltaTime);
		static void Reset();

		static double GetClockSeconds();
		static int64_t GetClockFrame();
		// Steps due this frame, in script order (also used by tests to drive the clock without a window).
		static std::vector<Step> TakeDueSteps();
	};

} // namespace Funke
