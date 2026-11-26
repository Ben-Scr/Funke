# Focused loading/resource probe

This opt-in Windows/MSVC runner compiles the changed loading, prefab, string-table
document and font implementations directly. Remaining dependencies come from an
existing Release engine DLL and dependency libraries. Physics2D is disabled here.
It is not the complete Engine/Editor suite, a matching-DLL ABI check, a benchmark,
or an alternative engine/editor build. Normal `Build.ps1` integrity gates remain
required before using/deploying new engine/editor binaries.

Prerequisites: Visual Studio 2022 C++ tools, CMake 3.25+, existing Release outputs
under `bin/Release-windows-x86_64`, and the existing Dawn static Release library.
`FUNKE_BINARY_ROOT` can select another compatible dependency-output directory.
Run from the repository root:

```powershell
cmake -S Tests/Loading-Resource-Probe -B bin-int/loading-resource-probe -G "Visual Studio 17 2022" -A x64
cmake --build bin-int/loading-resource-probe --config Release --parallel 3
```

Place the matching runtime dependency DLLs beside the generated executable or
make their directories available through this process's `PATH`. Use a short
temporary path for file-history fixtures to avoid the test environment's deep
default path (this is not a general Windows long-path test):

```powershell
New-Item -ItemType Directory -Force bin-int/lrt | Out-Null
$env:TEMP = (Resolve-Path bin-int/lrt).Path
$env:TMP = $env:TEMP
& bin-int/loading-resource-probe/Release/loading-resource-probe.exe --no-colors
```

With `FUNKE_LOADING_GPU_TEST` unset, the 22 native/document cases run and resource
cases are excluded. Opt into a resource backend explicitly:

```powershell
$env:FUNKE_LOADING_GPU_TEST = '1'
$env:FUNKE_LOADING_BACKEND = 'null' # 'vulkan' or unset for D3D12
# Optional font with Greek/Cyrillic/CJK coverage; default: C:/Windows/Fonts/msyh.ttc
# $env:FUNKE_LOADING_CJK_FONT = 'C:/fixtures/CjkFont.ttf'
& bin-int/loading-resource-probe/Release/loading-resource-probe.exe '--test-case=Large texture publication*,Font family selects*,Texture partial transfers*' --no-colors
```

Resource cases cover chunk visibility/cancel, partial failure/source invalidation
and mixed-font atlas/layout contracts. Exit 77 means backend initialization was
unavailable, not a passing/skipped GPU test. Null checks validation and lifetime
without executing GPU rendering. Font files are supplied by the environment and
are not redistributed by this test. Restore changed environment variables after
running in an interactive shell.

The 2026-11-14 run passed 22 native cases / 3467 assertions and 3 Null cases /
80 assertions. D3D12 and Vulkan were unavailable due Windows DLL load Error 87.
See the [verification details](../../Github-Docs/Documentation/Engine-Core/Cooperative-Loading.md).
