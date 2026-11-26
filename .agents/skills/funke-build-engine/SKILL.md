---
name: funke-build-engine
description: "Compile, test or launch the Funke engine, Editor, Runtime or Launcher. Covers local dev scripts, Premake, build errors and fresh setup."
---

# Build, test and run Funke

On Windows, run the development scripts from the repository root. Match the configuration of the app under test. The scripts default to `Release`.

| What needs verification | Command or check |
|---|---|
| Engine implementation or ABI | `.\scripts\dev\Build.ps1 Engine`. For a public contract change, also link the affected consumer. |
| Editor, Runtime, Launcher or CLI | Pass the affected project name to `Build.ps1`. For a managed-only change, use `ScriptCore` or `ScriptCompiler` as appropriate. |
| Native or managed behavior | `.\scripts\dev\Test.ps1 -Suite Engine -Filter "*Prefab*"`. Select `Engine`, `Editor` or `CSharp` and substitute the relevant filter. The script builds the suite first. Use `-NoBuild` only when its binaries are current. |
| Visible or runtime behavior | `.\scripts\dev\Run.ps1` opens a sandbox Editor in the background and captures a screenshot and a run log. Inspect the evidence for the reported behavior. |
| Skills or documentation | Validate the changed instructions, paths and examples directly. |

Preserve outputs and the active Premake options. `Build.ps1` can regenerate a default VS2022 workspace. For a custom configuration, regenerate with its original options when the graph changes, then use `-NoRegenCheck` on a current graph. Use the generated variant or package workspace for those targets. The dev scripts deploy updated DLLs and keep full diagnostics in `bin-int\_agent`.

- For build targets, project regeneration, CI commands or compiler/linker errors, read [Windows builds and tests](references/windows-builds.md).
- For Play-mode checks, saved-game isolation, scripted input, game-time screenshots or driving one open editor, read [runtime verification](references/runtime-verification.md).
- For a fresh checkout, Linux, macOS, WSL or release setup, read [setup and platforms](references/setup-and-platforms.md).
