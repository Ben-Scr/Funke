#include <doctest/doctest.h>

#include "Core/KeyCode.hpp"
#include "Core/Time.hpp"
#include "DevTools/DevConsole.hpp"
#include "DevTools/RunAutomation.hpp"
#include "DevTools/ScreenCapture.hpp"
#include "Project/IndexBuildProfile.hpp"
#include "Project/IndexProject.hpp"
#include "Serialization/File.hpp"
#include "Serialization/Path.hpp"

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace Index;

namespace {
	struct ConsoleReset {
		ConsoleReset() { DevConsole::ResetForTests(); }
		~ConsoleReset() { DevConsole::ResetForTests(); }
	};

	struct TempDir {
		TempDir() {
			Root = std::filesystem::temp_directory_path()
				/ ("IndexDevTools-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
			std::filesystem::create_directories(Root);
		}
		~TempDir() {
			std::error_code ec;
			std::filesystem::remove_all(Root, ec);
		}
		std::filesystem::path Root;
	};

	std::vector<std::string> OutputLines() {
		std::vector<std::string> lines;
		for (const DevConsole::HistoryEntry& entry : DevConsole::GetHistory()) {
			if (entry.Type == DevConsole::HistoryEntry::Kind::Output) lines.push_back(entry.Text);
		}
		return lines;
	}
}

TEST_CASE("DevConsole tokenizes quoted words and splits commands outside quotes") {
	CHECK(DevConsole::Tokenize("  teleport  Goal ") == std::vector<std::string>{ "teleport", "Goal" });
	CHECK(DevConsole::Tokenize("say \"hello world\" now") == std::vector<std::string>{ "say", "hello world", "now" });
	CHECK(DevConsole::Tokenize("say \"a \\\"quoted\\\" word\"") == std::vector<std::string>{ "say", "a \"quoted\" word" });
	CHECK(DevConsole::Tokenize("set \"\"") == std::vector<std::string>{ "set", "" });

	CHECK(DevConsole::SplitCommands("timescale 0.5; echo hi ;; ") == std::vector<std::string>{ "timescale 0.5", "echo hi" });
	CHECK(DevConsole::SplitCommands("echo \"a;b\"; quit") == std::vector<std::string>{ "echo \"a;b\"", "quit" });

	bool value = false;
	CHECK((DevConsole::ParseBool("ON", value) && value));
	CHECK((DevConsole::ParseBool("0", value) && !value));
	CHECK_FALSE(DevConsole::ParseBool("maybe", value));
}

TEST_CASE("DevConsole runs native commands, reports unknown ones and keeps a history") {
	ConsoleReset reset;
	std::vector<std::string> received;
	DevConsole::Register("Coins.Add", "<amount>", "Adds coins", [&](const std::vector<std::string>& args) {
		received = args;
		if (args.empty()) return DevCommandResult::Error("Missing amount.");
		return DevCommandResult::Ok("Coins +" + args[0]);
	});

	// Names are case-insensitive.
	DevCommandResult result = DevConsole::Execute("coins.add 50");
	CHECK(result.Result == DevCommandResult::Status::Ok);
	CHECK(result.Output == "Coins +50");
	CHECK(received == std::vector<std::string>{ "50" });

	result = DevConsole::Execute("COINS.ADD");
	CHECK(result.Result == DevCommandResult::Status::Error);

	result = DevConsole::Execute("nope 1");
	CHECK(result.Result == DevCommandResult::Status::NotFound);
	CHECK(result.Output.find("nope") != std::string::npos);

	// The status is the first failure, the output joins every command.
	result = DevConsole::Execute("coins.add 1; nope; coins.add 2");
	CHECK(result.Result == DevCommandResult::Status::NotFound);
	CHECK(result.Output.find("Coins +1") != std::string::npos);
	CHECK(result.Output.find("Coins +2") != std::string::npos);

	const std::vector<DevConsole::HistoryEntry> history = DevConsole::GetHistory();
	REQUIRE(history.size() >= 2);
	CHECK(history.front().Type == DevConsole::HistoryEntry::Kind::Input);
	CHECK(history.front().Text == "> coins.add 50");

	DevConsole::Unregister("coins.add");
	CHECK_FALSE(DevConsole::HasNativeCommand("coins.add"));
}

TEST_CASE("DevConsole built-ins: echo, timescale, physics.debug, help, clear") {
	ConsoleReset reset;
	DevConsole::RegisterBuiltInCommands();
	DevConsole::RegisterBuiltInCommands(); // idempotent

	CHECK(DevConsole::Execute("echo hello \"big world\"").Output == "hello big world");

	// No Application here, so Time ignores the value; the command still parses and reports.
	const float previousScale = Time::GetTimeScale();
	const DevCommandResult scale = DevConsole::Execute("timescale 0.5");
	CHECK(scale.Result == DevCommandResult::Status::Ok);
	CHECK(scale.Output.rfind("timescale ", 0) == 0);
	CHECK(DevConsole::Execute("timescale fast").Result == DevCommandResult::Status::Error);
	Time::SetTimeScale(previousScale);

	CHECK(DevConsole::Execute("physics.debug on").Output == "physics.debug on");
	CHECK(DevConsole::Execute("physics.debug").Output == "physics.debug off");
	CHECK(DevConsole::Execute("physics.debug sideways").Result == DevCommandResult::Status::Error);

	const std::string help = DevConsole::Execute("help physics").Output;
	CHECK(help.find("physics.debug") != std::string::npos);
	CHECK(help.find("timescale") == std::string::npos);

	DevConsole::Execute("clear");
	CHECK(OutputLines().empty());
}

TEST_CASE("DevConsole open state follows availability") {
	ConsoleReset reset;
	DevConsole::SetOpen(true);
	CHECK_FALSE(DevConsole::IsOpen());
	DevConsole::SetEnabled(true);
	DevConsole::SetOpen(true);
	CHECK(DevConsole::IsOpen());
	DevConsole::SetEnabled(false);
	CHECK_FALSE(DevConsole::IsOpen());
}

TEST_CASE("RunAutomation parses times, frames, keys and mouse buttons") {
	RunAutomation::Moment moment;
	REQUIRE(RunAutomation::ParseMoment("3.5s", moment));
	CHECK(moment.Seconds == doctest::Approx(3.5));
	CHECK(moment.Frame == -1);
	REQUIRE(RunAutomation::ParseMoment("2", moment));
	CHECK(moment.Seconds == doctest::Approx(2.0));
	REQUIRE(RunAutomation::ParseMoment("250ms", moment));
	CHECK(moment.Seconds == doctest::Approx(0.25));
	REQUIRE(RunAutomation::ParseMoment("120f", moment));
	CHECK(moment.Frame == 120);
	CHECK(moment.Text == "120f");
	CHECK_FALSE(RunAutomation::ParseMoment("-1s", moment));
	CHECK_FALSE(RunAutomation::ParseMoment("1.5f", moment));
	CHECK_FALSE(RunAutomation::ParseMoment("soon", moment));

	CHECK(RunAutomation::ParseKeyName("Space") == static_cast<int>(KeyCode::Space));
	CHECK(RunAutomation::ParseKeyName("d") == static_cast<int>(KeyCode::D));
	CHECK(RunAutomation::ParseKeyName("7") == static_cast<int>(KeyCode::Alpha7));
	CHECK(RunAutomation::ParseKeyName("Down") == static_cast<int>(KeyCode::Bottom));
	CHECK(RunAutomation::ParseKeyName("F8") == static_cast<int>(KeyCode::F8));
	CHECK(RunAutomation::ParseKeyName("LeftShift") == static_cast<int>(KeyCode::LeftShift));
	CHECK(RunAutomation::ParseKeyName("F26") == -1);
	CHECK(RunAutomation::ParseKeyName("Banana") == -1);

	CHECK(RunAutomation::ParseMouseButton("Left") == 0);
	CHECK(RunAutomation::ParseMouseButton("right") == 1);
	CHECK(RunAutomation::ParseMouseButton("thumb") == -1);
}

TEST_CASE("RunAutomation reads input scripts and rejects broken steps with their number") {
	RunAutomation::Script script;
	std::string error;
	REQUIRE(RunAutomation::ParseScript(R"({
		"fixedStep": 50,
		"steps": [
			{ "at": "0.5s", "key": "D", "action": "down" },
			{ "at": 1.5, "key": "D", "action": "up" },
			{ "frame": 90, "key": "Space" },
			{ "at": "2s", "mouse": [0.5, 0.25], "button": "left" },
			{ "at": "2s", "mouse": [0.6, 0.25] },
			{ "at": "3s", "touch": [0.1, 0.9], "finger": 2, "action": "move" },
			{ "at": "4s", "command": "teleport Goal" },
			{ "at": "4s", "screenshot": "goal.png" },
			{ "at": "4s", "log": "at the goal" },
			{ "at": "5s", "quit": true }
		]
	})", script, error));
	CHECK(script.FixedStep == doctest::Approx(0.02f));
	REQUIRE(script.Steps.size() == 10);
	using Kind = RunAutomation::Step::Kind;
	using Action = RunAutomation::Step::Action;
	CHECK(script.Steps[0].Type == Kind::Key);
	CHECK(script.Steps[0].Act == Action::Down);
	CHECK(script.Steps[0].Code == static_cast<int>(KeyCode::D));
	CHECK(script.Steps[1].At.Seconds == doctest::Approx(1.5));
	CHECK(script.Steps[2].At.Frame == 90);
	CHECK(script.Steps[2].Act == Action::Press);
	CHECK(script.Steps[3].Type == Kind::MouseButton);
	CHECK(script.Steps[3].Position.y == doctest::Approx(0.25f));
	CHECK(script.Steps[4].Type == Kind::MouseMove);
	CHECK(script.Steps[5].Type == Kind::Touch);
	CHECK(script.Steps[5].Act == Action::Move);
	CHECK(script.Steps[5].Finger == 2);
	CHECK(script.Steps[6].Text == "teleport Goal");
	CHECK(script.Steps[7].Type == Kind::Screenshot);
	CHECK(script.Steps[8].Type == Kind::Log);
	CHECK(script.Steps[9].Type == Kind::Quit);

	CHECK_FALSE(RunAutomation::ParseScript(R"({ "steps": [ { "at": "1s", "key": "Banana" } ] })", script, error));
	CHECK(error.find("step 1") != std::string::npos);
	CHECK_FALSE(RunAutomation::ParseScript(R"({ "steps": [ { "command": "quit" } ] })", script, error));
	CHECK(error.find("'at'") != std::string::npos);
	CHECK_FALSE(RunAutomation::ParseScript(R"({ "steps": [ { "at": "1s" } ] })", script, error));
	CHECK_FALSE(RunAutomation::ParseScript(R"({ "nope": [] })", script, error));
}

TEST_CASE("RunAutomation pulls its flags out of the command line") {
	const char* argv[] = {
		"Game.exe", "--project=C:/Game", "--screenshot-at=2s,120f,bad", "--fixed-step=60",
		"--quit-at=10s", "--exec=\"physics.debug on; timescale 2\"", "--input-script=run.json", "--screenshot-dir=out",
	};
	const RunAutomation::Options options = RunAutomation::ParseOptions(static_cast<int>(std::size(argv)), argv);
	REQUIRE(options.ScreenshotAt.size() == 2);
	CHECK(options.ScreenshotAt[0].Seconds == doctest::Approx(2.0));
	CHECK(options.ScreenshotAt[1].Frame == 120);
	CHECK(options.FixedStep == doctest::Approx(1.0f / 60.0f));
	REQUIRE(options.QuitAt.has_value());
	CHECK(options.QuitAt->Seconds == doctest::Approx(10.0));
	CHECK(options.Exec == "physics.debug on; timescale 2");
	CHECK(options.InputScriptPath == "run.json");
	CHECK(options.ScreenshotDirectory == "out");
}

TEST_CASE("RunAutomation fires steps on the game clock, presses for one frame and runs --exec first") {
	ConsoleReset reset;
	DevConsole::RegisterBuiltInCommands();
	TempDir temp;
	const std::filesystem::path scriptPath = temp.Root / "run.json";
	REQUIRE(File::WriteAllText(Path::ToUtf8(scriptPath), R"({
		"fixedStep": 10,
		"steps": [
			{ "at": "0.25s", "command": "echo quarter" },
			{ "frame": 1, "command": "echo frame-one" },
			{ "at": 0, "command": "echo start" }
		]
	})"));

	RunAutomation::Options options;
	options.InputScriptPath = Path::ToUtf8(scriptPath);
	options.Exec = "echo exec";
	RunAutomation::Configure(options);
	CHECK(RunAutomation::IsActive());
	CHECK(RunAutomation::GetFixedStep() == doctest::Approx(0.1f));

	// Nothing runs before the game started (splash, loading).
	RunAutomation::BeginFrame(false);
	RunAutomation::EndFrame(0.1f);
	CHECK(OutputLines().empty());
	CHECK(RunAutomation::GetClockFrame() == 0);

	std::vector<std::vector<std::string>> perFrame;
	for (int frame = 0; frame < 4; ++frame) {
		DevConsole::ClearHistory();
		RunAutomation::BeginFrame(true);
		perFrame.push_back(OutputLines());
		RunAutomation::EndFrame(RunAutomation::GetFixedStep());
	}
	CHECK(perFrame[0] == std::vector<std::string>{ "exec", "start" });
	CHECK(perFrame[1] == std::vector<std::string>{ "frame-one" });
	CHECK(perFrame[2].empty());
	CHECK(perFrame[3] == std::vector<std::string>{ "quarter" }); // 0.3 s >= 0.25 s
	CHECK(RunAutomation::GetClockSeconds() == doctest::Approx(0.4));
	RunAutomation::Reset();
	CHECK_FALSE(RunAutomation::IsActive());
}

TEST_CASE("ScreenCapture resolves bare names into its folder and adds .png") {
	TempDir temp;
	const std::string previous = ScreenCapture::GetDirectory();
	ScreenCapture::SetDirectory(Path::ToUtf8(temp.Root));
	CHECK(ScreenCapture::ResolvePath("goal") == Path::ToUtf8((temp.Root / "goal.png").lexically_normal()));
	CHECK(ScreenCapture::ResolvePath("shots/a.png") == Path::ToUtf8((temp.Root / "shots" / "a.png").lexically_normal()));
	const std::string absolute = Path::ToUtf8((temp.Root / "abs.png").lexically_normal());
	CHECK(ScreenCapture::ResolvePath(absolute) == absolute);

	const std::vector<uint8_t> pixels = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };
	CHECK(ScreenCapture::WritePng(absolute, 2, 2, pixels));
	CHECK(std::filesystem::file_size(Path::FromUtf8(absolute)) > 8);
	CHECK_FALSE(ScreenCapture::WritePng(absolute, 4, 4, pixels));
	ScreenCapture::SetDirectory(previous);
}

TEST_CASE("Build profiles round-trip the configuration, defines and test switches") {
	TempDir temp;
	IndexBuildProfile profile;
	profile.Name = "QA Build";
	profile.Configuration = IndexProject::BuildProfile::Debug;
	profile.Defines = { "INDEX_TEST", "DEMO" };
	profile.ShowRuntimeStats = false;
	profile.DevConsole = true;
	profile.IsolatedPrefs = true;
	const std::string path = Path::ToUtf8(temp.Root / "QA.build");
	REQUIRE(profile.Save(path));

	const IndexBuildProfile loaded = IndexBuildProfile::Load(path);
	REQUIRE(loaded.Configuration.has_value());
	CHECK(*loaded.Configuration == IndexProject::BuildProfile::Debug);
	CHECK(loaded.Defines == std::vector<std::string>{ "INDEX_TEST", "DEMO" });
	CHECK(loaded.ShowRuntimeStats == false);
	CHECK_FALSE(loaded.ShowRuntimeLogs.has_value());
	CHECK(loaded.DevConsole == true);
	CHECK(loaded.IsolatedPrefs);
	CHECK(loaded.GetPrefsProfileName() == "QA_Build");

	// Unset fields stay out of the file, so old profiles keep following project.json.
	IndexBuildProfile plain;
	plain.Name = "Plain";
	const std::string plainPath = Path::ToUtf8(temp.Root / "Plain.build");
	REQUIRE(plain.Save(plainPath));
	const std::string text = File::ReadAllText(plainPath);
	CHECK(text.find("configuration") == std::string::npos);
	CHECK(text.find("isolatedPrefs") == std::string::npos);

	IndexProject project;
	project.CustomDefines = { "DEMO", "STEAM" };
	loaded.ApplyTo(project);
	CHECK(project.ActiveBuildProfile == IndexProject::BuildProfile::Debug);
	CHECK(project.CustomDefines == std::vector<std::string>{ "DEMO", "STEAM", "INDEX_TEST" });
	CHECK(project.ShowRuntimeStats == false);
	CHECK(project.DevConsole == true);
	CHECK(project.PrefsProfile == "QA_Build");
}

TEST_CASE("Debug configuration, dev console and prefs profile in project.json") {
	CHECK(IndexProject::BuildProfileFromString("Debug") == IndexProject::BuildProfile::Debug);
	CHECK(std::string(IndexProject::BuildProfileToString(IndexProject::BuildProfile::Debug)) == "Debug");
	CHECK(IndexProject::GetBuildConfigurationFor(IndexProject::BuildProfile::Debug) == "Debug");
	CHECK(IndexProject::GetBuildConfigurationFor(IndexProject::BuildProfile::Release) == "Dist");
	CHECK(IndexProject::GetBuildConfigurationFor(IndexProject::BuildProfile::Development) == IndexProject::GetActiveBuildConfiguration());

	IndexProject project;
	project.ActiveBuildProfile = IndexProject::BuildProfile::Debug;
	CHECK(project.GetActiveBuildProfileDefine() == "INDEX_BUILD_DEBUG");
	CHECK(project.ResolveDevConsole());
	CHECK(project.ResolveShowRuntimeStats());
	project.ActiveBuildProfile = IndexProject::BuildProfile::Release;
	CHECK_FALSE(project.ResolveDevConsole());
	project.DevConsole = true;
	CHECK(project.ResolveDevConsole());

	CHECK(IndexProject::SanitizePrefsProfileName("../QA run") == "_QA_run");
	CHECK(IndexProject::SanitizePrefsProfileName("agent") == "agent");

	TempDir temp;
	std::filesystem::create_directories(temp.Root / "Assets");
	IndexProject saved;
	saved.Name = "DevTools";
	saved.RootDirectory = Path::ToUtf8(temp.Root);
	saved.ProjectFilePath = Path::ToUtf8(temp.Root / "project.json");
	saved.ActiveBuildProfile = IndexProject::BuildProfile::Debug;
	saved.DevConsole = false;
	saved.PrefsProfile = "QA";
	REQUIRE(saved.Save());
	const IndexProject loaded = IndexProject::Load(Path::ToUtf8(temp.Root));
	CHECK(loaded.ActiveBuildProfile == IndexProject::BuildProfile::Debug);
	CHECK(loaded.DevConsole == false);
	CHECK(loaded.PrefsProfile == "QA");
}
