# Install Funke on Windows

Download `FunkeSetup-<version>-Windows-x64.exe` from the release and open it on Windows 10 or 11 x64. The recommended installation includes the Launcher, Editor, Windows game runtimes and project templates. You do not need Git, Visual Studio or the engine source code for ordinary C# projects. Windows on Arm is not currently supported by this installer.

Funke is installed for your Windows user, by default in `%LOCALAPPDATA%\Programs\Funke`. Setup offers English and German. The Microsoft .NET SDK and Visual C++ runtimes are shared system components: if they are missing or too old, setup runs their official Microsoft installers. Setup checks both x64 (engine applications) and x86 (package generator), even on 64-bit Windows. Windows can ask for administrator approval for those components. Cancelling that approval leaves the prerequisite incomplete; install it with an administrator's help and run setup again. Follow any restart request before opening Funke.

The completed setup file includes the engine, optional package payloads and prerequisite installers. Installation itself does not need to download those files. Online package searches, additional packages and a project's own external dependencies can still require internet access later.

## Choose packages

Choose the recommended installation to start with the engine and templates. Full installation includes the command-line tool and all five optional packages; custom installation lets you choose individual components. Optional packages add reusable C# functionality:

| Package | Purpose |
|---|---|
| AudioMixer | Audio mixing helpers |
| Cryptography | Cryptography helpers |
| Serialization | Serialization helpers |
| Tween | Tweening and animation helpers |
| Networking | Networking helpers |

Selecting a package in setup makes it available on the computer. To use it, open a project and add it in the editor's **Package Manager**. Each project chooses its own packages; setup does not change existing projects. The .NET SDK compiles the selected package's sources when the editor adds it to the project.

The current installer does not include Android, Web or iOS export toolchains. See [Platform Packages](Platform-Packages.md) for those exports. Native C++ packages and scripts have additional development-tool requirements.

## Open your first project

1. Start **Funke** from the Start menu or the shortcut created by setup.
2. In the Launcher, choose a new project and select a 2D or 3D template.
3. Give the project a name and keep the suggested location, or choose another folder you can write to.
4. Create the project. The Launcher opens it in the Editor.

Projects are saved separately from the application, by default under your user folder's `Funke\Projects` directory. Keep project folders outside the Funke installation directory. If you do not use Git, turn off **Initialize Git repository** when creating a project.

See [Building and Shipping](Building-and-Shipping.md) when you are ready to export a game.

## Update or remove Funke

Close the Launcher and Editor before running a newer setup. It remembers your previous location and component choices. Deselecting a component during an upgrade leaves its existing files in place. Back up important projects before opening them in a newer engine version, because project formats and package versions can change.

Remove Funke through Windows **Installed apps**. Your projects live outside the application directory and remain available. Shared Microsoft runtimes are managed separately by Windows; removing Funke does not remove them.

If setup reports a missing or failed prerequisite, complete that installation and rerun setup. If the editor reports a C# build error, check that the .NET SDK installation finished successfully and restart Funke after installing it. The setup's checksum detects a damaged download; a signed release also lets Windows verify its publisher.
