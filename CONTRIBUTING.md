# Contributing to Index

Bug reports, documentation fixes and focused pull requests are welcome.

## Reporting bugs

Use the **Bug report** issue template. Attach the log from `%LOCALAPPDATA%\Index\Logs\` and, for crashes, the dump from `%LOCALAPPDATA%\Index\CrashDumps\`. Check [Known Limitations](Github-Docs/Documentation/Known-Limitations.md) first.

## Building from source

Follow **Getting Started** in the [README](README.md). Before opening a pull request, run:

```powershell
.\scripts\RunTests.bat --release
dotnet test Tests\Index-ScriptCore-Tests\Index-ScriptCore-Tests.csproj -c Release
```

## Pull requests

- Keep each pull request to one change, and describe how you tested it.
- Follow the style of the surrounding code: PascalCase methods, `m_`/`s_`/`k_` member prefixes, short "why" comments instead of narration.
- Add or update tests where the change is testable, and update `Github-Docs/Documentation` plus `CHANGELOG.md` for user-visible changes.
- New `.cpp`/`.cs` files need a Premake regeneration (`scripts\Setup.bat`) because project files list sources explicitly.

## License

Index is licensed under the [MIT License](LICENSE.txt). By submitting a contribution you agree that it is licensed under the MIT License as well.
