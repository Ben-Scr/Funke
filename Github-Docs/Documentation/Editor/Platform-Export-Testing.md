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

`passed` describes only this runtime invocation. `qualification` remains `unverified`, and `sourceCommit`/`buildToolchain` remain null because this harness does not attest the build. It cannot reject an unchanged older export based on its source commit or prove which compiler produced it. Bind the inventory to retained build provenance before treating it as release evidence; a dummy compiler's unit-test pass is never a real-build or platform pass. Missing runtime tools/devices produce a blocked runtime result, while unavailable build or physical-device evidence remains unverified. The workflow summary lists these levels independently.

A failed browser startup also saves `failure.png` when possible. Each invocation requires a new output directory, so a failed run cannot reuse a previous screenshot or result. Missing SDKs, devices or WebGPU support must remain explicitly unverified in release notes.
