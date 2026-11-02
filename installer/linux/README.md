# Linux installer

`FunkeSetup-<version>-Linux-x64.run` installs the prebuilt Launcher, Editor, CLI,
game runtimes, assets and binary EngineSDK for the current user. No engine
repository, Git, compiler or `sudo` is needed for the application installation.
An arm64 bundle produces a separate `Linux-arm64.run`; installers do not emulate
another architecture.

The release CI baseline is **Ubuntu 24.04 x64**. The binaries inherit its glibc
and system-library requirements; compatibility with every Linux distribution is
not implied. The target system also needs a supported Vulkan driver. Other
distributions and arm64 builds need native validation before being advertised.

## Install

Linux needs Python 3.10+ for setup, a stable .NET 10 **SDK** for project compilation,
and the system libraries/graphics drivers required by the supplied Linux engine
build (including a working Vulkan driver). Install missing prerequisites using
your distribution's package manager. Microsoft's SDK downloads are available at
https://dotnet.microsoft.com/download/dotnet/10.0. Setup checks the SDK and CPU
architecture before changing the installation; it never downloads or executes a
remote prerequisite script. The .NET Runtime alone cannot compile projects.

```sh
sha256sum -c FunkeSetup-2026.1.0-beta.1-Linux-x64.run.sha256
chmod +x FunkeSetup-2026.1.0-beta.1-Linux-x64.run
./FunkeSetup-2026.1.0-beta.1-Linux-x64.run
```

The default destination is `~/.local/opt/funke`. Open **Funke** in the applications
menu or execute `~/.local/bin/funke-launcher`. The CLI is `~/.local/bin/funke`;
add `~/.local/bin` to PATH if your distribution does not already do so. A custom
destination uses `--prefix /absolute/path/to/Funke`. Keep game projects in a
separate directory. Native C++ extensions additionally require the native SDK
and matching C++ build tools; the default installer contains the binary SDK.

The installer verifies every embedded payload file. Its adjacent checksum is
useful for download integrity; verify the release source too, because an unsigned
checksum does not authenticate its publisher. `--check` verifies without
installing. `--skip-prerequisites` is intended for packaging tests, not normal use.

## Upgrade and remove

Run the new installer with the same prefix. Each version is extracted and checked
in a private staging directory before an atomic `current` symlink switch. Previous
versions remain available until uninstallation. Desktop entries and command
wrappers owned by another application or modified by the user are never replaced.
Close Funke before upgrading to avoid old processes loading newly selected assets.
If a disk or permission error interrupts setup, correct the error and rerun the
same installer. Its integration journal permits recovery without replacing the
active version before the new installation is ready.

```sh
python3 ~/.local/opt/funke/uninstall.py --uninstall
# For a custom prefix:
python3 /path/to/Funke/uninstall.py --uninstall --prefix /path/to/Funke
```

Removal deletes only inventoried, unchanged files and installer-owned desktop
entries. Projects, preferences, unowned files and modified payload files are
preserved. Setup refuses symlinked destination ancestors and unmanaged nonempty
directories. A stale `.install.lock` after an interrupted setup must be removed
only after verifying that no installer is running.

## Build and verify

Stage a complete Linux Release + Dist bundle first, then run:

```sh
python3 scripts/BuildLinuxInstaller.py \
  --bundle-dir bin/Release-linux-x86_64/Funke-Launcher \
  --output-dir bin/installer --require-release
python3 -m unittest scripts.ci.test_linux_installer
./bin/installer/FunkeSetup-2026.1.0-beta.1-Linux-x64.run --check
python3 installer/linux/smoke_test.py \
  --installer bin/installer/FunkeSetup-2026.1.0-beta.1-Linux-x64.run \
  --report /tmp/funke-linux-installer-evidence.json
```

`--require-release` requires clean repository-scoped metadata, a full commit
identity, and the matching release build ID. The
builder uses the common SDK validator/filter, excludes engine sources/native
headers, preserves executable permissions and materializes in-bundle library
symlinks. It emits `.run`, `.run.sha256` and `.run.manifest.json`. The manifest
records architecture, engine identity, file sizes and SHA-256 digests.

Tests exercise an actual executable installer, upgrade, command wrappers and
uninstall in an isolated Linux user directory. They also reject traversal,
symlink/hardlink/device archive members, altered payloads and command collisions.
The native smoke test installs the real `.run` outside the checkout under a fresh
HOME with spaces and Unicode, guards Git/native compiler commands, checks the
binary SDK, runs installed CLI/Premake/Launcher/Editor and both runtime profiles,
and exports a relocated game with private .NET. It then reinstalls and removes
the application while preserving projects. Its report and logs remain under a
unique temporary directory for CI evidence. Distinct-version upgrades are tested
by the unit suite. Native smoke requires working Linux graphics or Xvfb plus a
Vulkan implementation; fixture tests alone do not certify a driver or distro.
