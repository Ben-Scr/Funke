[CmdletBinding()]
param(
    [ValidateSet("Dist")]
    [string]$Configuration = "Dist",

    [string]$OutputDirectory,

    [switch]$Verify
)

$ErrorActionPreference = "Stop"
$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$platformDirectory = "$Configuration-windows-x86_64"

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot "bin\FunkeSDK-$platformDirectory"
}

$sdkRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$sdkMarker = Join-Path $sdkRoot ".funke-native-sdk"
$engineBinaryDirectory = Join-Path $repoRoot "bin\$platformDirectory\Funke-Engine"
$engineDll = Join-Path $engineBinaryDirectory "Funke-Engine.dll"
$engineLib = Join-Path $engineBinaryDirectory "Funke-Engine.lib"
$builtSdlDll = Join-Path $repoRoot "External\SDL-build\windows-x64\install\bin\SDL3.dll"
$netHostDll = Join-Path $repoRoot "External\dotnet\lib\nethost.dll"
$assetsSource = Join-Path $repoRoot "Funke-Runtime\FunkeAssets"

function Assert-File {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required file was not found: $Path"
    }
}

function Assert-Directory {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "Required directory was not found: $Path"
    }
}

function Write-Utf8File {
    param(
        [string]$Path,
        [string]$Content
    )

    $parent = Split-Path -Parent $Path
    [System.IO.Directory]::CreateDirectory($parent) | Out-Null
    [System.IO.File]::WriteAllText(
        $Path,
        $Content,
        [System.Text.UTF8Encoding]::new($false)
    )
}

function Copy-HeaderTree {
    param(
        [string]$Source,
        [string]$Destination
    )

    Assert-Directory $Source
    $sourceRoot = [System.IO.Path]::GetFullPath($Source).TrimEnd('\')
    $headerExtensions = @(".h", ".hpp", ".inl", ".inc")

    foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File) {
        if ($headerExtensions -notcontains $file.Extension.ToLowerInvariant()) {
            continue
        }

        $relativePath = $file.FullName.Substring($sourceRoot.Length).TrimStart('\')
        $destinationPath = Join-Path $Destination $relativePath
        $destinationParent = Split-Path -Parent $destinationPath
        [System.IO.Directory]::CreateDirectory($destinationParent) | Out-Null

        if (Test-Path -LiteralPath $destinationPath -PathType Leaf) {
            $sourceHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
            $destinationHash = (Get-FileHash -LiteralPath $destinationPath -Algorithm SHA256).Hash
            if ($sourceHash -ne $destinationHash) {
                throw "SDK header collision at '$relativePath'."
            }
            continue
        }

        Copy-Item -LiteralPath $file.FullName -Destination $destinationPath
    }
}

function Find-VisualStudioTool {
    param(
        [string]$FindPattern,
        [string]$DisplayName
    )

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    Assert-File $vswhere
    $tool = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find $FindPattern |
        Select-Object -First 1
    if (-not $tool -or -not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "$DisplayName was not found in the latest Visual Studio installation."
    }
    return $tool
}

Assert-File $engineDll
Assert-File $engineLib
Assert-File $netHostDll
Assert-Directory $assetsSource
$engineSha256 = (Get-FileHash -LiteralPath $engineDll -Algorithm SHA256).Hash.ToLowerInvariant()

$dumpbin = Find-VisualStudioTool "VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe" "dumpbin.exe"
$sceneManagerGetActiveSceneSymbol = "?GetActiveScene@SceneManager@Funke@@SAPEAVScene@2@XZ"
$sceneManagerTryLoadSceneSymbol = '?TryLoadScene@SceneManager@Funke@@SA?AUSceneLoadResult@2@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z'
$sceneManagerTryLoadSceneAdditiveSymbol = '?TryLoadSceneAdditive@SceneManager@Funke@@SA?AUSceneLoadResult@2@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z'
$sceneManagerTryReloadSceneSymbol = '?TryReloadScene@SceneManager@Funke@@SA?AUSceneLoadResult@2@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@Z'
$renderSettingsApplySymbol = "?Apply@RenderSettings@Funke@@"
$engineExports = (& $dumpbin /exports $engineDll 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or
    -not $engineExports.Contains($sceneManagerGetActiveSceneSymbol) -or
    -not $engineExports.Contains($sceneManagerTryLoadSceneSymbol) -or
    -not $engineExports.Contains($sceneManagerTryLoadSceneAdditiveSymbol) -or
    -not $engineExports.Contains($sceneManagerTryReloadSceneSymbol) -or
    -not $engineExports.Contains($renderSettingsApplySymbol)) {
    throw "Funke-Engine.dll has a stale native SDK ABI. Rebuild Funke-Engine Dist|x64 before packaging the SDK."
}
$engineLibrarySymbols = (& $dumpbin /linkermember:2 $engineLib 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or
    -not $engineLibrarySymbols.Contains($sceneManagerGetActiveSceneSymbol) -or
    -not $engineLibrarySymbols.Contains($sceneManagerTryLoadSceneSymbol) -or
    -not $engineLibrarySymbols.Contains($sceneManagerTryLoadSceneAdditiveSymbol) -or
    -not $engineLibrarySymbols.Contains($sceneManagerTryReloadSceneSymbol) -or
    -not $engineLibrarySymbols.Contains($renderSettingsApplySymbol)) {
    throw "Funke-Engine.lib has a stale native SDK ABI. Rebuild Funke-Engine Dist|x64 before packaging the SDK."
}
$engineDependencies = (& $dumpbin /dependents $engineDll 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "Could not inspect Funke-Engine.dll dependencies."
}
$profilerEnabled = $engineDependencies -match "(?im)^\s*Tracy\.dll\s*$"
if ($engineDependencies -notmatch "(?im)^\s*SDL3\.dll\s*$") {
    throw "Funke-Engine.dll does not declare the SDL3 runtime dependency."
}
$sdlDll = Join-Path $engineBinaryDirectory "SDL3.dll"
if (-not (Test-Path -LiteralPath $sdlDll -PathType Leaf)) {
    $sdlDll = $builtSdlDll
}
Assert-File $sdlDll

if (Test-Path -LiteralPath $sdkRoot) {
    if (-not (Test-Path -LiteralPath $sdkMarker -PathType Leaf)) {
        throw "Refusing to replace an unrecognized directory: $sdkRoot"
    }
    Remove-Item -LiteralPath $sdkRoot -Recurse -Force
}

$includeDirectory = Join-Path $sdkRoot "include"
$libraryDirectory = Join-Path $sdkRoot "lib"
$runtimeDirectory = Join-Path $sdkRoot "runtime"
$licensesDirectory = Join-Path $sdkRoot "licenses"
$exampleDirectory = Join-Path $sdkRoot "examples\MinimalApp"
$packageExampleDirectory = Join-Path $sdkRoot "examples\NativePackage"

foreach ($directory in @(
    $includeDirectory,
    $libraryDirectory,
    $runtimeDirectory,
    $licensesDirectory,
    $exampleDirectory,
    $packageExampleDirectory
)) {
    [System.IO.Directory]::CreateDirectory($directory) | Out-Null
}
Write-Utf8File $sdkMarker "Funke Native SDK`n"

$headerRoots = @(
    "Funke-Engine\src",
    "External\spdlog\include",
    "External\glm",
    "External\entt\src",
    "External\stb",
    "External\magic_enum\include",
    "External\rapidxml",
    "External\concurrentqueue",
    "External\SDL\include",
    "External\dawn\include",
    "External\dawn\build\gen\include",
    "External\miniaudio",
    "External\box2d\include",
    "External\Index-Physics\include",
    "External\dotnet",
    "External\imgui",
    "External\ImGuizmo"
)

if ($profilerEnabled) {
    $headerRoots += "External\tracy\public"
}

foreach ($relativeRoot in $headerRoots) {
    Copy-HeaderTree (Join-Path $repoRoot $relativeRoot) $includeDirectory
}

$entityConfigSource = Join-Path $repoRoot "Funke-Engine\src\Generated\FunkeEntityBitsConfig.h"
Assert-File $entityConfigSource
Copy-Item -LiteralPath $entityConfigSource -Destination (Join-Path $includeDirectory "FunkeEntityBitsConfig.h")

$entityConfigText = Get-Content -Raw -LiteralPath $entityConfigSource
$entityBitsMatch = [regex]::Match($entityConfigText, "(?m)^#define\s+FUNKE_ENTITY_BITS\s+(\d+)\s*$")
if (-not $entityBitsMatch.Success) {
    throw "Could not read FUNKE_ENTITY_BITS from $entityConfigSource"
}
$entityBits = $entityBitsMatch.Groups[1].Value

Copy-Item -LiteralPath $engineLib -Destination (Join-Path $libraryDirectory "Funke-Engine.lib")
Copy-Item -LiteralPath $engineDll -Destination (Join-Path $runtimeDirectory "Funke-Engine.dll")
Copy-Item -LiteralPath $sdlDll -Destination (Join-Path $runtimeDirectory "SDL3.dll")
Copy-Item -LiteralPath $netHostDll -Destination (Join-Path $runtimeDirectory "nethost.dll")

if ($profilerEnabled) {
    $tracyDirectory = Join-Path $repoRoot "bin\$platformDirectory\Tracy"
    $tracyLib = Join-Path $tracyDirectory "Tracy.lib"
    $tracyDll = Join-Path $tracyDirectory "Tracy.dll"
    Assert-File $tracyLib
    Assert-File $tracyDll
    Copy-Item -LiteralPath $tracyLib -Destination (Join-Path $libraryDirectory "Tracy.lib")
    Copy-Item -LiteralPath $tracyDll -Destination (Join-Path $runtimeDirectory "Tracy.dll")
}

Copy-Item -LiteralPath $assetsSource -Destination (Join-Path $runtimeDirectory "FunkeAssets") -Recurse

$licenseFiles = @(
    @{ Source = "LICENSE.txt"; Destination = "Funke\LICENSE.txt" },
    @{ Source = "THIRD_PARTY_LICENSES.md"; Destination = "Funke\THIRD_PARTY_LICENSES.md" },
    @{ Source = "External\spdlog\LICENSE"; Destination = "spdlog\LICENSE" },
    @{ Source = "External\glm\copying.txt"; Destination = "GLM\copying.txt" },
    @{ Source = "External\entt\LICENSE"; Destination = "EnTT\LICENSE" },
    @{ Source = "External\stb\LICENSE"; Destination = "stb\LICENSE" },
    @{ Source = "External\magic_enum\LICENSE"; Destination = "magic_enum\LICENSE" },
    @{ Source = "External\rapidxml\LICENSE.txt"; Destination = "RapidXML\LICENSE.txt" },
    @{ Source = "External\concurrentqueue\LICENSE.md"; Destination = "concurrentqueue\LICENSE.md" },
    @{ Source = "External\SDL\LICENSE.txt"; Destination = "SDL3\LICENSE.txt" },
    @{ Source = "External\dawn\LICENSE"; Destination = "Dawn\LICENSE" },
    @{ Source = "External\miniaudio\LICENSE"; Destination = "miniaudio\LICENSE" },
    @{ Source = "External\box2d\LICENSE"; Destination = "Box2D\LICENSE" },
    @{ Source = "External\JoltPhysics\LICENSE"; Destination = "JoltPhysics\LICENSE" },
    @{ Source = "External\imgui\LICENSE.txt"; Destination = "Dear-ImGui\LICENSE.txt" },
    @{ Source = "External\ImGuizmo\LICENSE"; Destination = "ImGuizmo\LICENSE" }
)

if ($profilerEnabled) {
    $licenseFiles += @{ Source = "External\tracy\LICENSE"; Destination = "Tracy\LICENSE" }
}

foreach ($license in $licenseFiles) {
    $source = Join-Path $repoRoot $license.Source
    Assert-File $source
    $destination = Join-Path $licensesDirectory $license.Destination
    [System.IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}

$profilerDefines = ""
$profilerLibraries = ""
if ($profilerEnabled) {
    $profilerDefines = "FUNKE_PROFILER_ENABLED;TRACY_ENABLE;TRACY_IMPORTS;TRACY_ON_DEMAND;"
    $profilerLibraries = "Tracy.lib;"
}

$propertySheet = @'
<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup Label="UserMacros">
    <FunkeSdkRoot>$(MSBuildThisFileDirectory)</FunkeSdkRoot>
  </PropertyGroup>
  <PropertyGroup>
    <CharacterSet>Unicode</CharacterSet>
    <ExternalIncludePath>$(FunkeSdkRoot)include;$(ExternalIncludePath)</ExternalIncludePath>
    <FunkeSdkPreviousForceImportAfterCppTargets Condition="'$(ForceImportAfterCppTargets)' != ''">$(ForceImportAfterCppTargets)</FunkeSdkPreviousForceImportAfterCppTargets>
    <ForceImportAfterCppTargets>$(FunkeSdkRoot)FunkeSDK.targets</ForceImportAfterCppTargets>
  </PropertyGroup>
</Project>
'@
Write-Utf8File (Join-Path $sdkRoot "FunkeSDK.props") $propertySheet

$targetsFile = @'
<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Project="$(FunkeSdkPreviousForceImportAfterCppTargets)" Condition="'$(FunkeSdkPreviousForceImportAfterCppTargets)' != '' and '$(FunkeSdkPreviousForceImportAfterCppTargets)' != '$(FunkeSdkRoot)FunkeSDK.targets' and Exists('$(FunkeSdkPreviousForceImportAfterCppTargets)')" />
  <ItemDefinitionGroup>
    <ClCompile>
      <PreprocessorDefinitions>FNK_IMPORT_DLL;FNK_PLATFORM_WINDOWS;FNK_DIST;NDEBUG;FNK_RHI_WEBGPU;WEBGPU_CPP_HAS_ENUM_CLASS_BITMASKS=1;IMGUI_IMPL_WEBGPU_BACKEND_DAWN;FUNKE_WITH_RENDER=1;FUNKE_WITH_AUDIO=1;FUNKE_WITH_PHYSICS=1;FUNKE_WITH_SCRIPTING=1;FUNKE_WITH_APPLICATION=1;FUNKE_WITH_EDITOR=0;FUNKE_ENTITY_BITS=@@ENTITY_BITS@@;MAGIC_ENUM_RANGE_MIN=-1;MAGIC_ENUM_RANGE_MAX=32;FUNKE_ALL_MODULES=1;@@PROFILER_DEFINES@@%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <LanguageStandard>stdcpp20</LanguageStandard>
      <RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>
      <ExternalWarningLevel>TurnOffAllWarnings</ExternalWarningLevel>
      <AdditionalOptions>/utf-8 /FS /Zc:preprocessor %(AdditionalOptions)</AdditionalOptions>
    </ClCompile>
    <Link>
      <AdditionalLibraryDirectories>$(FunkeSdkRoot)lib;%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <AdditionalDependencies>Funke-Engine.lib;@@PROFILER_LIBRARIES@@shell32.lib;gdi32.lib;user32.lib;%(AdditionalDependencies)</AdditionalDependencies>
      <SubSystem>Windows</SubSystem>
    </Link>
  </ItemDefinitionGroup>
  <Target Name="CopyFunkeSdkRuntime" AfterTargets="Build" Condition="'$(FunkeSdkDeployRuntime)' != 'false'">
    <ItemGroup>
      <FunkeSdkRuntimeDll Include="$(FunkeSdkRoot)runtime\*.dll" />
      <FunkeSdkAsset Include="$(FunkeSdkRoot)runtime\FunkeAssets\**\*" />
    </ItemGroup>
    <Copy SourceFiles="@(FunkeSdkRuntimeDll)" DestinationFolder="$(OutDir)" SkipUnchangedFiles="true" />
    <Copy SourceFiles="@(FunkeSdkAsset)" DestinationFiles="@(FunkeSdkAsset-&gt;'$(OutDir)FunkeAssets\%(RecursiveDir)%(Filename)%(Extension)')" SkipUnchangedFiles="true" />
  </Target>
</Project>
'@
$targetsFile = $targetsFile.Replace("@@ENTITY_BITS@@", $entityBits)
$targetsFile = $targetsFile.Replace("@@PROFILER_DEFINES@@", $profilerDefines)
$targetsFile = $targetsFile.Replace("@@PROFILER_LIBRARIES@@", $profilerLibraries)
Write-Utf8File (Join-Path $sdkRoot "FunkeSDK.targets") $targetsFile

$packagePropertySheet = @'
<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Project="$(MSBuildThisFileDirectory)FunkeSDK.props" />
  <PropertyGroup>
    <FunkeSdkDeployRuntime>false</FunkeSdkDeployRuntime>
    <FunkePackagePreviousForceImportAfterCppTargets>$(ForceImportAfterCppTargets)</FunkePackagePreviousForceImportAfterCppTargets>
    <ForceImportAfterCppTargets>$(FunkeSdkRoot)FunkePackageSDK.targets</ForceImportAfterCppTargets>
  </PropertyGroup>
</Project>
'@
Write-Utf8File (Join-Path $sdkRoot "FunkePackageSDK.props") $packagePropertySheet

$packageTargets = @'
<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Project="$(FunkePackagePreviousForceImportAfterCppTargets)" Condition="'$(FunkePackagePreviousForceImportAfterCppTargets)' != '' and '$(FunkePackagePreviousForceImportAfterCppTargets)' != '$(FunkeSdkRoot)FunkePackageSDK.targets' and Exists('$(FunkePackagePreviousForceImportAfterCppTargets)')" />
  <Target Name="WriteFunkePackageAbiMetadata" AfterTargets="Link" Condition="'$(ConfigurationType)' == 'DynamicLibrary'">
    <ItemGroup>
      <FunkePackageAbiLine Include="schema=1" />
      <FunkePackageAbiLine Include="boundary=engine_cpp" />
      <FunkePackageAbiLine Include="engine_sha256=@@ENGINE_SHA256@@" />
    </ItemGroup>
    <WriteLinesToFile File="$(TargetPath).funkeabi" Lines="@(FunkePackageAbiLine)" Overwrite="true" Encoding="ASCII" />
  </Target>
</Project>
'@
$packageTargets = $packageTargets.Replace("@@ENGINE_SHA256@@", $engineSha256)
Write-Utf8File (Join-Path $sdkRoot "FunkePackageSDK.targets") $packageTargets

$exampleSource = @'
#include <Funke.hpp>

#include <Core/PackageHost.hpp>
#include <Project/ProjectManager.hpp>
#include <Serialization/Path.hpp>
#include <ExamplePackage.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include <windows.h>

using namespace Funke;

namespace {

Entity AddDemoSprite(Scene& scene, const char* name, const Vec2& position,
    const Vec2& scale, const Color& color, short sortingOrder,
    TextureHandle texture = {})
{
    Entity entity = scene.CreateEntity(name);
    auto& transform = entity.GetComponent<Transform2DComponent>();
    transform.SetPosition(position);
    transform.SetScale(scale);
    auto& sprite = entity.AddComponent<SpriteRendererComponent>();
    sprite.Color = color;
    sprite.SortingOrder = sortingOrder;
    sprite.TextureHandle = texture;
    return entity;
}

void AddPixelGlyph(Scene& scene, const char* name, std::array<uint8_t, 7> rows,
    const Vec2& topLeft, float pixelSize, const Color& color)
{
    const TextureHandle square = TextureManager::GetDefaultTexture(DefaultTexture::Square);
    for (size_t row = 0; row < rows.size(); ++row) {
        for (int column = 0; column < 5; ++column) {
            if ((rows[row] & (1u << (4 - column))) == 0) continue;
            AddDemoSprite(scene, name,
                { topLeft.x + column * pixelSize, topLeft.y - static_cast<float>(row) * pixelSize },
                { pixelSize * 0.78f, pixelSize * 0.78f }, color, 20, square);
        }
    }
}

void PopulateDemoScene(Scene& scene, bool packageReady)
{
    const TextureHandle square = TextureManager::GetDefaultTexture(DefaultTexture::Square);
    const TextureHandle circle = TextureManager::GetDefaultTexture(DefaultTexture::Circle);
    AddDemoSprite(scene, "Panel", { 0.0f, 0.0f }, { 12.0f, 6.5f },
        { 0.055f, 0.075f, 0.13f, 1.0f }, -100, square);
    AddDemoSprite(scene, "Header", { 0.0f, 2.35f }, { 12.0f, 1.8f },
        { 0.08f, 0.18f, 0.36f, 1.0f }, -90, square);
    AddDemoSprite(scene, "Accent", { -5.45f, 0.0f }, { 0.18f, 5.3f },
        { 0.18f, 0.58f, 1.0f, 1.0f }, -80, square);

    const Color titleColor{ 0.88f, 0.95f, 1.0f, 1.0f };
    const Color sdkColor{ 0.25f, 0.75f, 1.0f, 1.0f };
    constexpr float pixel = 0.17f;
    AddPixelGlyph(scene, "I", { 31, 4, 4, 4, 4, 4, 31 }, { -4.7f, 2.85f }, pixel, titleColor);
    AddPixelGlyph(scene, "N", { 17, 25, 25, 21, 19, 19, 17 }, { -3.68f, 2.85f }, pixel, titleColor);
    AddPixelGlyph(scene, "D", { 30, 17, 17, 17, 17, 17, 30 }, { -2.66f, 2.85f }, pixel, titleColor);
    AddPixelGlyph(scene, "E", { 31, 16, 16, 30, 16, 16, 31 }, { -1.64f, 2.85f }, pixel, titleColor);
    AddPixelGlyph(scene, "X", { 17, 10, 4, 4, 4, 10, 17 }, { -0.62f, 2.85f }, pixel, titleColor);
    AddPixelGlyph(scene, "S", { 15, 16, 16, 14, 1, 1, 30 }, { 1.0f, 2.85f }, pixel, sdkColor);
    AddPixelGlyph(scene, "D", { 30, 17, 17, 17, 17, 17, 30 }, { 2.02f, 2.85f }, pixel, sdkColor);
    AddPixelGlyph(scene, "K", { 17, 18, 20, 24, 20, 18, 17 }, { 3.04f, 2.85f }, pixel, sdkColor);

    AddDemoSprite(scene, "Status Bar", { 0.0f, 0.55f }, { 8.5f, 0.85f },
        { 0.035f, 0.12f, 0.20f, 1.0f }, -60, square);
    AddDemoSprite(scene, "Package Status", { -3.65f, 0.55f }, { 0.42f, 0.42f },
        packageReady ? Color{ 0.25f, 0.92f, 0.55f, 1.0f }
                     : Color{ 1.0f, 0.28f, 0.30f, 1.0f }, -40, circle);
    AddDemoSprite(scene, "Status Line 1", { -2.1f, 0.68f }, { 2.25f, 0.16f },
        { 0.58f, 0.72f, 0.9f, 1.0f }, -40, square);
    AddDemoSprite(scene, "Status Line 2", { -2.65f, 0.38f }, { 1.15f, 0.12f },
        { 0.25f, 0.48f, 0.72f, 1.0f }, -40, square);

    const std::array<Vec2, 3> cardPositions{ Vec2{ -3.0f, -1.25f }, Vec2{ 0.0f, -1.25f }, Vec2{ 3.0f, -1.25f } };
    const std::array<Color, 3> cardColors{
        Color{ 0.08f, 0.22f, 0.38f, 1.0f },
        Color{ 0.10f, 0.27f, 0.31f, 1.0f },
        Color{ 0.22f, 0.16f, 0.36f, 1.0f }
    };
    for (size_t i = 0; i < cardPositions.size(); ++i) {
        AddDemoSprite(scene, "Feature Card", cardPositions[i], { 2.35f, 1.45f },
            cardColors[i], -60, square);
        AddDemoSprite(scene, "Feature Icon", { cardPositions[i].x, cardPositions[i].y + 0.12f },
            { 0.55f, 0.55f },
            i == 0 ? Color{ 0.2f, 0.68f, 1.0f, 1.0f }
                   : (i == 1 ? Color{ 0.25f, 0.92f, 0.55f, 1.0f }
                             : Color{ 0.76f, 0.48f, 1.0f, 1.0f }),
            -40, i == 1 ? circle : square);
    }
}

class MainLayer final : public Layer {
public:
    MainLayer()
        : Layer("Main")
    {
    }

    void OnUpdate(Application&, float) override
    {
        if (Input::GetKeyDown(KeyCode::Esc)) {
            Application::Quit();
        }
    }
};

class MinimalApp final : public Application {
public:
    MinimalApp()
    {
        const CommandLineArgs args = GetCommandLineArgs();
        for (int i = 1; i < args.Count; ++i) {
            if (args[i] && std::string_view(args[i]) == "--smoke-test") {
                m_SmokeTest = true;
            }
        }
    }

    ~MinimalApp() override
    {
        if (m_SmokeTest && !m_SmokeCompleted) {
            std::abort();
        }
    }

    ApplicationConfig GetConfiguration() const override
    {
        auto config = ApplicationConfig::Minimal();
        config.WindowSpecification = WindowSpecification(
            960, 540, "Funke Native SDK", true, true, false);
        config.EnableRenderer2D = true;
        config.EnableShaderManager = true;
        config.EnableTextureManager = true;
        config.EnablePackageHost = true;
        config.Graphics.RenderScale = 1.0f;
        config.Graphics.UpscaleFilter = UpscaleFilterMode::Linear;
        config.Graphics.PostProcessingQuality = PostProcessingQualityLevel::High;
        config.Graphics.ParticleQuality = ParticleQualityLevel::High;
        config.SetWindowIcon = false;
        config.Vsync = true;
        return config;
    }

    void ConfigureScenes() override
    {
        Application::SetIsPlaying(true);
        SceneManager::RegisterScene("Main").SetAsStartupScene();
    }

    void ConfigureLayers() override { PushLayer<MainLayer>(); }
    void Start() override
    {
        FunkeExamplePackage_IsLoadedFn isLoaded = nullptr;
        for (const LoadedPackage& package : PackageHost::GetLoaded()) {
            if (package.Name == "Example") {
                isLoaded = reinterpret_cast<FunkeExamplePackage_IsLoadedFn>(
                    ::GetProcAddress(static_cast<HMODULE>(package.ModuleHandle),
                        "FunkeExamplePackage_IsLoaded"));
                break;
            }
        }
        const bool packageReady = isLoaded && isLoaded() == 1;
        if (m_SmokeTest && !packageReady) {
            std::abort();
        }

        SetTargetFramerate(144.0f);
        Camera2DComponent* camera = Camera2DComponent::GetMain();
        Scene* scene = SceneManager::GetActiveScene();
        if (!camera || !scene) {
            if (m_SmokeTest) std::abort();
            return;
        }
        if (m_SmokeTest) {
            const EntityHandle cameraEntity = camera->GetOwnerEntity();
            if (!camera->IsValid()
                || camera->GetOwnerScene() != scene
                || cameraEntity == entt::null
                || !scene->IsValid(cameraEntity)
                || !scene->HasComponent<Camera2DComponent>(cameraEntity)
                || scene->GetEntityOrigin(cameraEntity) != EntityOrigin::Runtime) {
                std::abort();
            }
        }
        camera->SetOrthographicSize(10.0f);
        camera->SetClearColor(Color{ 0.02f, 0.03f, 0.05f, 1.0f });
        camera->SetPostProcessingEnabled(false);
        PopulateDemoScene(*scene, packageReady);

        if (m_SmokeTest) {
            Entity componentProbe = scene->CreateEmptyEntity();
            NameComponent* missingName = nullptr;
            bool missingGetThrew = false;
            try {
                (void)componentProbe.GetComponent<NameComponent>();
            }
            catch (const FunkeException&) {
                missingGetThrew = true;
            }
            catch (...) {
                std::abort();
            }
            if (!componentProbe.IsValid()
                || componentProbe.HasComponent<NameComponent>()
                || componentProbe.TryGetComponent(missingName)
                || missingName != nullptr
                || !missingGetThrew) {
                std::abort();
            }
            componentProbe.Destroy();

            Entity staleEntity = Entity::Null;
            {
                auto detached = Scene::CreateDetachedScene("SDK Lifetime Probe");
                staleEntity = detached->CreateEmptyEntity();
            }
            bool staleGetThrew = false;
            try {
                (void)staleEntity.GetComponent<NameComponent>();
            }
            catch (const FunkeException&) {
                staleGetThrew = true;
            }
            catch (...) {
                std::abort();
            }
            if (staleEntity.IsValid()
                || staleEntity.GetScene() != nullptr
                || staleEntity.GetRegistry() != nullptr
                || staleEntity.HasComponent<NameComponent>()
                || !staleGetThrew) {
                std::abort();
            }

            Scene* const activeBeforeFailedLoad = SceneManager::GetActiveScene();
            const size_t sceneCountBeforeFailedLoad = SceneManager::GetLoadedSceneCount();
            const SceneLoadResult failedLoad = SceneManager::TryLoadScene("MissingNativeSdkScene");
            if (failedLoad
                || failedLoad.Status != SceneLoadStatus::NotRegistered
                || !failedLoad.LoadedScene.expired()
                || failedLoad.Message.empty()
                || SceneManager::GetActiveScene() != activeBeforeFailedLoad
                || SceneManager::GetLoadedSceneCount() != sceneCountBeforeFailedLoad) {
                std::abort();
            }

            SceneManager::RegisterScene("Additive");
            auto additive = SceneManager::LoadSceneAdditive("Additive").lock();
            if (!additive
                || additive->GetMainCamera() != nullptr
                || !SceneManager::SetActiveScene("Additive")
                || additive->GetMainCamera() == nullptr) {
                std::abort();
            }

            SceneManager::RegisterScene("CameraFree").WithoutDefaultCamera2D();
            auto cameraFree = SceneManager::LoadSceneAdditive("CameraFree").lock();
            if (!cameraFree || !SceneManager::SetActiveScene("CameraFree")
                || cameraFree->GetMainCamera() != nullptr) {
                std::abort();
            }
            if (!SceneManager::SetActiveScene("Main")
                || Camera2DComponent::GetMain() == nullptr) {
                std::abort();
            }
        }
    }
    void Update() override
    {
        if (m_SmokeTest && ++m_Frames >= 120) {
            m_SmokeCompleted = true;
            Quit();
        }
    }
    void FixedUpdate() override {}
    void OnPaused() override {}
    void OnQuit() override {}

private:
    bool m_SmokeTest = false;
    bool m_SmokeCompleted = false;
    int m_Frames = 0;
};

}

Funke::Application* Funke::CreateApplication()
{
    const std::filesystem::path appRoot(Path::ExecutableDir());
    const Application::CommandLineArgs args = Application::GetCommandLineArgs();
    bool smokeTest = false;
    for (int i = 1; i < args.Count; ++i) {
        if (args[i] && std::string_view(args[i]) == "--smoke-test") {
            smokeTest = true;
        }
    }

    auto project = std::make_unique<FunkeProject>();
    project->Name = "Native SDK Example";
    project->RootDirectory = appRoot.string();
    project->AssetsDirectory = (appRoot / "Assets").string();
    project->PackagesDirectory = (appRoot / "Packages").string();
    project->Packages = { "Example" };
    ProjectManager::SetCurrentProject(std::move(project));
    return new MinimalApp();
}

#include <EntryPoint.hpp>
'@
Write-Utf8File (Join-Path $exampleDirectory "MinimalApp.cpp") $exampleSource

$exampleProject = @'
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Dist|x64">
      <Configuration>Dist</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <ProjectGuid>{53677516-6F0A-49C2-AE2F-5A7A5281D0DC}</ProjectGuid>
    <Keyword>Win32Proj</Keyword>
    <RootNamespace>MinimalApp</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Dist|x64'" Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <WholeProgramOptimization>true</WholeProgramOptimization>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <Import Project="..\..\FunkeSDK.props" />
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Dist|x64'">
    <OutDir>$(ProjectDir)bin\Dist\</OutDir>
    <IntDir>$(ProjectDir)obj\Dist\</IntDir>
  </PropertyGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Dist|x64'">
    <ClCompile>
      <AdditionalIncludeDirectories>$(ProjectDir)..\NativePackage\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <Optimization>Full</Optimization>
      <WarningLevel>Level4</WarningLevel>
    </ClCompile>
    <Link>
      <SubSystem>Console</SubSystem>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="MinimalApp.cpp" />
  </ItemGroup>
  <ItemGroup>
    <ProjectReference Include="..\NativePackage\Pkg.Example.Native.vcxproj">
      <Project>{E2820A38-7668-4B4A-9217-0D694AC61E13}</Project>
      <LinkLibraryDependencies>false</LinkLibraryDependencies>
      <ReferenceOutputAssembly>false</ReferenceOutputAssembly>
    </ProjectReference>
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
'@
Write-Utf8File (Join-Path $exampleDirectory "MinimalApp.vcxproj") $exampleProject

$packageHeader = @'
#pragma once

#if defined(_WIN32)
#if defined(FUNKE_EXAMPLE_PACKAGE_BUILD)
#define FUNKE_EXAMPLE_PACKAGE_API __declspec(dllexport)
#else
#define FUNKE_EXAMPLE_PACKAGE_API __declspec(dllimport)
#endif
#else
#define FUNKE_EXAMPLE_PACKAGE_API __attribute__((visibility("default")))
#endif

extern "C" FUNKE_EXAMPLE_PACKAGE_API int FunkeExamplePackage_IsLoaded();
using FunkeExamplePackage_IsLoadedFn = int (*)();
'@
Write-Utf8File (Join-Path $packageExampleDirectory "include\ExamplePackage.hpp") $packageHeader

$packageSource = @'
#include "ExamplePackage.hpp"

#include <Core/Log.hpp>
#include <Packages/PackageApi.hpp>

namespace {
    bool s_Loaded = false;
}

extern "C" FUNKE_PACKAGE_API int FunkePackage_OnLoad()
{
    s_Loaded = true;
    FNK_CORE_INFO_TAG("NativePackage", "Loaded the FunkeSDK native package example.");
    return 0;
}

extern "C" FUNKE_PACKAGE_API void FunkePackage_OnUnload()
{
    s_Loaded = false;
}

extern "C" FUNKE_EXAMPLE_PACKAGE_API int FunkeExamplePackage_IsLoaded()
{
    return s_Loaded ? 1 : 0;
}
'@
Write-Utf8File (Join-Path $packageExampleDirectory "src\ExamplePackage.cpp") $packageSource

$packageProject = @'
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Dist|x64">
      <Configuration>Dist</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <ProjectGuid>{E2820A38-7668-4B4A-9217-0D694AC61E13}</ProjectGuid>
    <Keyword>Win32Proj</Keyword>
    <RootNamespace>Pkg.Example.Native</RootNamespace>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Dist|x64'" Label="Configuration">
    <ConfigurationType>DynamicLibrary</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v143</PlatformToolset>
    <WholeProgramOptimization>true</WholeProgramOptimization>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <Import Project="..\..\FunkePackageSDK.props" />
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Dist|x64'">
    <TargetName>Pkg.Example.Native</TargetName>
    <OutDir>$(ProjectDir)..\MinimalApp\bin\Dist\Packages\Example\Bin\Windows-x64\</OutDir>
    <IntDir>$(ProjectDir)obj\Dist\</IntDir>
  </PropertyGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Dist|x64'">
    <ClCompile>
      <AdditionalIncludeDirectories>$(ProjectDir)include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>FUNKE_EXAMPLE_PACKAGE_BUILD;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <Optimization>Full</Optimization>
      <WarningLevel>Level4</WarningLevel>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="src\ExamplePackage.cpp" />
    <ClInclude Include="include\ExamplePackage.hpp" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
'@
Write-Utf8File (Join-Path $packageExampleDirectory "Pkg.Example.Native.vcxproj") $packageProject

$packageManifest = @'
return {
    name = "Example",
    version = "0.1.0",
    description = "Minimal native package built against the exact FunkeSDK engine binary.",
    layers = {
        native = {
            sources = { "src/**.cpp", "src/**.hpp", "include/**.hpp" },
            includes = { "include" },
        },
    },
    dependencies = {},
}
'@
Write-Utf8File (Join-Path $packageExampleDirectory "funke-package.lua") $packageManifest

$metadata = [ordered]@{
    configuration = $Configuration
    platform = "windows-x86_64"
    profiler = $profilerEnabled
    entityBits = [int]$entityBits
    engineSha256 = $engineSha256
    nativePackageAbiSchema = 1
    includeRootsRequiredByConsumer = 1
    importLibrariesRequiredByConsumer = 1 + [int]$profilerEnabled
}
Write-Utf8File (Join-Path $sdkRoot "FunkeSDK.json") ($metadata | ConvertTo-Json)

$sdkReadme = @'
# Funke Native SDK

For a Visual Studio 2022 C++ project:

1. Open View > Other Windows > Property Manager.
2. Add the existing property sheet `FunkeSDK.props` to `Dist | x64`.
3. Include `<Funke.hpp>` and place `<EntryPoint.hpp>` in exactly one `.cpp`.

The property sheet supplies the single include root and loads `FunkeSDK.targets`
after the project settings. The targets file applies compiler defines, the import
library, the Windows subsystem, system libraries, and post-build deployment of
runtime DLLs and `FunkeAssets`.

Treat the generated headers, import library, and engine DLL as one versioned set.
Replace them together and rebuild every raw C++ host whenever the SDK changes.

Scene-manager and scene/entity structural operations are main-thread-only. Component
references and pointers returned by raw ECS APIs are borrowed; do not retain them
across component changes, entity destruction, or scene unload/reload.

The active scene receives a runtime `Camera2D` before its systems start when the
2D renderer is enabled and the scene did not create one during `OnLoad`. Call
`WithoutDefaultCamera2D()` on a scene definition to opt out.

Use `TryLoadScene`, `TryLoadSceneAdditive`, or `TryReloadScene` when the caller
needs structured failure details. `SceneLoadResult::Succeeded()` is true for
`Success` and `AlreadyLoaded`. Other statuses are `ReentrantLoad`,
`NotInitialized`, `NotRegistered`, `NotLoaded`, `InstantiationFailed`,
`LoadCallbackFailed`, `SetupFailed`, `BeforeStartFailed`, `AwakeFailed`,
`StartFailed`, `CommitFailed`, `AfterStartFailed`, `SnapshotFailed`, and
`WrongThread`.
`Message` carries context and `LoadedScene` is expired when no usable scene was
produced. A failed `TryReloadScene` can return the preserved original scene, so
always check the result before treating it as reload success. The legacy
`LoadScene*` methods remain available and return an expired pointer on failure.

A non-additive load stages and validates its replacement before unloading the
current non-persistent scenes. Failure preserves the previous loaded scene set
and active scene. Use `OnLoadChecked` for deserialization or validation work;
returning `false` rejects the staged scene with `LoadCallbackFailed`.

`TryReloadScene` keeps the same logical scene identity and does not emit scene
loaded/unloaded events for its internal instance swap.

Set startup render quality through `ApplicationConfig::Graphics`. At runtime,
main-thread code can use `RenderSettings::SetRenderScale(0.75f)` and the other
individual setters, or update a `GraphicsSettings` snapshot with
`RenderSettings::Apply`. Accepted changes become effective at the next frame
boundary; render scale is clamped to `0.25f` through `1.0f`.

`examples/MinimalApp/MinimalApp.vcxproj` is a buildable reference project.
It references `examples/NativePackage/Pkg.Example.Native.vcxproj`, stages the
DLL in the PackageHost layout, and verifies during `--smoke-test` that the
package loaded before startup-scene deserialization.

For your own engine-linked package DLL, import `FunkePackageSDK.props`, name
the output `Pkg.<Name>.Native.dll`, and keep it beside the generated
`.dll.funkeabi` file. Native packages use Funke's C++ ABI and therefore must be
rebuilt whenever the SDK changes. The sidecar makes a stale binary fail before
`LoadLibrary` instead of crashing inside package initialization. Package public
headers and the generated import `.lib` can also be consumed normally by C++;
including a header alone does not run `FunkePackage_OnLoad`.
'@
Write-Utf8File (Join-Path $sdkRoot "README.md") $sdkReadme

if ($Verify) {
    $msbuild = Find-VisualStudioTool "MSBuild\**\Bin\MSBuild.exe" "MSBuild.exe"
    $exampleProjectPath = Join-Path $exampleDirectory "MinimalApp.vcxproj"
    & $msbuild $exampleProjectPath /m /nr:false /p:Configuration=Dist /p:Platform=x64 /p:PreferredToolArchitecture=x64 /v:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "Native SDK example build failed with exit code $LASTEXITCODE."
    }

    $exampleOutput = Join-Path $exampleDirectory "bin\Dist"
    $exampleExecutable = Join-Path $exampleOutput "MinimalApp.exe"
    Assert-File $exampleExecutable

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $exampleExecutable
    $startInfo.Arguments = "--smoke-test"
    $startInfo.WorkingDirectory = $exampleOutput
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::Start($startInfo)
    if (-not $process.WaitForExit(120000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Native SDK smoke test timed out after 120 seconds."
    }
    if ($process.ExitCode -ne 0) {
        throw "Native SDK smoke test failed with exit code $($process.ExitCode)."
    }
}

Write-Host "Funke Native SDK: $sdkRoot"
Write-Host "Profiler ABI: $profilerEnabled"
Write-Host "Consumer setup: import FunkeSDK.props"
