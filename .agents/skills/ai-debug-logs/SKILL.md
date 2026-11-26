---
name: ai-debug-logs
description: "Find the latest Funke Editor, Launcher or Runtime log and crash dump for a reported failure, including rotated logs and per-editor sessions."
---

# Funke logs for agents

| What | Where |
|---|---|
| Windows log per executable | `%LOCALAPPDATA%\Funke\Logs\<exe>.log` (`Funke-Editor.log`, `Funke-Launcher.log`, `Funke-Runtime.log`, or the shipped game's exe name). Active file plus up to three rotated backups (`<exe>.1.log` through `<exe>.3.log`), 10 MiB each. Appends across runs. |
| Windows crash dumps | `%LOCALAPPDATA%\Funke\CrashDumps\<exe>_<FNK_BUILD_ID>_<timestamp>.dmp` (written by `Funke-Engine/src/Core/CrashReporter.cpp`) |
| Other native platforms | File logs use `Path::GetSpecialFolderPath(SpecialFolder::LocalAppData)/Funke/Logs`, backed by the platform's user-data root. Linux/macOS crash reports are `<exe>-<timestamp>.crash.txt` under its `Funke/CrashDumps` folder. |
| Android / Web | Android also writes logcat tag `Funke` (`adb logcat -s Funke`). Web uses the browser console; it deliberately creates no log file. |

Line format: `[HH:MM:SS] [<logger>] [<level>] [<Tag>] message` — logger is `FUNKE` (engine), `APP` (client) or `EDITOR`; level is `trace|debug|info|warning|error|critical`. Warn and above flush immediately; info may lag until shutdown.

## Isolate one session

Start at the last `Funke Engine <version>` or `Initializing...` marker. Search for `\] \[(warning|error|critical)\] ` and narrow by tag, such as `[SceneSerializer]`. For a fresh reproduction, `scripts\dev\Run.ps1` saves just that run in `bin-int\_agent\run\<App>.log` and lists new dumps. With multiple editors open, select the process with `scripts\dev\Editor.ps1 logs --level warning -Project <dir>`.
