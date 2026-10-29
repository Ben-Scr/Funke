# Verify a standalone Windows installation

`scripts/ci/standalone_sdk_test.py` exercises a binary installation without building the engine. Supply the installed **core, templates and CLI components**, a .NET 10 SDK, and Python 3.10 or newer. Templates are optional in the SDK contract but required by this project-creation test. The harness imports only Python's standard library.

```powershell
python scripts/ci/standalone_sdk_test.py `
  --install 'C:\TestPayload\Funke' `
  --dotnet 'C:\Program Files\dotnet\dotnet.exe' `
  --work-parent 'C:\Temp\StandaloneChecks' `
  --forbidden-root 'C:\source\Funke' `
  --report 'C:\Temp\StandaloneChecks\evidence.json'
```

The work parent must be outside the installation and checkout. The harness also detects the checkout containing its own script when engine sources are present. Repeat `--forbidden-root` for other source checkouts that must not appear in generated build references. A sparse CI checkout needs only this script, the binary payload artifact and the SDK/Python prerequisites.

The full gate:

1. Validates the binary SDK manifest and required payload. It rejects native SDK/source trees, ScriptCore sources or its `.csproj`, embedded native build tools, symlinks and reparse points. The expected profiles are `Development -> Runtime-Development` and `Release -> Runtime-Dist`.
2. Copies the installation outside the checkout into a temporary path containing an umlaut. Child processes use an unrelated working directory, a private user-data root, empty NuGet sources/cache and a reduced PATH. Inherited Funke/toolchain environment overrides are discarded. Engine versions running this test must support the explicit `FUNKE_USER_DATA_ROOT` isolation override. The copied Launcher must complete `--ci-smoke-test --background` with its stable-frame marker and exit code zero.
3. Creates a project with `--no-git --no-register`. A local `Standalone.ManagedProbe` C# package is always authored and enabled through `funke package add`. The shipped Premake executable generates its workspace, then the .NET SDK compiles those generated C# projects for the Editor. No registry download is needed. `--with-tween` additionally installs and references the shipped Tween component; this option fails if Tween was not included in the input payload. This explicit authoring build is necessary: `package add` and `package refresh` activate packages and update references, but do not compile newly authored source packages; the Editor's startup rebuild currently targets stale native packages. The harness does not claim automatic source-package compilation on installation.
4. Starts its own background Editor and addresses its bridge by PID. Two successive script revisions must compile and run for 20 frames, producing different expected markers from `OnUpdate`. The marker also requires a call into the managed package. The test stops Play mode and captures an offscreen screenshot. It never sends desktop input to other applications.
5. Exports Development and Release, checks generated references, and copies each result into a second clean directory. Before either player starts, the test renames its installation and project so their original absolute paths disappear.
6. Runs both copied players with the SDK removed from PATH and `DOTNET_ROOT*` pointing to a nonexistent directory. Both exports must contain a private `dotnet/host/fxr` layout and execute the updated script plus package. Markers must identify the correct Development/Release configuration. The harness then restores its own installation/project paths.

Native-tool traps cover `git`, `cmake`, `ninja`, native `msbuild`, C/C++ compilers, `make`, `nmake` and `vswhere`. They record attempted invocations and fail with exit code 97. Their apphosts have a private runtime so the traps also work during the SDK-free player phase. The shipped Premake generator and the .NET SDK's managed MSBuild are allowed; neither is evidence that native engine compilation occurred.

Every command has a bounded timeout (`--timeout`, default 600 seconds; player timeout 90 seconds). JSON evidence contains phases, exit codes, timings, log paths, input hashes, manifest data and player markers. Work directories and logs are retained for diagnosis. Exit codes are **0** for the complete gate passing, **1** for failure, and **2** for a successful partial diagnostic using `--skip-editor`. A partial run must not satisfy a release gate.

## What a host run proves

A successful host run demonstrates the copied payload's tested create/package/edit/play/export/player workflow under a restricted environment. It is labeled `host-limited`: PATH traps do not intercept absolute executable paths, Windows registry discovery, or arbitrary reads from other host folders. Renaming the copied project/installation does not make the original input payload or every repository on the host inaccessible. Consequently this result alone does **not** prove operation on a machine without a repository or native toolchain.

CI can run this gate on a fresh runner with a sparse checkout and only downloaded binary artifacts. This removes the engine source checkout from that job, but runner-installed Visual Studio remains a limitation. Preserve the JSON and logs even when the gate fails.

## Windows Sandbox release check

The separate recipe creates a stronger environment with no repository or native toolchain mapped into the guest. It does not enable Windows features, install virtualization or claim that creating a recipe executed it. Windows Sandbox must already be available; virtual GPU support must also be sufficient for the engine's renderer.

Provide a **complete relocatable Python installation** and an existing .NET 10 SDK directory. The recipe copies these dependencies, the validated binary payload and the single harness script into a new directory outside the repository. It maps only that input bundle read-only and a separate writable results folder; networking, clipboard and printer redirection are disabled.

```powershell
scripts/ci/standalone_sandbox.ps1 `
  -Install 'C:\TestPayload\Funke' `
  -DotnetRoot 'C:\Program Files\dotnet' `
  -PythonRoot 'C:\Tools\Python313' `
  -OutputDirectory 'C:\Temp\FunkeSandboxGate'

# On a machine with Windows Sandbox already enabled:
Start-Process 'C:\Temp\FunkeSandboxGate\Funke-Standalone.wsb'
```

Alternatively add `-Launch` to the preparation command. The script refuses to overwrite an existing bundle. Runtime input directories must not contain reparse points or an engine checkout. Native redistributables, renderer support and the supplied Python's relocatability are real guest prerequisites: failures remain failures in the report, rather than silently borrowing the host's installation.

The guest writes `Results/evidence.json`, `runner.log`, `exit-code.txt`, and relevant logs/markers/screenshots under `Results/Artifacts`. Wait for `exit-code.txt` before closing the guest. A release check requires exit code **0**, report status **passed**, both player markers, the successful Editor revisions, and `isolation: windows-sandbox` from the guest bootstrap. Keep `Input/recipe.json` and the `.wsb` with the evidence so the mapped folders and harness hash are reviewable. This is an executable isolation recipe, not cryptographic attestation.

## Fast harness tests

```powershell
python -m unittest discover -s scripts/ci -p test_standalone_sdk.py -v
```

These tests validate payload/path rejection, environment isolation, forbidden references, marker enforcement and bounded child-process execution. They do not launch the engine or replace the end-to-end run.
