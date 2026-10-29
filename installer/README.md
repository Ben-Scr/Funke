# Windows installer

`scripts/BuildInstaller.ps1` packages an existing Windows x64 release bundle with Inno Setup 6.7.3. It does not compile the engine. The setup targets Windows 10/11 x64, installs Funke for the current Windows user and can install the official Microsoft .NET 10 SDK and Visual C++ runtime when needed. Windows on Arm is not currently an installer target.

The default installer contains binaries, assets, templates, package tools and `EngineSDK/FunkeSDK.json`; it excludes engine sources, native headers and link libraries. `-IncludeNativeSdk` adds the staged native headers and libraries for C++ scripts and packages, without engine implementation sources. The input bundle must have been staged with native development support. For direct payload preparation the equivalent option is `--include-native-sdk`. Upgrading to the binary installer removes obsolete engine-source/native-SDK directories inside the application SDK; project directories are separate.

`scripts/BuildWindows.ps1 -PublishBundle` and `scripts/PackagePosix.py` also produce binary distributions by default. Their opt-in native SDK switches are `-IncludeNativeSdk` and `--include-native-sdk`. Developer launcher staging retains native support by default; `scripts/CopyEngineSdk.py ... --binary-only` stages a managed-only SDK. The manifest advertises only runtime folders actually staged, and the release verifier requires complete Development and Release payloads.

User instructions: [Install Funke on Windows](../Github-Docs/Documentation/Editor/Installer.md).

## Build

From the repository root, first produce a complete Release + Dist bundle:

```powershell
scripts/BuildWindows.ps1 -PublishBundle
scripts/BuildInstaller.ps1 -DownloadCompiler
```

The first command builds the engine and stages its publish bundle. The second explicitly allows downloading the pinned Inno compiler into `bin-int`, then prepares the installer payload and compiles setup. Subsequent installer builds can reuse the staged bundle and compiler. Native engine changes require a fresh bundle first.

The packaging machine also needs Windows SDK `mt.exe` (found automatically, or supplied with `-ManifestToolPath`). Both installer packaging and `BuildWindows.ps1 -PublishBundle` use `scripts/PrepareBundledPremake.ps1` to merge the UTF-8 process manifest into staged Premake before publishing, so installation and project paths containing non-ASCII characters work. The vendor executable remains unchanged; the installer payload inventory records both hashes. Users of the finished installer or ZIP do not need the Windows SDK.

To use an existing compiler and bundle:

```powershell
scripts/BuildInstaller.ps1 `
    -BundleDirectory 'C:\Builds\FunkeBundle' `
    -CompilerPath 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe' `
    -OutputDirectory 'C:\Builds\Installers'
```

Defaults are `bin/Release-windows-x86_64/Funke-Launcher` for the input and `bin/installer` for output. Release identity comes from the bundle metadata. The outputs are:

- `FunkeSetup-<version>-Windows-x64.exe`
- The executable's `.sha256` checksum
- The executable's `.manifest.json` payload inventory and `.dependencies.json` dependency record

The finished setup contains its payload and prerequisite installers; it does not download them during installation. Preparing that payload requires network access unless its locked inputs are already available locally. `-PrerequisiteDirectory` supplies the Microsoft installers from an existing directory; `-CatalogPath` selects an alternate package catalog. These inputs are validated against their expected metadata and hashes.

## Optional packages

The package catalog in `windows/packages.lock.json` pins the package versions, download locations and checksums. The initial selection contains five packages implemented in C#:

| Package | Version |
|---|---|
| Funke.AudioMixer | 1.1.1 |
| Funke.Cryptography | 0.1.0 |
| Funke.Serialization | 0.1.1 |
| Funke.Tween | 1.0.0 |
| Funke.Networking | 1.0.0 |

Selected sources are installed under `EngineSDK/packages`. They appear in the editor's Package Manager, where the user enables them separately for each project. Installation does not modify existing projects or enable all packages in new projects. Source files must remain in the payload: the project package workspace compiles them with the .NET SDK when enabled.

The initial catalog excludes native packages and platform exporters. Native packages need matching engine ABI artifacts and possibly C++ build tools. Android, Web and iOS exporters also need platform toolchains and a complete native engine SDK; copying their manifests into this desktop bundle is insufficient.

## Installation modes

The recommended installation includes the core application and templates. Full installation also includes the CLI and all five catalog packages; custom installation permits individual choices. Setup selects English or German from the system language. The default location is `%LOCALAPPDATA%\Programs\Funke`.

Inno's standard `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG` and `/COMPONENTS` options are supported. Silent mode does not bypass Windows administrator approval for missing Microsoft prerequisites. For an unattended deployment, preinstall those prerequisites or provide an appropriately elevated deployment context. Exit code 3010 from a prerequisite requests a restart, and setup defers launching Funke until the restart is resolved.

Rerunning setup preserves the previous location and selections. Deselecting an optional component during an upgrade does not delete its existing files. Uninstall uses tracked application files; it does not remove project folders, preferences or shared Microsoft runtimes.

## Signing and CI

Public release builds must sign the engine bundle before packaging and sign both setup and its uninstaller. Use the same identity for both stages:

```powershell
scripts/BuildWindows.ps1 -PublishBundle -RequireSigning `
    -SigningCertificatePath 'C:\Signing\funke.pfx' `
    -SigningTimestampUrl 'https://your-rfc3161-timestamp-service.example'
scripts/BuildInstaller.ps1 -DownloadCompiler -RequireSigning `
    -SigningCertificatePath 'C:\Signing\funke.pfx' `
    -SigningTimestampUrl 'https://your-rfc3161-timestamp-service.example'
```

Set the PFX password through `FUNKE_SIGNING_CERTIFICATE_PASSWORD`, or pass the name of another secret environment variable using `-SigningPasswordEnvironmentVariable`. Do not put the password in the command line. Alternatively, use `-SigningCertificateThumbprint` for a certificate with an accessible private key in `CurrentUser\My` or `LocalMachine\My`.

`BuildInstaller.ps1` passes the signing configuration to `SignInstaller.ps1` through process environment variables. Inno calls that helper for setup and the uninstaller. It locates Windows SDK `signtool.exe`, signs using SHA-256, verifies the signature and expected signer, and checks the timestamp when configured. The helper suppresses native diagnostics that could expose signing arguments.

The Windows CI job builds the installer after the signed publish bundle, using the existing certificate secrets and timestamp setting. Tags require signing; builds without configured credentials remain unsigned for testing. CI uploads setup, checksum, payload inventory and dependency record. The tag release job adds them to the existing draft release.

## Release checks

Run the installer integration smoke test on a test account with the prerequisites installed and no existing Funke installation:

```powershell
scripts/ci/installer_smoke_test.ps1 `
    -InstallerPath 'bin/installer/FunkeSetup-<version>-Windows-x64.exe'
```

The test uses an isolated path containing spaces and a non-ASCII character. It checks selective package installation, adding components, removing obsolete installer-owned package sources, enabling Tween in a new project, compiling its Dist assembly during a Release game export, and uninstalling while preserving separate projects. These checks passed locally. Windows CI runs the same test; it retains its logs and the separate test projects for inspection.

Exercise setup on clean Windows x64 with a standard user account, including a machine without .NET or the Visual C++ runtime. Check prerequisite cancellation and restart handling, a path containing spaces and non-ASCII characters, upgrading an existing install, and uninstalling while retaining projects. Create a project, enable one selected package, run it, and export a Windows Release build. Verify a signed installer and its installed uninstaller. CI packaging alone does not establish those clean-machine results.
