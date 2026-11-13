# Test an exported game on a device or browser

`scripts/ci/platform_export_smoke.py` runs an **already exported game**, requires messages emitted by its C# scripts, captures its rendered image, and checks expected image colors. It fails on missing messages, script exceptions, browser errors, crashes, timeouts or wrong pixels. It saves the log, screenshot and `result.json` in a new evidence directory. Export/build success alone does not pass this check.

The ordinary Python builder tests still substitute external compilers. This harness is a separate, opt-in release check against real APKs, WebAssembly exports or simulator apps; it does not compile the game or provision a device.

Results distinguish four evidence levels. A green builder unit test does not qualify a platform:

| Evidence level | Required evidence | Current producer |
|---|---|---|
| `unit_mock` | Builder logic with substituted compiler outputs | `test_android_builder.py`, `test_ios_builder.py`, `test_web_builder.py` |
| `real_build` | Actual compiler invocation, source commit, toolchain versions and export hash | Separate build evidence; not attested by this runtime harness |
| `real_runtime` | The identified artifact runs, emits the required markers and passes pixel checks | `platform_export_smoke.py` |
| `physical_device` | The same identified build runs on documented physical hardware | Separate device evidence; browser/emulator/simulator results cannot substitute |

The minimum qualification matrix remains open until the corresponding evidence is retained:

| Platform | Builder unit tests | Real build | Real runtime | Physical device |
|---|---|---|---|---|
| Web | Available, `unit_mock` | Unverified by this harness | Opt-in browser run | Hardware/browser coverage recorded separately |
| Android | Available, `unit_mock` | Unverified by this harness | Opt-in device or emulator run | Unverified by this harness |
| iOS | Available, `unit_mock` | Unverified by this harness | Opt-in simulator run | Unverified; this harness only supports simulators |

## Prepare a small test project

Use a dedicated test application identifier, such as `dev.funke.exportsmoke`. Give the scene an unmistakable stationary image, for example a magenta square at its center against a dark background. Attach a script that logs a unique marker after several successful updates:

```csharp
using Funke;

public class ExportSmoke : EntityScript
{
    private int m_Frames;

    public override void OnUpdate()
    {
        if (++m_Frames == 120)
            Log.Info("FUNKE_EXPORT_SMOKE_SCRIPT_OK");
    }
}
```

For a stronger release gate, have the script test the required game behavior (asset load, input, audio initialization and save/load round-trip), emit distinct success markers only after those checks pass, and repeat `--expect-log` for each. The harness proves only the behavior actually asserted by those markers and pixels; it cannot determine whether a sound was audible from a screenshot.

All platforms need Python 3.10+ and Pillow (`python -m pip install Pillow`). Global options must come **before** the platform name. `--expect-pixel x,y,R,G,B` uses normalized screenshot coordinates and RGB bytes; repeat it for several known locations. The default color tolerance is 12. Web screenshots cover the game canvas; device screenshots cover the whole display, so account for orientation and system bars. Choose test colors and positions in advance rather than accepting whatever a broken export draws.

## Web

Install Node.js and Playwright in your test environment (`npm install playwright`; `npx playwright install chromium`), or point `NODE_PATH` to an existing installation. An installed Chrome/Edge can be selected using `--browser-executable` or `--browser-channel chrome|msedge`. The browser must provide a working WebGPU adapter; absence of one is a failure, not a skipped success.

```powershell
python scripts/ci/platform_export_smoke.py --output output/web-smoke-001 --expect-log FUNKE_EXPORT_SMOKE_SCRIPT_OK --expect-pixel 0.5,0.5,255,0,255 web --entry Builds/Web/MyGame/index.html --browser-channel chrome
```

The harness serves the export on loopback with the required cross-origin isolation headers, launches a fresh browser context, waits for the script marker and watches for errors for another two seconds. `--settle` increases that observation period. `--browser-arg=...` passes an explicit browser flag when a CI environment requires one. It does not disable the browser sandbox or fake WebGPU support.

The **Exported Web game smoke** GitHub Actions workflow can be dispatched with an existing run ID, exported-game artifact name, HTML entry, script marker and expected pixel. It downloads that artifact and executes its WebAssembly in Chromium with the SwiftShader option for hosted CI, then uploads the evidence even on failure. This is a software-rendering smoke check; retain a real-GPU browser check for release qualification. The workflow intentionally does not run against arbitrary source changes without an exported game to test.

## Android

Install Android platform-tools, enable debugging on a **test** device and authorize the computer. Choose the exact serial from `adb devices`; the script never selects a device implicitly. The current arm64 export needs a compatible device or emulator.

```powershell
python scripts/ci/platform_export_smoke.py --output output/android-smoke-001 --expect-log FUNKE_EXPORT_SMOKE_SCRIPT_OK --expect-pixel 0.5,0.5,255,0,255 android --apk Builds/Android/MyGame.apk --serial DEVICE_SERIAL --package dev.funke.exportsmoke
```

This installs and launches the APK, reads logs for that process and captures the display. It refuses to replace an installed package unless `--replace` is given. The test stops its application when finished, leaves the APK installed, and does not clear user data or the device's log buffer. Use a dedicated test package and device.

## iOS Simulator

On macOS with Xcode, supply a `.app` **built for the simulator**, its bundle identifier and the exact UDID of an already booted simulator (`xcrun simctl list devices booted`). A signed device `.app` cannot run in the simulator. Funke's current iOS export targets physical devices; prepare the simulator build separately with a matching native/Mono toolchain. This harness does not convert a device build into a simulator build and does not certify physical-device signing or deployment.

```bash
python3 scripts/ci/platform_export_smoke.py --output output/ios-smoke-001 --expect-log FUNKE_EXPORT_SMOKE_SCRIPT_OK --expect-pixel 0.5,0.5,255,0,255 ios --app Builds/Simulator/MyGame.app --device SIMULATOR_UDID --bundle-id dev.funke.exportsmoke
```

Replacing an installed simulator app also requires `--replace`. Keep a separate physical iOS device check in the release checklist.

## Record release evidence

Keep `result.json`, `runtime.json`, `game.log` and `game.png` together. Report format 2 records `evidenceLevels`, the selected target and a SHA-256 inventory of the APK, complete Web entry directory or iOS app directory. The inventory hashes the canonical JSON file list (sorted paths, byte counts and SHA-256 per file); the source directory itself is not part of the digest. The harness hashes before and after the run and rejects an artifact changed during validation. Keep the evidence directory outside the export, and use regular files rather than linked payloads. Runtime versions are recorded when the selected tool starts successfully.

`passed` describes only this runtime invocation. Without build provenance, even a successful smoke leaves `real_runtime`, `sourceCommit` and `buildToolchain` unverified/null. `qualification` always remains `unverified`; a smoke is not a complete platform qualification. Missing runtime tools/devices produce a blocked runtime result.

For a build-bound runtime check, supply both `--build-evidence receipt.json` and
`--expected-commit <full-lowercase-40-character-commit>` before the platform name.
The expected commit must come from the calling build job. The receipt must come
from a trusted successful **real compiler build**, with its compiler logs retained:

```json
{
  "formatVersion": 1,
  "platform": "web",
  "sourceCommit": "<full source commit>",
  "artifactSha256": "<sha256-file-inventory-v1 digest>",
  "evidenceLevel": "real_build",
  "passed": true,
  "buildToolchain": {"emcc": "<actual version>", "dotnet": "<actual version>"}
}
```

Produce the digest using `artifact_identity` on the final export; keep the receipt
outside that export. The harness rejects a different commit, platform, artifact,
mock-build receipt or missing toolchain before launching the runtime. It retains
the receipt hash, commit and toolchain in the result. Successful runtime checks
then produce `real_runtime: passed`, while `real_build` explicitly cites the
separate build receipt. This is provenance validation, not a cryptographic
compiler attestation: a caller-supplied fabricated receipt is not trustworthy.
Use `scripts/ci/build_export_evidence.py` in the trusted build job to produce the
receipt automatically around the actual export command. It requires a clean
checkout at the full expected commit both before and after the command, a fresh
export path, successful tool-version queries and a successful build. A failed
build retains its log and a failed receipt. Put the evidence directory outside
the checkout and the export. For example (replace the paths and commit):

```bash
python scripts/ci/build_export_evidence.py \
  --platform web --source-root /work/checkout --expected-commit "$COMMIT" \
  --artifact /work/export/MyGame/index.html --output /work/build-proof \
  --tool emcc=/sdk/emscripten/emcc --tool dotnet=/sdk/dotnet \
  -- python platforms/Funke.Platform.Web/Builder/build.py build --request /work/request.json
```

The receipt and `build.log` are created in `/work/build-proof`. Publish that
folder as a separate artifact from the Web export. The wrapper trusts the CI
job's chosen build command and tool paths; it cannot distinguish a deliberately
substituted compiler or arbitrary file-generating command. Git cleanliness also
does not attest ignored files, external inputs or dependency downloads. Keep
those inputs versioned or separately pinned in the trusted build job. No
compiler or device qualification is inferred from unit-test fixtures.

In the exported-Web workflow, set `build_evidence_artifact` to that artifact's
name and `expected_commit` to the same full commit. Both are optional as a pair.
Before downloading proof, the workflow checks that the source run succeeded,
uses the expected commit and is a push/dispatch from this repository, not a fork
or PR run. The game and proof are downloaded from the same run. Without both
inputs, it remains an unbound diagnostic smoke.

Inventory ordering uses case-sensitive POSIX-relative strings on every OS.
Recreate older Windows receipts with mixed-case filenames; the earlier
Windows-specific ordering could differ from a Linux runtime check.

A failed browser startup also saves `failure.png` when possible. Each invocation requires a new output directory, so a failed run cannot reuse a previous screenshot or result. Missing SDKs, devices or WebGPU support must remain explicitly unverified in release notes.
