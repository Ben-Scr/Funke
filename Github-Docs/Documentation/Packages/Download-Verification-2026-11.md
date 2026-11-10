# Download and archive verification — November 2026

QAL-028 was checked on Windows x64 Release, from `4ed58316af85` plus the current working changes. This is a local adversarial verification, not a certification of every network provider or platform. The loopback fixtures never contact external services.

## Source and caller contracts

| Caller | Integrity and publication boundary |
|---|---|
| Package Manager registry install/restore | Requires a registry SHA-256; validates the downloaded manifest/name/version before registration. Updates keep a backup for rollback. PackageTool refuses an already existing package directory. |
| `GitHubSource` DLL distribution | Requires a valid SHA-256 before download; canonical containment under `Packages`; adds the C# reference only after the successful tool result. NuGet uses the separate `dotnet add package` path. |
| Launcher Asset Library | Optional catalog archive SHA-256 is verified before extraction. An omitted hash remains an explicitly less trusted user/catalog source. The destination project is staged before import. |
| Localization downloads | Optional expected size and SHA-256 are checked by `LanguageDownloader` after the file transfer. Existing callers have a two-minute process limit. |
| `github-download` CLI | Optional SHA-256; a unique sibling temporary file is atomically moved over the output only after verification. Existing output survives bad hash, truncated data, size rejection and timeout. |
| `registry-download` / `unzip` | Shared bounded extraction, preflight path validation, streamed byte checks and CRC-32 verification. No symlinks, junction destinations, duplicate entries, traversal, absolute paths, device names or alternate data streams. `unzip` requires an empty/absent destination. |

Registry and file transfers enforce 512 MiB, 30 seconds of inactivity and a 15-minute total deadline. Extraction enforces 10,000 entries and 2 GiB using both declared and emitted sizes. Registry publication still requires exactly one top-level folder with `funke-package.lua`. Cleanup runs after streams and archives close, including early rejection paths.

## Confirmed defects and corrections

- Failed file downloads overwrote existing output and could leave a partial file. Staging now preserves the prior output; hash computation runs after the output stream closes.
- Registry rejection could try to delete an archive while it was still open on Windows. Final cleanup now runs after disposal.
- The standalone unzip path lacked registry-equivalent budgets and could write through a pre-existing junction. Both extraction paths now share validation; nonempty destinations and linked destination ancestors are rejected.
- Download streams had no internal inactivity deadline. The shared downloader now cancels stalled reads as well as header waits.
- A ZIP with a falsified uncompressed length was silently truncated by the underlying reader. Explicit CRC-32 validation rejects this and corrupt entry checksums.

## Executed evidence

`scripts/ci/package_download_adversarial_test.py` runs after building `Funke-PackageTool` (`FUNKE_PACKAGE_TOOL_TEST` can select a private DLL). Twenty-two transfer/archive cases passed in 65.7 seconds; two additional CRC/declared-length cases and valid-archive regressions passed after adding checksum validation. These include actual 513-MiB streamed responses, absent Content-Length, wrong/malformed hashes, truncated transfers, 34-second stalls, 10,001 entries, oversized ZIP metadata, duplicate paths, traversal with either separator, symlink entries and a real Windows junction. Every fixture checks prior package bytes, project registration and an outside-destination sentinel. Rejected installs emit no success event and leave no handled-failure staging files.

Sixteen native package-registration/output-path cases passed with 74 assertions, including rejected registration preserving project JSON, references and C# project bytes. Fifteen existing installer-payload tests passed. No remote CI run was launched. The full Editor update-dialog rollback path, NuGet transport, archive packing, metadata-fetch limits, POSIX runtime behavior and malicious concurrent replacement of filesystem ancestors were not exercised by this focused run. Abrupt process termination cannot run managed cleanup; shorter parent deadlines can therefore leave private temporary files. This does not imply a completed installation.

## Dependency pins

All four cached Windows prerequisites matched `installer/windows/dependencies.lock.json`: .NET SDK 10.0.403 (SHA-512), VC Redistributable x64/x86 14.51.36247.0 (SHA-256), Inno Setup 6.7.3 (SHA-256). Their Authenticode signatures were valid; `BuildInstaller.ps1` also requires the configured publisher. The five cached installer package archives matched their exact release versions and SHA-256 values in `installer/windows/packages.lock.json`. Hash verification protects against replaced release assets even when a hosting service permits replacement.

Deliberately moving development inputs remain visible: registry discovery uses `main`, CI uses `10.0.x` and versioned action tags, and the ImGui submodule declares the `docking` tracking branch. Normal submodule checkout still uses the superproject's recorded gitlink commit. Discovery metadata is not equivalent to a locked archive hash. No dependency version, credential, signing policy or publication was changed by this verification.
