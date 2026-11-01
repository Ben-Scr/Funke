# macOS installer

`scripts/BuildMacInstaller.py` turns the existing macOS Launcher bundle into
`Funke.app` inside `FunkeSetup-<version>-macOS-<arm64|x64>.dmg`. Users drag the
application to Applications. No repository, Python or build tools are needed
for ordinary C# projects. The .NET 10 SDK is a separate Microsoft prerequisite;
the supported baseline is macOS 15 or newer, on arm64 or x64.
the disk image includes an official download link and installation instructions.
It does not download or execute an unauthenticated bootstrap script.

Run on macOS after staging the Release Launcher and Dist Runtime:

```sh
python3 scripts/BuildMacInstaller.py \
  --bundle bin/Release-macosx-arm64/Funke-Launcher \
  --output-dir dist-installers
```

This produces a local test DMG, a SHA-256 file and a payload manifest. A native
Intel build uses its `Release-macosx-x86_64` bundle; the builder verifies the
Mach-O architecture rather than relabeling binaries. It refuses existing output
files. `--include-native-sdk` retains a previously staged native SDK; default
installers omit engine sources, headers and link libraries.

The flat application payload stays under `Contents/MacOS` so the engine's
executable-relative assets, sibling editor/runtime and EngineSDK resolution
remain valid. Internal relative symlinks expose this payload through
`Contents/Resources`, where SDL resolves the bundled application's base path.
All bundled Mach-O dependencies are relocated to app-local
`@loader_path` references; unresolved dependencies, external runtime references
and wrong architectures fail the build. System libraries remain system-linked.

For public release, import the Developer ID Application certificate into the
build keychain and configure an Apple notarytool keychain profile out of band:

```sh
python3 scripts/BuildMacInstaller.py \
  --bundle bin/Release-macosx-arm64/Funke-Launcher \
  --output-dir dist-installers \
  --signing-identity 'Developer ID Application: Your Name (TEAMID)' \
  --notary-keychain-profile funke-notary \
  --require-signing
```

Tag builds require this mode automatically. Public mode requires clean commit
metadata, signs native components before the app, enables hardened runtime,
notarizes and staples both the app and DMG, verifies signatures and runs the
Gatekeeper assessment. Missing credentials or a rejected notarization abort
instead of silently producing a public release. Passwords/API keys are never
command-line arguments of this script. The two app entitlements permit .NET JIT
compilation and loading .NET/user-native libraries with different signing teams;
there is no debugger or general sandbox-bypass entitlement.

GitHub Actions can run `python3 installer/macos/signing.py prepare --require`
before the build and `python3 installer/macos/signing.py cleanup` in an always-run
cleanup step. The helper uses `MACOS_CERTIFICATE_P12_BASE64`,
`MACOS_CERTIFICATE_PASSWORD`, `MACOS_SIGNING_IDENTITY`, `APPLE_API_KEY_P8_BASE64`,
`APPLE_API_KEY_ID`, and `APPLE_API_ISSUER_ID` secrets. It creates a temporary
keychain, imports the certificate and stores a notary API-key profile, then
removes the decoded credential files. It exports `FUNKE_MAC_SIGNING_IDENTITY`,
`FUNKE_MAC_SIGNING_KEYCHAIN`, and `FUNKE_MAC_NOTARY_KEYCHAIN_PROFILE` through
`GITHUB_ENV`. Pass them to the builder's `--signing-identity`,
`--signing-keychain`, and `--notary-keychain-profile` arguments. Cleanup restores
the original keychain search list and removes the temporary keychain.
Without `--require`, completely absent secrets allow an unsigned test build;
partially configured credentials always fail.

Verify the final disk image on its native runner:

```sh
python3 installer/macos/smoke_test.py \
  --installer dist-installers/FunkeSetup-2026.1.0-beta.1-macOS-arm64.dmg \
  --require-scripting
```

This mounts read-only, copies the app to a path containing spaces and Unicode,
unmounts, verifies its inventory and signatures, then executes the installed CLI,
Premake and Runtime, creates a C# project, exports it and runs the exported game.
Its test harness is copied outside the checkout so its working-directory fallback
cannot find repository files. The test uses the system .NET SDK like the installed
product; the exported game must use its private .NET runtime.

Before publication, install the exact DMG on a clean Mac of the target
architecture; check the missing-SDK path, project creation, compile/reload,
Play/Stop, export, relocation, update and uninstall. The unit tests validate
packaging boundaries on all hosts, but do not substitute for Apple tooling or
that device-level test. Native CI should also mount the finished DMG and launch
its installed app from a relocated path.

References: [Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution),
[Microsoft macOS publishing](https://learn.microsoft.com/en-us/dotnet/core/deploying/macos),
[.NET 10 supported systems](https://github.com/dotnet/core/blob/main/release-notes/10.0/supported-os.md).
