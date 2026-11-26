# Package compatibility audit — 2026-11-03

The eight archives referenced by `index.json` were downloaded from their declared GitHub Release URLs.
Each archive's SHA-256, byte size, package name and manifest version matched the catalog.
The following requirements describe those published archives, not newer source files on `main`.

| Package | Published version | Manifest engine range | Catalog minimum |
| --- | --- | --- | --- |
| Funke.AudioMixer | 1.1.1 | >=2026.1.0 | 2026.1.0 |
| Funke.Cryptography | 0.1.0 | Not specified | Not specified |
| Funke.Serialization | 0.1.1 | Not specified | Not specified |
| Funke.Tilemap2D | 0.2.1 | Not specified | Not specified |
| Funke.Tween | 1.0.0 | >=2026.1.0 | 2026.1.0 |
| Funke.Networking | 1.0.0 | >=2026.1.0 | 2026.1.0 |
| Funke.Input | 0.1.0 | >=2026.1.0 | 2026.1.0 |
| Funke.Lighting2D | 0.1.1 | >=2026.1.0 | 2026.1.0 |

Four missing catalog minima (AudioMixer, Tween, Networking and Input) were copied from the verified release
manifests. Archive names, versions, hashes and sizes remain unchanged. An omitted requirement means no declared
engine-version gate, not proof that every older engine supplies the APIs the package uses. Lighting2D additionally
requires the scene-effect APIs called out in its README and a native rebuild against the actual engine.

Funke deliberately compares **engine compatibility** using the engine's version without its prerelease suffix:
`PackageManifest::IsCompatibleWithEngine` and registry `SupportsEngine` both call `WithoutPreRelease`.
Thus engine `2026.1.0-beta.1` satisfies an engine requirement of `>=2026.1.0`. This is separate from package version
selection: generic SemVer ranges and package dependencies still require explicit prerelease opt-in.
`PackageVersioningTests.cpp` covers this distinction. An older engine is rejected with the existing
`needs engine <range>, but this is Funke <version>` resolver diagnostic; the Package Manager displays the range
and disables installation when no compatible release exists.

The C# sources of all eight packages compile together against the current neighboring Release ScriptCore DLL.
The audit treats warnings as errors except CA2255 for Lighting2D's intentional module initializer. Input,
Networking and Tween lifecycle subscriptions were updated to the current event names; AudioMixer widget bindings
also use `ValueChanged`. This compiler check does not certify native ABI compatibility or runtime behavior on
Android, iOS or Web. Native package DLLs still require the normal engine hash/ABI check.

Pending source revisions are AudioMixer 1.1.2 (two-way UI bindings), Networking 1.0.1 (wire/numeric validation),
Tween 1.0.1 (current lifecycle event), and the already-in-progress Input 0.2.0. These sources have not replaced
the published archives. Publishing must create new versioned archives and hashes; do not relabel the old ZIPs.
