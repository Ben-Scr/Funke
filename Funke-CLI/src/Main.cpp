#include "pch.hpp"
#include "Commands.hpp"

#include "Core/Base.hpp"
#include "Core/Engine.hpp"
#include "Core/EngineAbi.hpp"
#include "Core/Log.hpp"
#include "Core/Version.hpp"

#include <array>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#ifdef FNK_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace Funke::Cli {

	namespace {
		constexpr std::array k_Commands{
			Command{ "create", "Create a new project",
R"(funke create <Name> [options]

Creates <path>/<Name> with the starter scene, scripts, build profiles and C# project, compiles the
scripts once and adds the project to the Launcher's project list.

  --path <dir>          Parent folder (default: ~/Funke/Projects)
  --dir <name>          Folder name when it should differ from <Name>
  --format json|binary  Scene/prefab format on disk (default: json)
  --no-git              Skip `git init` and .gitignore
  --no-compile          Skip the initial `dotnet build` of the scripts
  --no-register         Do not add the project to the Launcher list)", &RunCreate },

			Command{ "info", "Show project settings, scenes, build profiles and packages",
R"(funke info [--project <dir>] [--json])", &RunInfo },

			Command{ "build", "Build a playable game",
R"(funke build [options]

Runs the same pipeline as the editor's Build button, without the editor. project.json is not
modified; --debug/--development/--release and the chosen profile apply to this build only.
A profile's "configuration", "defines" and test switches apply unless overridden here.

  --project <dir>          Project root (default: the project containing the working directory)
  --release                Release build (Dist binaries, FUNKE_BUILD_RELEASE)
  --development            Development build (default: the profile's or Build panel setting)
  --debug                  Debug build (Debug binaries + PDBs, FUNKE_BUILD_DEBUG; needs engine sources)
  --platform <name>        Windows, Linux, MacOS, Android, iOS, Web: picks that platform's profile
  --profile <name>         Build profile (BuildProfiles/<name>.build)
  --output <dir>           Output folder (default: <project>/Builds/<profile>)
  --run                    Launch the built game afterwards (host desktop builds)
  --json                   Print the result as JSON)", &RunBuild },

			Command{ "run", "Compile the scripts and play the project in Funke-Runtime",
R"(funke run [options]

  --project <dir>    Project root
  --scene <name>     Start in this scene instead of the startup scene
  --seconds <n>      Quit after n seconds (smoke test); exit code reflects runtime errors
  --no-compile       Skip compiling the C# scripts
  --detach           Start the runtime and return immediately

Test runs (see Editor/Testing.md):
  --prefs-profile <name>   PlayerPrefs/SaveData in Profiles/<name>, never the real saves
  --input-script <file>    Timed keys, mouse, touch, console commands, screenshots, quit (JSON)
  --screenshot-at <t,...>  PNGs at game time ("2.5s") or frame ("120f")
  --screenshot-dir <dir>   Where screenshots go (default %LOCALAPPDATA%\Funke\Screenshots)
  --fixed-step <fps>       Every frame advances exactly 1/fps seconds (reproducible runs)
  --quit-at <t>            Quit at a game time or frame
  --exec "<cmd>; <cmd>"    Dev console commands on the first game frame)", &RunPlay },

			Command{ "open", "Open the project in the editor",
R"(funke open [--project <dir>])", &RunOpen },

			Command{ "editor", "Drive the running editor: play, input, screenshots, console, logs",
R"(funke editor <command> [options]

Talks to an editor that is already open, over a local channel only your user account can use.
Nothing on the desktop changes: no window is moved, focused or resized and no OS input is sent; the
editor may be in the background or minimized. The editor is the one with --project <dir> (or the
working directory's project) open, the only one running, or --pid <n>.

  status                        Project, open scene, edit/play/paused, script build state, surface
  list                          Running editors
  play [--scene <name>]         Enter Play mode and wait until it runs (compiles scripts first if needed)
       [--size WxH]             Lay the game out for exactly this screen, e.g. 1080x2400 (until Play stops)
       [--fixed-step <fps>]     Every frame advances exactly 1/fps seconds (until Play stops)
       [--seconds <s> | --frames <n>]   Then let the game run that long before returning
       [--save | --discard]     With --scene: what to do with unsaved scene changes
       [--no-wait]
  stop | pause | resume
  step [--frames <n>]           Run n frames, then pause
  wait (--seconds <s> | --frames <n>)   Game time in Play mode (pauses don't count), else real time
  exec "<console command>"      Dev console line (also [DevCommand]s); prints its output
  recompile [--no-wait]         Rebuild the C# scripts and report errors. In Play mode the editor
                                leaves Play first when "Exit Play Mode On Recompilation" is on
  screenshot <out.png> [--width W --height H | --size WxH]
                                Renders the game camera offscreen; default size: the game's surface
  input <script.json> [--fixed-step <fps>] [--screenshot-dir <dir>]
                                Runs an input script (same format as --input-script) in Play mode
  tap <x> <y> [--hold <s>]      Touch at 0..1 of the game surface (or pixels with --px)
  click <x> <y> [--button left|right|middle] [--hold <s>] [--px]
  drag <x1> <y1> <x2> <y2> [--duration <s>] [--mouse] [--px]
  key <name> [--hold <s>]       Space, A, Enter, Left, F1, ...
  open-scene <name|path.scene> [--save | --discard]
  save-scene
  select <entity name|id> | select --clear
                                Selects an entity like a Hierarchy click (Inspector, gizmos)
  surface <WxH> | surface --clear
  logs [--since <n>] [--level warning] [--tail <n>] [--contains <text>] [--follow]

  --timeout <s>   Give up after s seconds (each command has a sensible default)
  --json          The editor's answer as JSON on stdout (errors too)

Exit codes: 0 ok, 1 failed, 2 usage, 3 busy (loading, building, a dialog is open) or timed out,
4 no editor reachable.)", &RunEditor },

			Command{ "convert", "Convert scenes, prefabs and data assets between JSON and binary",
R"(funke convert --format json|binary [paths...] [options]

Without paths every .scene/.prefab/.dataasset under Assets/ is converted and project.json's
assetSerializationFormat is updated to match, so the editor keeps saving in that format.

  --project <dir>   Project root
  --keep-setting    Convert files but leave assetSerializationFormat unchanged)", &RunConvert },

			Command{ "validate", "Check that the engine accepts every project file",
R"(funke validate [--project <dir>] [--json] [--strict]

Loads project.json, build profiles, every .meta and every scene, prefab and data asset the way the
engine does and reports what it would reject or silently repair: unparsable files, duplicate or
missing GUIDs, dangling asset references, entities without a uuid, duplicate entity uuids, missing
build scenes. Read-only. Exit code 1 when errors were found (--strict: warnings too).)", &RunValidate },

			Command{ "assets", "Asset registry maintenance",
R"(funke assets refresh [--project <dir>]   Write a .meta (with a GUID) for every asset that lacks one
                                         and rebuild .funke/asset-registry.cache
funke assets list [--kind <kind>] [--json]
funke assets guid <path>                 Print an asset's GUID (creates its .meta if missing))", &RunAssets },

			Command{ "sprite", "Slice sprite sheets",
R"(funke sprite slice <texture> (--grid WxH | --count CxR | --alpha) [options]

Writes the slices into the texture's .meta, exactly like the Sprite Editor's slice buttons.

  --grid WxH         Fixed cell size in pixels
  --count CxR        Columns x rows; the cell size is derived from the texture size
  --alpha            One slice per opaque island (flood fill)
  --padding X[,Y]    Gap between cells (grid/count)
  --offset X[,Y]     Start of the first cell (grid)
  --threshold <a>    Alpha that counts as opaque, 0-255 (alpha, default 1)
  --min-size <px>    Drop islands smaller than this (alpha, default 1)
  --pivot X,Y        Pivot for every new slice (default 0.5,0.5)
  --name <stem>      Slice name prefix (default: the texture's file name)
  --keep             Append to the existing slices instead of replacing them
  --dry-run          Print the slices without writing

funke sprite list <texture> [--json]
funke sprite border <texture> --border L,B,R,T [--slice <name>]
                   9-slice border in pixels for the whole texture or one slice
                   (one value sets all four; 0 clears it). Sliced/Tiled Images use it
                   when their own border is 0.)", &RunSprite },

			Command{ "prefab", "Create, apply, revert and unpack prefab instances in a scene",
R"(funke prefab create <scene> <entity> <out.prefab>   Save an entity subtree as a prefab and turn it
                                                   into an instance of it
funke prefab apply  <scene> <entity>               Write an instance's overrides into its prefab
funke prefab revert <scene> <entity> [--path <p>]  Drop all overrides, or one (e.g. Transform2D.posX)
funke prefab unpack <scene> <entity>               Turn an instance back into plain scene entities
funke prefab list   <scene>                        List the prefab instances and their overrides

<scene> is a scene name or a .scene path; <entity> is a uuid, a name, or a path like Parent/Child.
The scene is saved in the format it already has. Close the scene in the editor first, or let the
editor reload it when it notices the change.)", &RunPrefab },

			Command{ "package", "Add, remove and list packages and engine modules",
R"(funke package list [--json]                packages plus the engine modules (Funke.2D, Funke.3D)
funke package add <name | path | git-url | nuget:Id[@version] | Funke.2D | Funke.3D>
funke package remove <name | Funke.2D | Funke.3D> [--delete]     --delete also removes Packages/<name>/
funke package refresh                      Regenerate Packages/FunkePackages.props from project.json)", &RunPackage },

			Command{ "setup-path", "Add this tool's folder to your user PATH",
R"(funke setup-path [--remove]

Windows: adds the folder containing funke.exe to the current user's PATH (HKCU\Environment).
Linux / macOS: installs an `funke` launcher script in ~/.local/bin.
Either way `funke` works in any new terminal. --remove takes it out again. Nothing outside your user
account changes.)", &RunSetupPath },
		};

		void PrintGeneralHelp() {
			std::string text = std::string(Engine::GetVersionLong()) + " command-line tool\n\n"
				"usage: funke <command> [arguments] [--project <dir>] [--json] [--verbose] [--quiet]\n\n"
				"commands:\n";
			for (const Command& command : k_Commands) {
				std::string name(command.Name);
				name.resize(12, ' ');
				text += "  " + name + std::string(command.Summary) + "\n";
			}
			text += "\n`funke help <command>` shows a command's options.";
			Out(text);
		}

		std::vector<std::string> CollectArguments(int argc, char** argv) {
			std::vector<std::string> arguments;
#ifdef FNK_PLATFORM_WINDOWS
			// argv is in the ANSI code page; paths with non-ASCII characters need the wide command line.
			int wideCount = 0;
			if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &wideCount)) {
				for (int i = 1; i < wideCount; ++i) {
					const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
					std::string utf8(bytes > 0 ? static_cast<std::size_t>(bytes - 1) : 0, '\0');
					if (bytes > 1) WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, utf8.data(), bytes, nullptr, nullptr);
					arguments.push_back(std::move(utf8));
				}
				LocalFree(wide);
				return arguments;
			}
#endif
			for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
			return arguments;
		}
	}

	std::span<const Command> AllCommands() { return k_Commands; }

	const Command* FindCommand(std::string_view name) {
		for (const Command& command : k_Commands) {
			if (command.Name == name) return &command;
		}
		return nullptr;
	}

	int Main(int argc, char** argv) {
		Args args(CollectArguments(argc, argv));
		Options().Json = args.Flag("json");
		Options().Verbose = args.Flag("verbose", "v");
		Options().Quiet = args.Flag("quiet", "q");
		Options().ProjectRoot = args.Option("project", "p");
		const bool help = args.Flag("help", "h");
		// stderr only: --json output on stdout stays parseable.
		ReportEngineAbiMismatch("funke.exe");

		if (args.Flag("version")) {
			Out(std::string(Engine::GetVersionLong()));
			return k_ExitOk;
		}

		std::optional<std::string> commandName = args.Positional();
		if (commandName && *commandName == "help") {
			commandName = args.Positional();
			if (!commandName) {
				PrintGeneralHelp();
				return k_ExitOk;
			}
			const Command* command = FindCommand(*commandName);
			if (!command) return Fail("unknown command '" + *commandName + "'", k_ExitUsage);
			Out(command->Usage);
			return k_ExitOk;
		}
		if (!commandName) {
			PrintGeneralHelp();
			return help ? k_ExitOk : k_ExitUsage;
		}

		const Command* command = FindCommand(*commandName);
		if (!command) return Fail("unknown command '" + *commandName + "' (see `funke help`)", k_ExitUsage);
		if (help) {
			Out(command->Usage);
			return k_ExitOk;
		}

		// Redirect logging before InitializeCore so its startup lines don't land on stdout.
		Log::Initialize();
		ConfigureEngineLogging();
		InitializeCore();
		int exitCode = k_ExitFailure;
		try {
			exitCode = command->Run(args);
		}
		catch (const std::exception& exception) {
			exitCode = Fail(exception.what());
		}
		ShutdownCore();
		return exitCode;
	}

} // namespace Funke::Cli

int main(int argc, char** argv) {
	return Funke::Cli::Main(argc, argv);
}
