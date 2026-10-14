---
name: ai-debug-logs
description: Read the Index engine's own log files and crash dumps instead of asking the user to paste console output. Use when the user reports a bug, crash or unexpected behaviour in the Editor, Launcher or a shipped Runtime — "check the logs", "the editor crashed", "I see a warning but can't read the console", "what went wrong". For a full debugging workflow see `index-debug`; to launch an app and capture its log yourself see `index-build-engine` (scripts/dev/Run.ps1).
---

# Index logs for agents

The old Axiom JSONL logs (`.axiom/logs`, `AXIOM_AI_LOGS`) no longer exist. Index writes plain text:

| What | Where |
|---|---|
| Log per executable | `%LOCALAPPDATA%\Index\Logs\<exe>.log` (`Index-Editor.log`, `Index-Launcher.log`, `Index-Runtime.log`, or the shipped game's exe name). Rotating, 10 MB × 3. Appends across runs. |
| Crash dumps | `%LOCALAPPDATA%\Index\CrashDumps\<exe>_<IDX_BUILD_ID>_<timestamp>.dmp` (written by `Index-Engine/src/Core/CrashReporter.cpp`) |

Line format: `[HH:MM:SS] [<logger>] [<level>] [<Tag>] message` — logger is `INDEX` (engine), `APP` (client) or `EDITOR`; level is `trace|debug|info|warning|error|critical`. Warn and above flush immediately; info may lag until shutdown.

## Recipes

- Problems only: Grep `\] \[(warning|error|critical)\] ` in the log file.
- One subsystem: Grep `\[Tag\]`, e.g. `\[AssetRegistry\]`, `\[SceneSerializer\]`, `\[ScriptSystem\]`.
- Only the latest session: start from the last `Index Engine <version>` / `Initializing...` line.
- Reproduce and capture cleanly: `scripts\dev\Run.ps1` — it records only the run's lines into `bin-int\_agent\run\<App>.log` and lists new crash dumps.

Read logs proactively when the user reports a runtime bug or after you changed code and need proof a path executed. Not needed for pure code questions.
