# Building and Shipping

Index currently supports end-to-end game exports for **Windows x64**. Linux engine builds are covered by CI, but Linux, macOS, mobile, and Web editor exports remain disabled until their platform packages are available.

## Build a game

Before exporting:

1. Open **Project Settings** and set the executable name, company, version, window mode, icon, and splash screen.
2. Add every required scene to **Build Scenes** and choose a valid startup scene.
3. Select a Windows build profile.
4. Use **Development** while testing and **Release** for distribution.
5. Choose an empty output directory outside the project and click **Build**.

The build is fail-closed: invalid paths, missing scenes, incomplete scripting artifacts, package failures, and incomplete runtime payloads stop the export instead of publishing a partial folder. The completed directory is staged first and then moved into place.

Do not remove files from the result. The executable depends on the adjacent engine, GLFW, .NET host, ScriptCore, project assembly, package libraries, project configuration, assets, and license folders produced by the build.

For the current demo release, list every native package dependency directly in the project's `packages` array. Native runtime loading does not yet expand transitive package dependencies.

## Player prerequisites

A Windows player machine needs:

- Windows x64 with a WebGPU-capable D3D12 or Vulkan graphics driver.
- The [.NET 10 x64 Runtime](https://dotnet.microsoft.com/en-us/download/dotnet/10.0).
- The [Microsoft Visual C++ 2015-2022 x64 Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe).

Index is framework-dependent rather than self-contained, so installing the matching .NET runtime is required. If it is missing, the game shows a message with a download link instead of failing silently.

Index is MIT-licensed ([LICENSE.txt](../../../LICENSE.txt)): you may ship and sell games made with it freely. The license only asks that its notice travels with the engine, which the build already does by placing it, together with the third-party notices, in the output's `Licenses` folder. Keep that folder when you distribute the game.

Saved data (`PlayerPrefs`, `SaveData`) is stored in `%LOCALAPPDATA%\<Company>\<Name>`, taken from **Project Settings**. Set both before your first release: changing them later moves players' saves to a new folder.

## Standalone editor prerequisites

Creating and building games with the standalone Editor requires:

- Visual Studio or Build Tools with MSVC v143 14.44 or newer for native scripts and packages.
- Visual Studio 2026 (18.0+) for C# IntelliSense when Visual Studio is selected as the script editor.
- The .NET 10 SDK for C# scripts and managed packages.
- Python 3.10 or newer for the package scaffolding commands.
- CMake for a project's `NativeScripts` target.

Premake and the package authoring scripts are included in `EngineSDK`.

## Validate a game release

Test the exact folder or archive you plan to upload:

1. Copy it to a clean Windows x64 VM that has no Index source checkout and no developer SDKs.
2. Install only the player prerequisites above.
3. Launch the executable from a path containing spaces and non-ASCII characters.
4. Exercise the startup scene, scene changes, C# scripts, native and managed packages, audio, input, save data, fullscreen/window switching, and shutdown.
5. Confirm no absolute developer paths appear in logs or configuration.
6. Unzip the final archive into a second clean directory and repeat the launch test.
7. Scan the archive and record its checksum before publishing.

Treat the clean-machine run as a release gate. Repository CI can verify structure and startup behavior, but it cannot prove that an arbitrary customer machine has the expected drivers and runtimes.

## Build the standalone engine distribution

Engine maintainers need Visual Studio with MSVC v143 14.44 or newer, the .NET 10 SDK, Python 3.10 or newer, Git, CMake, and a recursive submodule checkout.

After a fresh clone, build the git-ignored Dawn/WebGPU dependency once. A release-only Dawn build is sufficient for the publish bundle:

```powershell
$env:DAWN_BUILD_CONFIGS = "Release"
$env:DAWN_SKIP_PREMAKE = "1"
.\scripts\dawn\SetupDawn.bat
Remove-Item Env:DAWN_BUILD_CONFIGS
Remove-Item Env:DAWN_SKIP_PREMAKE
```

Generate project files once:

```powershell
.\scripts\Setup.bat
```

Build the Release editor/runtime, the stripped Dist runtime, the package tool, ScriptCore, Launcher, SDK payload, notices, and then run the structural verifier:

```powershell
.\scripts\BuildWindows.ps1 -PublishBundle
```

The verified folder and upload artifacts are:

```text
bin\Release-windows-x86_64\Index-Launcher\
bin\Release-windows-x86_64\Index-Editor-<version>-Windows-x64.zip
bin\Release-windows-x86_64\Index-Editor-<version>-Windows-x64.zip.sha256
bin\Release-windows-x86_64\PrivateSymbols\Index-Editor-<build-id>-Windows-x64-symbols.zip
```

The version comes from `Version.props`. The build ID combines that version with the exact Git commit and is embedded in native and managed binaries plus `BuildMetadata.json`. The build extracts the ZIP into a temporary path containing spaces and non-ASCII characters, verifies that extracted copy, and records its SHA-256. The archive includes:

- Launcher, Editor, and Development Runtime executables.
- The prebuilt `EngineSDK\Runtime-Dist` runtime used for Release game exports.
- Index-ScriptCore and Index-PackageTool artifacts.
- Premake, the package workspace scripts, and package create/remove/version tools.
- Native headers, import libraries, and required third-party headers.
- Index, third-party, Dawn, font, and .NET redistribution notices.

PDB files are excluded from the public archive. A separate build-ID-keyed private symbols archive contains the exact Release and Dist PDBs, a binary hash manifest, and the build identity needed to diagnose crash dumps.

The CI artifact's 90-day retention is transfer storage only. Copy every private symbols archive to durable, access-controlled storage and retain it for the full supported lifetime of that exact release, plus any longer crash-report retention period.

The structural verifier can also be run directly:

```powershell
.\scripts\verify-standalone-zip.bat Release
```

## Engine release checklist

Before tagging a public release, update `Version.props`, add the exact version to `CHANGELOG.md`, and use the matching `v<version>` tag. Prerelease and final versions are distinct: for example, `v2026.1.0-rc.1` requires `IndexVersion` to be `2026.1.0-rc.1`.

For unsigned local candidates, the normal publish command is sufficient. Official tags require Authenticode signing and a detached CMS signature over the checksum. CI reads the PFX and password from `INDEX_SIGNING_CERTIFICATE_BASE64` and `INDEX_SIGNING_CERTIFICATE_PASSWORD`; configure `INDEX_SIGNING_TIMESTAMP_URL` as a repository variable when timestamping is required. Credentials stay outside the repository.

Before tagging a public release:

1. Confirm GitHub rulesets protect the default branch and release-tag path and require both the `windows-build` and `linux-build` CI jobs. This repository setting is external to the checkout; do not publish a tag while either required check is absent.
2. Run `.\scripts\RunTests.bat --release`, then `.\scripts\RunTests.bat` to cover both Release and Debug native tests.
3. Run `dotnet test Tests\Index-ScriptCore-Tests\Index-ScriptCore-Tests.csproj -c Release`.
4. Run the project-local package smoke test:

   ```powershell
   .\scripts\ci\project_package_smoke_test.ps1 -Configuration Release
   ```

5. Run `.\scripts\ci\core_module_build.ps1` so the isolated core-only Engine must link as a complete shared library.
6. Create and verify the release-candidate artifacts with `.\scripts\BuildWindows.ps1 -PublishBundle`.
7. Copy the Launcher bundle to a clean Windows VM, install the standalone editor prerequisites, and create, open, play, build, and run a small project containing both a C# and native package.
8. Run the packaged Launcher, Editor, and Dist Runtime smoke tests and require their natural zero exit after the stable-frame marker.
9. Review the bundled licenses and the matching `CHANGELOG.md` section.
10. Move the private symbols artifact to durable, access-controlled storage under its build ID, and retain it for the release's full supported lifetime. Do not treat the 90-day CI artifact as the archive of record.
11. Verify the archive checksum and detached signature:

   ```powershell
   .\scripts\VerifyRelease.ps1 `
       -ArchivePath .\Index-Editor-<version>-Windows-x64.zip `
       -ChecksumPath .\Index-Editor-<version>-Windows-x64.zip.sha256 `
       -ExpectedSignerThumbprint <published-code-signing-thumbprint> `
       -RequireSignature
   ```

CI repeats the automated portions for pull requests. A public tag should be created only after the clean-machine engine and exported-game checks pass.

CI regenerates `--module-profile=core` into `premake/generated/core` and builds the isolated Engine on Windows and Linux. Linux CI also runs the Debug engine tests in separate `--sanitize` and `--coverage` variants. AddressSanitizer and UndefinedBehaviorSanitizer fail the job on runtime defects; lcov publishes the engine coverage report. The core, sanitizer, and coverage object directories are distinct, so none of these gates can reuse full-profile or uninstrumented objects.
