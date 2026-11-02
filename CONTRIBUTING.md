# Contributing to Funke

Bug reports, documentation fixes and focused pull requests are welcome.

## Reporting bugs

Use the **Bug report** issue template. Attach the log from `%LOCALAPPDATA%\Funke\Logs\` and, for crashes, the dump from `%LOCALAPPDATA%\Funke\CrashDumps\`. Check [Known Limitations](Github-Docs/Documentation/Known-Limitations.md) first.

## Building from source

Follow **Getting Started** in the [README](README.md) for the initial setup. On Windows, use the incremental development scripts for the affected target:

```powershell
.\scripts\dev\Build.ps1 Engine -Config Release
.\scripts\dev\Test.ps1 -Suite Engine -Config Release -Filter "*RelevantTest*"
.\scripts\dev\Test.ps1 -Suite CSharp -Config Release
```

Choose `Editor`, `Runtime`, `Launcher`, `CLI`, `ScriptCore` or another supported build target when that is what changed. Select the matching test suite; replace `*RelevantTest*` with the affected tests, or omit the filter for the whole suite. Documentation-only changes need reference and example checks, not an engine build. `scripts/RunTests.bat --release` remains the broader native test entry point used for release verification.

On Linux and macOS, build the affected make target (for example `make config=release -j6 Funke-Engine-Tests`) and run its executable under `bin/Release-<system>-<arch>/`. Managed tests use `dotnet test Tests/Funke-ScriptCore-Tests/Funke-ScriptCore-Tests.csproj -c Release`.

Rerun `scripts/dawn/SetupDawn.bat` or `scripts/dawn/SetupDawn.sh` after an interrupted Dawn fetch or a pin change. Both validate the root revision and dependency revisions before CMake, resume repositories without HEAD/origin, and stop with the failing Git command if fetching fails. Unexpected source edits or remotes require manual reconciliation; setup never resets or cleans them. Versioned Funke patches are recognized separately. Windows keeps the smaller dependency selection; both configure CMake with dependency fetching disabled after the checked prefetch.

CI validates Dawn's `funke-artifacts.json`, required headers and libraries. Source files and Git metadata are intentionally absent from artifact caches. Invalid restored caches are preserved beside `External/dawn` before rebuilding; Windows caches must contain both Debug and Release libraries. Linux coverage uses atomic counters for the threaded engine and retains separate build, test, capture and report logs.

## Pull requests

- Keep each pull request to one change, and describe how you tested it.
- Follow the style of the surrounding code: PascalCase methods, `m_`/`s_`/`k_` member prefixes, short "why" comments instead of narration.
- Add or update tests where the change is testable, and update `Github-Docs/Documentation` plus `CHANGELOG.md` for user-visible changes.
- Regenerate Premake when the native source list or build graph changes. `scripts\dev\Build.ps1` detects unlisted native sources and changed generator Lua files; custom module workspaces require regeneration with their original options. `scripts\Setup.bat` is the initial dependency setup, not the normal regeneration command.

## License

Funke is licensed under the [MIT License](LICENSE.txt). By submitting a contribution you agree that it is licensed under the MIT License as well.
