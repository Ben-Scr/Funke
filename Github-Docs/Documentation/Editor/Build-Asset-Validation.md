# Serialized assets in exports

The shared desktop/Web asset preparation converts `.scene`, `.prefab` and
`.dataasset` files in the export staging directory. The project source files and
`.meta` files are not conversion targets. A failed preparation must not replace a
previous successful export; publication happens only after the build succeeds.

| Asset selection | Invalid or empty serialized content |
|---|---|
| Referenced or explicitly included | Fail with the project-relative asset path and parser/conversion cause. |
| Proven unreachable by a valid pruning plan | Exclude before conversion. No replacement file is generated. |
| Usage cannot be proven, including native sources or unresolved package/dynamic dependencies | Keep conservatively. Fail if invalid and include the pruning-disable reason. |

A missing static reference does not prove that an asset is unused. Empty data
assets are not silently skipped or repaired. Author valid content, remove an
unneeded source asset deliberately, or correct its references/export policy.

`SceneSerializer::ConvertFileFormat(path, format, error)` preserves the original
read/parse error. `GameBuilder::ConvertSerializedAssets` normalizes the display
path to `Assets/...`; directory traversal errors also fail preparation. The shared
`GameBuilder::Result.Stage` contains, for example,
`Asset export failed: Assets/File.dataasset: File is empty`. CLI JSON exposes this
as `stage`, and the CLI failure message and editor progress state use the same
result. A conservative copy appends `asset pruning disabled: <reason>`.

Implementation: `Funke-Editor/src/Build/SerializedAssetExport.cpp`,
`Build/GameBuilder.cpp`, `Editor/BuildAssetPruning.cpp`, and
`Funke-Engine/src/Serialization/SceneSerializer.cpp`.

Regression tests cover an empty nested data asset, a non-object root, valid
conversion, unchanged rejected input, reachable/unreachable empty assets and
conservative native-source inclusion. These exercise the real serializer and
pruning implementation. They do not certify a complete desktop/Web export or
actual build-panel presentation. That end-to-end acceptance remains in the
[asset backlog](../../Backlog-Build-Asset-Validation.md).
