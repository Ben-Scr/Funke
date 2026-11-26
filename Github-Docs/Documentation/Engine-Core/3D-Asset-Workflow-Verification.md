# 3D asset and material workflow verification

The 14 November implementation checks below cover individual import and render
paths. The [20 November application checks](#additional-runtime-verification-20-november-2026)
also cover the Windows Editor, Play mode and asset-stripped exports.

## Implemented behavior

| Area | Implemented contract |
| --- | --- |
| glTF/GLB materials | Standard PBR factors; base color, normal, metallic/roughness, emissive and occlusion images; OPAQUE/MASK/BLEND, cutoff, double-sided and KHR_materials_unlit. glTF color factors remain linear; legacy Funke authored colors retain their sRGB interpretation. |
| Primitive assignments | MeshAsset stores indexed submesh ranges/material indices. Renderer3D submits each range with its imported material in static and skinned paths. An explicit renderer Material GUID overrides the whole model; wireframe uses combined geometry. |
| Slot overrides/extraction | Static and skinned renderers serialize model-bound slot overrides. Unique source names survive reordering. Explicit extraction creates editable material/PNG copies, preserves existing edits and GUID metadata, and fills only unassigned slots. Assignment participates in Inspector undo. |
| Images and reload | External percent-decoded paths, base64 data URIs and GLB buffer views decode through ImageProcessing. External images join model dependency stamps. Failed reimport retains the last good model; successful replacement increments its revision. |
| Standard material | NormalTexture/NormalScale, MetallicRoughnessTexture (G roughness/B metallic), EmissiveTexture, OcclusionTexture/OcclusionStrength, AlphaCutoff and DoubleSided are serialized and exposed in the Inspector and C#. These fields use reserved typed property names. The separate slot API appends four callbacks and raises native/managed layout version to 59. |
| Tangents | Native MeshVertex is 48 bytes. Imported tangents/handedness propagate through CPU/GPU skinning and world transforms. Missing tangents use UV derivatives; mirrored UVs work and degenerate UVs retain geometric normals. This is not MikkTSpace generation. |
| Cutout | Cutout=2 is appended; Opaque=0 and Transparent=1 remain stable. Color and shadow fragment shaders use identical base-texture/tint coverage and cutoff. Surviving cutout pixels write depth. |
| Resource ownership | Model GPU images are cached by GUID/revision. Reload and idle purge retire their bind groups/bundles. Standard maps participate in scene texture ownership. This does not establish the cause of the separate old mixed-workload memory rise. |
| Asset stripping | Existing relative-image scanning retains external glTF/GLB images. New standard map GUIDs are retained through material references; tests exercise both paths. |

## Executed verification

| Check | Result |
| --- | --- |
| Current-source import/material/reload CPU tests and actual WGSL/GPU readbacks | 13 cases, 292 assertions passed |
| Current-source asset-pruning tests: glTF/GLB images, four added maps and per-slot material references | 3 cases, 18 assertions passed |
| GPU | AMD Radeon RX 7800 XT, D3D12 hardware adapter |
| Native compilation | Renderer3D, GltfImporter, SkeletalAnimation, Material, Shader, ScriptBindingsMaterial, MeshAsset, PrimitiveMesh and the material Inspector compiled individually |
| Managed compilation | Funke-ScriptCore and test assembly Release/x64 succeeded |
| Managed slot tests | 2 test bodies passed in a direct in-process runner: both renderer kinds, UTF-8 names, full GUIDs, clearing, bounds and native rejection. Native callbacks are mocked. |
| Binding contract and ABI metadata | Contract validator succeeded; 5 contract tests and 3 ABI metadata tests passed. |

GPU tests compile the current built-in WGSL, construct render pipelines and
read back color/depth pixels. They verify matching cutout holes in color and
shadows, tint-alpha coverage, opaque alpha behavior, neutral/tilted normal maps,
missing tangents with regular/mirrored/degenerate UVs, independent metallic and
roughness channels, and reflected tangent agreement between current CPU/GPU
skinning. They are not a complete Renderer3D scene integration test.

CPU fixtures cover three primitive/material ranges, PNGs via external URI,
data URI and GLB buffer view, repeated import, exact GUID serialization,
legacy defaults, unsupported required extensions/UV sets, corrupt images,
image dependency reload, retention of the last valid model and weak-pointer
checks that replaced/invalidated CPU images are freed. Expected corrupt-file
warnings belong to negative tests.

Tests ran in isolated probes: importer, skeletal animation, material, mesh and
primitive implementations were compiled from current source. Existing Engine
utilities supply JSON, image decoding, file/registry and logging functions.
The probe does not replace the application Engine DLL. The pruning probe
compiles current BuildAssetPruning.cpp with existing Engine utilities.
Response files, hashes and logs are under `artifacts/3d-workflow-20261114/`:

- `probe.rsp`, `probe-build.log`, `probe-tests.log`
- `export-probe.rsp`, `export-probe-build.log`, `export-probe-tests.log`
- `compile-*.log`, `managed-build.log`, `implementation-checks.json`

Earlier `checks.json`, `engine.log` and `export.log` describe pre-existing
binaries before implementation. They are not evidence for the new features
and are not included in these counts.

### Slot/extraction follow-up

The follow-up evidence is in `artifacts/3d-material-overrides-20261114/`:
`probe.rsp`, `probe-build.log`, `probe-tests.log`, `export-probe.rsp`,
`export-probe-tests.log`, `managed-tests.log`, per-translation-unit compilation
logs and `implementation-checks.json`. The 13/292 and 3/18 counts above are
from this follow-up run and supersede the earlier subset counts.

Additional CPU fixtures cover unique/duplicate material names, reordered
primitive assignments, full-width override GUIDs, model mismatch and clearing,
PNG pixel round-trip, sampler preservation, texture deduplication, distinct
samplers, existing user edits, invalid indices, corrupt existing material files
and retry after registration failure. Extraction tests inject a small asset
registrar: these do not constitute a live Editor/AssetRegistry session test.

Renderer3D, GltfImporter, Material, ScriptBindings3D, ScriptBindings,
BuiltInComponentRegistration3D, BuiltInComponentRegistrationSkinnedMesh3D,
ComponentInspectors and EditorComponentRegistration compiled individually.
The new ImportedMaterialAssets implementation compiled in the CPU probe; it is
included by the Editor source glob and explicitly in Editor-Tests premake.
The complete native apps still require regeneration and a matching rebuild.

## Status at the end of the 14 November checks

1. Per-slot overrides and explicit editable asset extraction are implemented.
   Unnamed/duplicate material names use indices; reordering those is not stable.
   Extracted copies are user-owned and intentionally do not track later source
   material edits. Their original paths and metadata must remain for automatic
   extraction reuse. Whole-model overrides take precedence.
2. TEXCOORD_0 is supported. Other requested UV sets and KHR_texture_transform
   fail explicitly. Unsupported required extensions fail; unsupported optional
   extensions use base glTF fallback. Some separate min/mag filtering
   combinations are approximated by the engine filter enum.
3. Full regeneration is blocked by denied access to
   `.git/modules/External/box2d`; vendor-integrity checks were not disabled.
   Native header layouts changed, so all native consumers must be rebuilt
   together. Individual compilation/probes do not establish application ABI
   compatibility. No new native application binaries were deployed here.
4. Editor/Play/export parity, a multi-material static/skinned reference scene
   and an asset-stripped runtime launch remain to be verified after that build.
5. The native thread-heap rise was attributed to Tracy on 15 November and a
   Windows thread-exit cleanup fix was regression-tested. A complete post-fix
   mixed Editor run remains unverified: the 20 November attempt was denied at
   the named pipe before collecting samples. See
   [memory lifecycle verification](../Editor/Memory-Lifecycle-Verification.md).
6. The four material tasks still awaited the application checks on 14 November.
   Their subsequent Windows results are recorded below.

## Additional runtime verification, 20 November 2026

`scripts/ci/material_export_acceptance.py` creates a reproducible reference
project with a two-material static glTF (external geometry/image) and an
animated, skinned GLB (embedded image). Both have imported-material and per-slot
override variants. The authored override references five distinct textures and
retains cutout, double-sided and alpha-cutoff settings. Runtime scripts check
material identities, map validity and advancing skeletal animation. Screenshot
pixel checks require the correct imported/override colors in all four regions;
an existing file or a successful process exit alone cannot pass.

The independent `--project-runtime` mode passed against the existing Windows
runtime on the Radeon RX 7800 XT, both before and after reordering the static
model's named source materials. GPU skinning was active. This verifies source
project rendering/bindings; it does not verify an exported game, live reimport
inside the Editor, or a matching new build. Existing component tests cover live
dependency reload and extraction preserving user edits.

The source-checkout export attempt stopped during the runtime build. The CLI
beside the packaged Launcher selected its installed Development player and
completed both original and reordered exports. Both relocated players rendered
the expected materials with the source project unavailable. External geometry,
the source image and all five override maps survived pruning. The unused texture
was absent. Evidence: `artifacts/acceptance-20261120/slot-order-sdk/report.json`.

Native material/import/profiler checks passed 39 cases / 3,626 assertions; the
separate cutout color/shadow/normal-map GPU test passed 1 case / 81 assertions.
The current generated projects include the current native source files, but a
full build also fails at build-metadata generation because ImGuizmo submodule
metadata is inaccessible. The packaged-SDK run exercised real export with its
existing runtime.

A second reference scene used three skinned primitives. An external PNG provided
the red slot, the imported material provided green, and an authored override
replaced a yellow slot with blue. The clip lifted all three by 0.65 world units.
The normal 3D Editor View showed the expected swatches. For exact comparisons
the Editor rendered the game camera at 640 × 360 on the RX 7800 XT with D3D12.

| Compared captures | Differing RGB pixels |
|---|---:|
| Edit Game View and Play at the initial pose | 0 / 230,400 |
| Edit Game View and exported player at frame 30 | 0 / 230,400 |
| Animated Play pose and exported player at frame 120 | 0 / 230,400 |
| Live reimport in the open Editor and a second export at frame 30 | 0 / 230,400 |

Changing the source PNG from red to cyan changed exactly the original 7,140 red
pixels. Green and blue kept their pixel bounds and counts. Both exports loaded a
binary scene, ran with the source project temporarily moved, and omitted the
unreferenced texture. Full-frame comparisons, color-region assertions and hashes
are in `artifacts/acceptance-20261120/material-parity-result.json`. See the
[acceptance record](../Editor/Acceptance-2026-11-20.md) for the application builds.

The Windows material acceptance covers GFX-014 (tangent-space normals and
legacy defaults), GFX-015 (linear G/B packed maps and stripped exports),
GFX-016 (color and shadow cutout) and GFX-017 (glTF slots, textures, reimport
and unsupported-extension diagnostics). A further run on the RX 7800 XT
passed nine targeted native tests and 208 assertions, including GPU
readback. Neutral normal-map color was within two 8-bit levels of the
geometric normal. Static, skinned and generated triangle fixtures cover
authored tangents and the UV-derivative fallback. The image and export
checks above describe the supported features and the tested platform.
