newoption
{
    trigger = "core-validation",
    description = "Generate an isolated core-only Engine workspace for CI validation."
}

ROOT_DIR = os.realpath(_MAIN_SCRIPT_DIR)
IndexCoreValidationRoot = _OPTIONS["core-validation"]
    and path.join(ROOT_DIR, "premake/generated/core")
    or nil

-- Optional engine feature modules (Index.2D, Index.3D). Their source lists live in
-- Index-Engine/EngineModules.json, shared with cmake/IndexEngine.cmake.
IndexFeatureModules = json.decode(io.readfile(path.join(ROOT_DIR, "Index-Engine/EngineModules.json"))).modules
for _, featureModule in ipairs(IndexFeatureModules) do
    newoption
    {
        trigger = "without-" .. featureModule.option,
        description = "Build a runtime variant without the " .. featureModule.id .. " engine module (isolated workspace under premake/generated/variant-*)."
    }
end

local function ResolveIndexEngineVariant()
    local included = {}
    local excludedAny = false
    for _, featureModule in ipairs(IndexFeatureModules) do
        featureModule.Enabled = not _OPTIONS["without-" .. featureModule.option]
        if featureModule.Enabled then
            table.insert(included, featureModule.option)
        else
            excludedAny = true
        end
    end
    if not excludedAny then
        return nil
    end
    return #included > 0 and table.concat(included, "-") or "nomodules"
end

-- A variant only builds the engine + runtime a game ships, into its own workspace and output
-- folders, so it never overwrites the full-engine projects the editor uses.
IndexEngineVariant = ResolveIndexEngineVariant()
IndexVariantRoot = IndexEngineVariant
    and path.join(ROOT_DIR, "premake/generated/variant-" .. IndexEngineVariant)
    or nil
if IndexVariantRoot and IndexCoreValidationRoot then
    error("--without-* module options cannot be combined with --core-validation")
end
IndexIsolatedRoot = IndexCoreValidationRoot or IndexVariantRoot

-- Project files go to the isolated workspace root when one is active.
function IndexProjectLocation(projectName, defaultLocation)
    if IndexIsolatedRoot then
        return path.join(IndexIsolatedRoot, projectName)
    end
    return defaultLocation
end

function IsIndexFeatureModuleEnabled(moduleId)
    for _, featureModule in ipairs(IndexFeatureModules) do
        if featureModule.id == moduleId then
            return featureModule.Enabled
        end
    end
    error("Unknown engine feature module '" .. tostring(moduleId) .. "'")
end

include "premake/architecture.lua"

workspace "Index"
    architecture(IndexPremakeArchitecture)
    startproject ((IndexCoreValidationRoot and "Index-Engine") or (IndexVariantRoot and "Index-Runtime") or "Index-Launcher")

    if IndexIsolatedRoot then
        location(IndexIsolatedRoot)
    end

    configurations
    {
        "Debug",
        "Release",
        "Dist"
    }

    filter { "system:windows", "language:C++" }
        toolset "v143"
        defines { "SPDLOG_WCHAR_FILENAMES" }

    filter { "system:windows", "language:C++", "configurations:not Dist" }
        -- /Z7 (DebugInformationFormat=OldStyle) instead of premake's defaults:
        -- /ZI (Edit & Continue) for Debug and /Zi for Release.
        --
        -- /ZI is the most memory-hungry debug-info format and nothing in this
        -- repo uses Edit & Continue. More importantly, both /ZI and /Zi funnel
        -- every cl.exe through mspdbsrv.exe to serialise writes into a single
        -- vc143.pdb per project. That is a parallelism bottleneck and the
        -- source of `error C2471: cannot update program database` whenever
        -- several projects compiled at once -- the reason whole-solution
        -- builds had to run serially.
        --
        -- /Z7 embeds debug info directly in each .obj, so compilers never
        -- contend for a PDB. The linker still emits a complete PDB for the
        -- final binary, so breakpoints and stack traces are unchanged.
        -- Trade-off: .obj files grow, so bin-int/ gets bigger. Dist is
        -- excluded because it builds with symbols "Off" anyway.
        debugformat "c7"

    filter { "system:linux", "language:C++" }
        -- GCC rejects a member named identically to its type (e.g. the
        -- WindowSpecification / Log::Entry::Level fields in core headers) as a
        -- [-Wchanges-meaning] error, where MSVC accepts it. It is functionally
        -- harmless scope pedantry, so silence it for the whole GCC/Linux build
        -- rather than renaming widely-used API members.
        buildoptions { "-Wno-changes-meaning" }

    filter "system:linux"
        -- Every Linux artifact ultimately links into libIndex-Engine.so, so all
        -- static deps (ImGui, Box2D, glad, Index-Physics, ...) must be position-
        -- independent. Set it workspace-wide so no dependency is missed (Tracy
        -- already sets it locally; this is harmless there).
        pic "On"

    filter {}

newoption
{
    trigger = "sanitize",
    description = "Instrument Linux Debug native targets with AddressSanitizer and UndefinedBehaviorSanitizer."
}

newoption
{
    trigger = "coverage",
    description = "Instrument an isolated Linux Debug native build for gcov/lcov coverage."
}

if _OPTIONS["core-validation"] and _OPTIONS["module-profile"] ~= "core" then
    error("--core-validation requires --module-profile=core")
end
if _OPTIONS["core-validation"] and (_OPTIONS["sanitize"] or _OPTIONS["coverage"]) then
    error("--core-validation cannot be combined with instrumentation variants")
end
if _OPTIONS["sanitize"] and _OPTIONS["coverage"] then
    error("--sanitize and --coverage use separate isolated build variants")
end

local outputVariant = _OPTIONS["sanitize"] and "-sanitized"
    or (_OPTIONS["coverage"] and "-coverage")
    or (_OPTIONS["core-validation"] and "-core")
    or (IndexEngineVariant and ("-" .. IndexEngineVariant) or "")
outputdir = "%{cfg.buildcfg}-%{cfg.system}-" .. IndexArchitecture .. outputVariant
-- Module-independent outputs (ScriptCore) that a runtime variant reuses from the full build.
outputdirFull = "%{cfg.buildcfg}-%{cfg.system}-" .. IndexArchitecture

if (_OPTIONS["sanitize"] or _OPTIONS["coverage"]) and os.target() ~= "linux" then
    error("--sanitize and --coverage are supported only for Linux targets")
end

if _OPTIONS["sanitize"] then
    filter { "system:linux", "language:C++", "configurations:Debug" }
        buildoptions {
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-fno-sanitize-recover=all"
        }
        linkoptions { "-fsanitize=address,undefined", "-fno-sanitize-recover=all" }
    filter {}
end

if _OPTIONS["coverage"] then
    filter { "system:linux", "language:C++", "configurations:Debug" }
        buildoptions { "--coverage", "-fno-omit-frame-pointer" }
        linkoptions { "--coverage" }
    filter {}
end

newoption
{
    trigger = "with-imgui-demo",
    description = "Include imgui_demo.cpp in the ImGui static library project."
}

newoption
{
    trigger = "module-profile",
    value = "PROFILE",
    description = "Index module profile: full (default compatibility), core, or custom. Supplying any --with-* option without this flag selects custom.",
    allowed =
    {
        { "full", "Compatibility profile: render, audio, physics, scripting, editor, and INDEX_ALL_MODULES." },
        { "core", "Core-only profile: no optional module defines or third-party module dependencies." },
        { "custom", "Core-equivalent with no module flags, or the complete application surface with --with-editor." }
    }
}

newoption { trigger = "with-render", description = "Select render in a custom profile; currently requires --with-editor." }
newoption { trigger = "with-audio", description = "Select audio in a custom profile; currently requires --with-editor." }
newoption { trigger = "with-physics", description = "Select physics in a custom profile; currently requires --with-editor." }
newoption { trigger = "with-scripting", description = "Enable scripting; custom profiles also require --with-editor." }
newoption { trigger = "with-editor", description = "Enable the editor and implicitly enable render, audio, physics, and scripting." }

newoption
{
    trigger = "index-project",
    value = "PATH",
    description = "Absolute path to an Index project whose entityBits setting should configure the engine build. Project-local packages use premake/project-packages.lua."
}

newoption
{
    trigger = "no-profiler",
    description = "Strip the Index profiler from every configuration. By default, Tracy and the profiler panel are available in Debug/Release; Dist is always profiler-free."
}

-- The engine has exactly one graphics backend (WebGPU via Dawn); the
-- runtime D3D12/Vulkan/Metal choice underneath Dawn is selected by Dawn
-- itself based on the adapter request in WebGPUApi.cpp::RequestAdapterSync.

-- Controls whether development configurations offer profiler support; Dist is always stripped.
IndexProfiler = {}
IndexProfiler.Enabled = not _OPTIONS["no-profiler"] and not IndexCoreValidationRoot

require("premake/fix_csharp_platforms")

-- Surface DawnLayout (include / lib / define paths to the pre-built
-- webgpu_dawn.lib) BEFORE Dependencies.lua is loaded so EngineCoreRender
-- can splice the layout's lists into its own. dawn.lua is always loaded
-- — WebGPU is the only RHI, so a missing External/dawn/ checkout is a
-- real configuration error (scripts/dawn/SetupDawn.bat fixes it).
include "premake/dependencies/dawn.lua"
include "premake/dependencies/sdl3.lua"

include "Dependencies.lua"

local function HasExplicitModuleOption()
    return _OPTIONS["with-render"]
        or _OPTIONS["with-audio"]
        or _OPTIONS["with-physics"]
        or _OPTIONS["with-scripting"]
        or _OPTIONS["with-editor"]
end

local function ResolveIndexModules()
    local profile = _OPTIONS["module-profile"]
    if not profile then
        profile = HasExplicitModuleOption() and "custom" or "full"
    end

    local modules =
    {
        Profile = profile,
        FullCompatibility = profile == "full",
        Render = false,
        Audio = false,
        Physics = false,
        Scripting = false,
        Editor = false
    }

    if profile == "full" then
        modules.Render = true
        modules.Audio = true
        modules.Physics = true
        modules.Scripting = true
        modules.Editor = true
    elseif profile == "custom" then
        modules.Render = _OPTIONS["with-render"] or false
        modules.Audio = _OPTIONS["with-audio"] or false
        modules.Physics = _OPTIONS["with-physics"] or false
        modules.Scripting = _OPTIONS["with-scripting"] or false
        modules.Editor = _OPTIONS["with-editor"] or false
    end

    if modules.Editor then
        modules.Render = true
        modules.Audio = true
        modules.Physics = true
        modules.Scripting = true
    end

    if profile == "custom" and modules.Scripting and not modules.Editor then
        error("--with-scripting requires --with-editor in a custom module profile.")
    end

    if profile == "custom"
        and (modules.Render or modules.Audio or modules.Physics or modules.Scripting)
        and not modules.Editor then
        error("Partial custom module profiles are not supported; use --module-profile=core or add --with-editor (which enables the complete application surface).")
    end

    if IndexVariantRoot and profile ~= "full" then
        error("--without-* module options build a full-profile runtime variant; drop --module-profile/--with-*")
    end

    -- Feature modules sit on top of the application surface; a core build has neither.
    local hasApplicationSurface = modules.Render and modules.Physics
    modules.TwoD = hasApplicationSurface and IsIndexFeatureModuleEnabled("Index.2D")
    modules.ThreeD = hasApplicationSurface and IsIndexFeatureModuleEnabled("Index.3D")

    return modules
end

IndexModules = ResolveIndexModules()

-- The engine itself no longer depends on ImGui (the editor application owns its own
-- ImGui context as a Layer). EngineCoreEditor is therefore consumed only by the
-- editor and launcher executables, not by the engine static library.
--
-- Exception: engine.dll hosts the WebGPU-side ImGui backend
-- (Gui/ImGuiImplWebGPU.cpp, a thin wrapper around the official
-- imgui_impl_wgpu). It touches the wgpu::Device that lives in
-- WebGPUApi.cpp's TU, so it has to compile in the same module. The
-- consumer's ImGuiContext is synced into engine.dll's ImGui copy through
-- PackageImGuiBridge at every backend entry point (same pattern packages
-- already use).
Dependency.EngineSelectedModules = MergeDependencySets(
    Dependency.EngineCore,
    IndexModules.Render and Dependency.EngineCoreEditor or nil,
    IndexModules.Render and Dependency.EngineCoreRender or nil,
    IndexModules.Audio and Dependency.EngineCoreAudio or nil,
    IndexModules.Physics and IndexModules.TwoD and Dependency.EngineCorePhysics2D or nil,
    IndexModules.Physics and IndexModules.ThreeD and Dependency.EngineCorePhysics3D or nil,
    IndexModules.Scripting and Dependency.EngineCoreScripting or nil,
    Dependency.EngineCoreLinuxSystem
)

Dependency.EditorRuntimeCommon = MergeDependencySets(
    {
        -- Index-EditorRuntime is the StaticLib that holds shared editor /
        -- launcher scaffolding (custom titlebar, ImGui context bring-up).
        -- Both consumers compile and link the same source via this dep
        -- set instead of duplicating the files into each binary.
        IncludeDirs = { "%{IncludeDir.IndexEditorRuntime}" },
        DependsOn = { "Index-Engine", "Index-EditorRuntime" },
        Links = { "Index-Engine", "Index-EditorRuntime" }
    },
    Dependency.EngineSelectedModules,
    IndexModules.Editor and Dependency.EngineCoreEditor or nil
)

-- Picks the EnTT entity/version bit-split from the active project's
-- project.json. Default is 20 — matches EnTT's stock split, so
-- existing projects pay no extra memory and behave identically to the
-- unpatched engine. Cached so repeated calls from each consumer's
-- premake5.lua don't re-parse the file. The vendored EnTT header at
-- External/entt/src/entt/entity/entity.hpp branches on the resulting
-- -DINDEX_ENTITY_BITS=N to adjust entt_traits<uint32_t>::entity_mask /
-- version_mask.
--
-- Mirrors the IndexProject::Load validator in
-- Index-Engine/src/Project/IndexProject.cpp so the premake-time and
-- runtime views of the setting stay in sync.
IndexEntityBits = nil
function GetIndexEntityBits()
    if IndexEntityBits ~= nil then
        return IndexEntityBits
    end

    local default = 20
    local projectRootDir = _OPTIONS["index-project"]
    if not projectRootDir or projectRootDir == "" then
        IndexEntityBits = default
        return IndexEntityBits
    end

    local manifestPath = path.join(projectRootDir, "project.json")
    if not os.isfile(manifestPath) then
        IndexEntityBits = default
        return IndexEntityBits
    end

    local file = io.open(manifestPath, "rb")
    if not file then
        IndexEntityBits = default
        return IndexEntityBits
    end
    local jsonText = file:read("*all")
    file:close()
    if not jsonText then
        IndexEntityBits = default
        return IndexEntityBits
    end

    jsonText = jsonText:gsub("//[^\n]*", ""):gsub("/%*.-%*/", "")
    local valueText = jsonText:match('"entityBits"%s*:%s*(%-?%d+)')
    if not valueText then
        IndexEntityBits = default
        return IndexEntityBits
    end

    local bits = tonumber(valueText)
    if bits == 16 or bits == 20 or bits == 22 or bits == 24 or bits == 28 then
        IndexEntityBits = bits
    else
        print("[Index] project.json: entityBits=" .. tostring(valueText)
            .. " is not one of {16, 20, 22, 24, 28}; falling back to 20.")
        IndexEntityBits = default
    end
    return IndexEntityBits
end

-- Writes the generated header that the EnTT patch reads to override
-- INDEX_ENTITY_BITS at compile time. The build-time -D in
-- GetIndexModuleDefines() (and in package-loader.lua's RegisterNative-
-- Project) is the fallback; the header is the override path that the
-- editor's Rebuild Engine flow uses to change the value without
-- regenerating project files.
--
-- Layout:
--   Index-Engine/src/Generated/IndexEntityBitsConfig.h
--
-- Every C++ project that compiles entt-using code has this folder on its
-- include path (the four main projects + RegisterNativeProject). On
-- premake regen we always rewrite the header so its value tracks
-- project.json. If the file ever drifts from the -D, the header
-- wins (it #undef/#defines INDEX_ENTITY_BITS).
IndexEntityBitsConfigHeader = path.join(ROOT_DIR,
    "Index-Engine/src/Generated/IndexEntityBitsConfig.h")
IndexEntityBitsConfigIncludeDir = path.join(ROOT_DIR,
    "Index-Engine/src/Generated")

function WriteIndexEntityBitsConfigHeader()
    local bits = GetIndexEntityBits()
    os.mkdir(IndexEntityBitsConfigIncludeDir)
    local newContent =
        "// Auto-generated by premake (and rewritten by the editor's\n" ..
        "// Rebuild Engine flow). Do not edit by hand. Overrides the\n" ..
        "// build-time -DINDEX_ENTITY_BITS so the editor can change the\n" ..
        "// EnTT entity/version bit split without a full premake regen.\n" ..
        "// See premake5.lua's WriteIndexEntityBitsConfigHeader() and\n" ..
        "// Index-Editor/src/Systems/ImGuiEditorLayerPanels.cpp's\n" ..
        "// Rebuild Engine handler for the two writers.\n" ..
        "#pragma once\n" ..
        "#undef  INDEX_ENTITY_BITS\n" ..
        "#define INDEX_ENTITY_BITS " .. tostring(bits) .. "\n"
    -- Skip the write when the on-disk content already matches. Touching the
    -- mtime here invalidates every TU that transitively includes entt (the
    -- header sits on every entt-using project's include path), which is what
    -- forces Visual Studio to recompile the world on every launcher-driven
    -- premake regen.
    local existing = io.open(IndexEntityBitsConfigHeader, "rb")
    if existing then
        local current = existing:read("*all")
        existing:close()
        if current == newContent then return end
    end
    local file, err = io.open(IndexEntityBitsConfigHeader, "wb")
    if not file then
        error("[Index] cannot write " .. IndexEntityBitsConfigHeader
            .. " (" .. tostring(err) .. ")")
    end
    file:write(newContent)
    file:close()
end

-- Run once at premake-time so the header exists before any project block
-- is processed. The editor's Rebuild Engine path rewrites this same file
-- from C++ when only the bit count changes (avoiding the premake regen).
WriteIndexEntityBitsConfigHeader()

-- Reproduce the engine-owned changes in pinned upstream submodules.
-- Hash and path verification prevents dependency bumps from accepting stale patches.
require("premake/vendor-patches")
ApplyIndexVendorPatches()

function GetIndexModuleDefines()
    local hasApplication = IndexModules.Render
        and IndexModules.Audio
        and IndexModules.Physics
        and IndexModules.Scripting
        and IndexModules.Editor

    local defines =
    {
        "INDEX_WITH_RENDER=" .. (IndexModules.Render and "1" or "0"),
        "INDEX_WITH_AUDIO=" .. (IndexModules.Audio and "1" or "0"),
        "INDEX_WITH_PHYSICS=" .. (IndexModules.Physics and "1" or "0"),
        "INDEX_WITH_SCRIPTING=" .. (IndexModules.Scripting and "1" or "0"),
        -- INDEX_WITH_EDITOR is intentionally NOT emitted here: it varies per
        -- build configuration (1 in Debug/Release so the workspace can host the
        -- editor, 0 in Dist so shipped runtimes carry no editor systems). It is
        -- applied by ApplyIndexEditorModuleDefine() below, which every engine
        -- consumer calls right after defines(GetIndexModuleDefines()).
        "INDEX_WITH_APPLICATION=" .. (hasApplication and "1" or "0"),
        "INDEX_WITH_2D=" .. (IndexModules.TwoD and "1" or "0"),
        "INDEX_WITH_3D=" .. (IndexModules.ThreeD and "1" or "0"),
        "INDEX_ENTITY_BITS=" .. tostring(GetIndexEntityBits()),
        -- Shrink magic_enum's default reflection window [-128, 127] -> [-1, 64].
        -- All engine-side reflected enums are small `enum class : uint8_t`
        -- with sequential values in [0, ~16] (see Index-Engine/src/Components/
        -- and Index-Engine/src/Graphics/Wrap.hpp for the only customized
        -- range). The default window instantiates 256 template specializations
        -- per reflected enum; ComponentInspectors.cpp reflects so many enums
        -- via Properties::MakeWith<T>() that even 64-bit cl.exe runs out of
        -- heap (C1060 in magic_enum.hpp). Shrinking the window cuts template
        -- bloat ~4x. If a new enum exceeds [-1, 64], specialize
        -- magic_enum::customize::enum_range<T> in its header (see
        -- Index-Engine/src/Graphics/Wrap.hpp for the existing pattern).
        "MAGIC_ENUM_RANGE_MIN=-1",
        "MAGIC_ENUM_RANGE_MAX=32"
    }

    if IndexModules.FullCompatibility then
        table.insert(defines, "INDEX_ALL_MODULES=1")
    end

    return defines
end

-- Emits INDEX_WITH_EDITOR per build configuration. MUST be called inside a
-- project block, right after defines(GetIndexModuleDefines()).
--
-- Editor-support code in the engine (the PrefabTemplateCache .prefab watcher,
-- ProjectManager's cache hook, ...) is gated on `#if INDEX_WITH_EDITOR`. It
-- compiles into Debug/Release so the dev workspace can host the editor, but is
-- stripped from Dist so a shipped runtime carries no editor-only systems.
-- When the workspace was generated without the editor module at all, it stays
-- off in every configuration.
--
-- Splitting it out per-config (rather than baking one value at premake time)
-- is safe for cross-DLL ODR: every binary in a single process is built in the
-- same configuration, so engine.dll and its consumers always agree on the
-- value (PrefabTemplateCache has an INDEX_WITH_EDITOR-conditional member).
function ApplyIndexEditorModuleDefine()
    if IndexModules.Editor then
        filter "configurations:Dist"
            defines { "INDEX_WITH_EDITOR=0" }
        filter "configurations:not Dist"
            defines { "INDEX_WITH_EDITOR=1" }
        filter {}
    else
        defines { "INDEX_WITH_EDITOR=0" }
    end
end

function UseIndexEngineModuleDependencies()
    UseDependencySet(Dependency.EngineSelectedModules)
    UseIndexProfilerDependencies(true)
end

-- Shared postbuild command: copy IndexAssets into each target output
-- directory. The single source tree is `Index-Runtime/IndexAssets/`,
-- staged via one {COPYDIR} next to the consumer exe.
CopyIndexAssets = '{COPYDIR} "' .. path.join(ROOT_DIR, "Index-Runtime/IndexAssets") .. '" "%{cfg.targetdir}/IndexAssets"'

local function SharedLibFileName(projectName)
    if os.target() == "windows" then
        return projectName .. ".dll"
    elseif os.target() == "macosx" then
        return "lib" .. projectName .. ".dylib"
    end

    return "lib" .. projectName .. ".so"
end

-- Copies a shared library next to a consumer. On Linux / macOS a changed library is replaced by a new file
-- (rm + cp) instead of being overwritten in place: a running editor may have the old one mapped, and
-- truncating a mapped library crashes it (the editor rebuilds the runtime with make while it runs).
function CopySharedLibFile(source, destination)
    if os.target() == "windows" then
        return '{COPYFILE} "' .. source .. '" "' .. destination .. '"'
    end
    return 'cmp -s "' .. source .. '" "' .. destination .. '" || { rm -f "' .. destination ..
        '" && cp "' .. source .. '" "' .. destination .. '"; }'
end

local function CopySharedLib(projectName)
    local fileName = SharedLibFileName(projectName)
    return CopySharedLibFile(path.join(ROOT_DIR, "bin/" .. outputdir, projectName, fileName),
        "%{cfg.targetdir}/" .. fileName)
end

local function RemoveSharedLib(projectName)
    local target = "%{cfg.targetdir}/" .. SharedLibFileName(projectName)
    if os.target() == "windows" then
        return 'if exist "' .. target .. '" del /F /Q "' .. target .. '"'
    end
    return 'rm -f "' .. target .. '"'
end

-- Shared postbuild command: copy the engine SharedLib next to each consumer executable
-- so it resolves at runtime without depending on PATH.
local function DawnSharedLibFileName()
    if os.target() == "macosx" then
        return "libwebgpu_dawn.dylib"
    end

    return "libwebgpu_dawn.so"
end

CopyIndexEngineDll = CopySharedLib("Index-Engine")
CopyDawnSharedLib = ""
if IndexModules.Render and os.target() ~= "windows" then
    local dawnSharedLib = DawnSharedLibFileName()
    CopyDawnSharedLib = CopySharedLibFile(path.join(ROOT_DIR, "External/dawn/build/src/dawn/native", dawnSharedLib),
        "%{cfg.targetdir}/" .. dawnSharedLib)
end

-- The Index-GameComponents sidecar DLL is removed — user components now
-- register at runtime via DynamicComponentRegistrar in Index-ScriptCore.
-- No per-project codegen artifact gets shipped alongside consumer exes.
CopyIndexGameComponentsDll = "" -- retained as a no-op so any leftover postbuild reference stays harmless

-- Consumers load private ScriptCore copies so its shared build output remains replaceable while they run.
local function CopyScriptCoreArtifact(filename)
    return '{COPYFILE} "' ..
        path.join(ROOT_DIR, "bin/" .. (IndexVariantRoot and outputdirFull or outputdir), "Index-ScriptCore", filename) ..
        '" "%{cfg.targetdir}/' .. filename .. '"'
end

CopyScriptCoreArtifacts = {
    CopyScriptCoreArtifact("Index-ScriptCore.dll"),
    CopyScriptCoreArtifact("Index-ScriptCore.runtimeconfig.json"),
    CopyScriptCoreArtifact("Index-ScriptCore.deps.json"),
}

-- Editor-only in-process hot-reload compiler (Index-ScriptCompiler, a dotnet-built project outside the
-- solution). Optional: without it the editor falls back to dotnet build.
if os.target() == "windows" then
    local scriptCompilerDir = path.translate(path.join(ROOT_DIR, "bin/" .. outputdir, "Index-ScriptCompiler"), "\\")
    CopyScriptCompilerArtifacts = { 'if exist "' .. scriptCompilerDir .. '\\Index-ScriptCompiler.dll" xcopy /Y /Q /I "' ..
        scriptCompilerDir .. '\\Index-ScriptCompiler.*" "%{cfg.targetdir}\\ScriptCompiler\\" >nul' }
else
    -- Index-Managed (below) always builds it on Linux / macOS.
    local scriptCompilerDir = path.join(ROOT_DIR, "bin/" .. outputdir, "Index-ScriptCompiler")
    CopyScriptCompilerArtifacts = { '{MKDIR} "%{cfg.targetdir}/ScriptCompiler"' }
    for _, file in ipairs({ "Index-ScriptCompiler.dll", "Index-ScriptCompiler.deps.json", "Index-ScriptCompiler.runtimeconfig.json" }) do
        table.insert(CopyScriptCompilerArtifacts,
            '{COPYFILE} "' .. path.join(scriptCompilerDir, file) .. '" "%{cfg.targetdir}/ScriptCompiler/' .. file .. '"')
    end
end

-- Materialize a self-contained EngineSDK/ folder next to the Launcher exe
-- so the zipped Launcher can build new projects' NativeScripts (which
-- generate a CMakeLists.txt that hard-references ${INDEX_DIR}/Index-Engine/
-- src and ${INDEX_DIR}/External/... — see BuildNativeScriptCMakeLists in
-- Index-Engine/src/Project/IndexProject.cpp). ResolveEngineRoot() picks up
-- the bundled SDK ahead of the dev-layout fallbacks. The script is a thin
-- batch wrapper around robocopy so the lua stays free of per-folder copy
-- spam; see scripts/CopyEngineSdk.bat for the actual copy list.
if os.target() == "windows" then
    CopyEngineSdk = 'call "' .. path.join(ROOT_DIR, "scripts/CopyEngineSdk.bat") ..
        '" "%{cfg.targetdir}/EngineSDK" "' .. ROOT_DIR ..
        '" "' .. path.join(ROOT_DIR, "bin/" .. outputdir) .. '"'
else
    CopyEngineSdk = 'python3 "' .. path.join(ROOT_DIR, "scripts/CopyEngineSdk.py") ..
        '" "%{cfg.targetdir}/EngineSDK" "' .. ROOT_DIR ..
        '" "' .. path.join(ROOT_DIR, "bin/" .. outputdir) .. '"'
end

-- Managed projects consumers depend on: the solution's C# projects on Windows, the Index-Managed
-- utility (scripts/BuildManaged.py) on Linux / macOS where gmake2 cannot build SDK-style projects.
function IndexManagedDependencies(windowsProjects)
    if os.target() == "windows" then
        return windowsProjects
    end
    return { "Index-Managed" }
end

-- SDL3 is a shared library so engine.dll and the consumer executables share one copy
-- of its global state. Each consumer ships it alongside its binary.
CopySDL3SharedLib = CopySharedLibFile(path.join(ROOT_DIR, SDL3Layout.SharedLibPath),
    "%{cfg.targetdir}/" .. SDL3Layout.SharedLibFileName)

-- Glad has been removed from the engine build (the render backend
-- handles its own GPU context). CopyGladDll stays defined as a no-op so
-- consumer postbuild lists referencing it don't have to be edited in
-- lockstep.
CopyGladDll = ""

-- Tracy is process-wide and ships only beside profiler-enabled development binaries.
if IndexProfiler and IndexProfiler.Enabled then
    CopyTracyDll = CopySharedLib("Tracy")
else
    CopyTracyDll = ""
end
RemoveTracyDll = RemoveSharedLib("Tracy")

function ApplyIndexProfilerRuntime()
    if IndexProfiler and IndexProfiler.Enabled then
        filter "configurations:not Dist"
            postbuildcommands { CopyTracyDll }
        filter "configurations:Dist"
            postbuildcommands { RemoveTracyDll }
    else
        postbuildcommands { RemoveTracyDll }
    end
    filter {}
end

local function NormalizeRootPath(pathValue)
    if path.isabsolute(pathValue) then
        return pathValue
    end

    return path.join(ROOT_DIR, pathValue)
end

local function NormalizeRootPaths(paths)
    local normalized = {}

    for _, pathValue in ipairs(paths) do
        table.insert(normalized, NormalizeRootPath(pathValue))
    end

    return normalized
end

-- Apply per-config libdirs pointing at Dawn's Debug vs Release output
-- folders. Call once inside a project block; the filter context resets
-- afterwards so subsequent project config isn't accidentally scoped.
--
-- `rootRelPrefix` is the path prefix to prepend so the (repo-root-
-- relative) Dawn build directory resolves correctly from the consumer's
-- own premake working directory. Top-level project folders (Index-Engine/,
-- Index-Editor/, etc.) need "../"; deeper folders (Tests/Index-Engine-
-- Tests/) need "../../" — caller picks.
--
-- Why per-config: webgpu_dawn.lib lives in both `Debug/` and `Release/`
-- subdirectories with /MDd vs /MD runtime selection respectively. Linking
-- the wrong-config lib into an /MDd Debug engine produces LNK2038
-- "RuntimeLibrary" + "_ITERATOR_DEBUG_LEVEL" mismatch errors.
function ApplyDawnLibDirs(rootRelPrefix)
    local p = rootRelPrefix or "../"
    filter "configurations:Debug"
        libdirs { p .. "External/dawn/build/src/dawn/native/Debug" }
    filter "configurations:Release"
        libdirs { p .. "External/dawn/build/src/dawn/native/Release" }
    filter "configurations:Dist"
        libdirs { p .. "External/dawn/build/src/dawn/native/Release" }
    filter {}
    -- Single-config generators (Make / Ninja — i.e. the Linux + macOS builds,
    -- and CI) emit the lib straight into .../native/ with no per-config
    -- subfolder, so the per-config dirs above never match there. Add the bare
    -- path for every config as a fallback. On Windows (multi-config VS) the
    -- bare .../native/ holds no webgpu_dawn lib — the libs live in the Debug/
    -- and Release/ subfolders above — so this is a harmless search-path miss
    -- with no LNK2038 risk. On Linux a Debug engine linking the Release .a is
    -- fine: libstdc++ has no _ITERATOR_DEBUG_LEVEL ABI split, unlike MSVC's
    -- /MDd vs /MD. This is what lets CI build Release Dawn once and link it
    -- into both the Release and Debug engine builds.
    libdirs { p .. "External/dawn/build/src/dawn/native" }
end

-- True when an engine-shipped package with this name is installed on disk —
-- its manifest is present at <repo>/packages/<Name>/index-package.lua. The
-- package loader (premake/package-loader.lua) registers exactly those, so this
-- is a faithful "will the Pkg.<Name> project exist in the workspace?" predicate.
-- It checks the filesystem rather than loader state, so a consumer's premake5.lua
-- can call it even though packages are loaded last. Lets a sample opt into a
-- package link only when the package is actually present, so a checkout without
-- it (CI, a fresh clone, a user who hasn't fetched it from the registry) builds
-- clean instead of emitting MSB3245 for an unresolvable assembly reference.
function IndexEnginePackageInstalled(name)
    return os.isfile(path.join(ROOT_DIR, "packages", name, "index-package.lua"))
end

function UseDependencySet(dep)
    if dep.IncludeDirs then
        includedirs(NormalizeRootPaths(dep.IncludeDirs))
    end

    if dep.LibDirs then
        libdirs(NormalizeRootPaths(dep.LibDirs))
    end

    if dep.DependsOn then
        dependson(dep.DependsOn)
    end

    if dep.Links then
        links(dep.Links)
    end

    if dep.Defines then
        defines(dep.Defines)
    end
end

-- Variant for intermediate StaticLib projects whose consumer .exe handles
-- the linking. Skips LibDirs and Links because MSVC's lib.exe MERGES input
-- .lib files into the output archive (it doesn't just record references).
-- Routing Links onto a StaticLib's <Lib><AdditionalDependencies> bloats the
-- resulting .lib by the combined size of every transitively-listed static
-- lib — e.g. webgpu_dawn.lib is ~185 MB Release / ~1.36 GB Debug, and ends
-- up archived wholesale inside the consuming StaticLib if links() is called.
-- Headers, dependson() (build-order), and defines() are still applied so the
-- TUs in this lib see the same include paths / preprocessor surface as the
-- consumer .exes that link the actual dependencies.
function UseDependencySetNoLink(dep)
    if dep.IncludeDirs then
        includedirs(NormalizeRootPaths(dep.IncludeDirs))
    end

    if dep.DependsOn then
        dependson(dep.DependsOn)
    end

    if dep.Defines then
        defines(dep.Defines)
    end
end

function UseIndexProfilerDependencies(linkTracy)
    if not (IndexProfiler and IndexProfiler.Enabled) then
        return
    end

    filter "configurations:not Dist"
        if linkTracy then
            UseDependencySet(Dependency.Profiler)
        else
            UseDependencySetNoLink(Dependency.Profiler)
        end
    filter {}
end

group "Dependencies"
if IndexModules.Render then
    project "ImGui"
        location (IndexProjectLocation("ImGui", path.join(ROOT_DIR, "premake/generated/ImGui")))
        kind "StaticLib"
        language "C++"
        cppdialect "C++20"
        staticruntime "off"

    targetdir (path.join(ROOT_DIR, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(ROOT_DIR, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files
        {
            -- Core
            "External/imgui/imconfig.h",
            "External/imgui/imgui.h",
            "External/imgui/imgui_internal.h",
            "External/imgui/imstb_rectpack.h",
            "External/imgui/imstb_textedit.h",
            "External/imgui/imstb_truetype.h",
            "External/imgui/imgui.cpp",
            "External/imgui/imgui_draw.cpp",
            "External/imgui/imgui_tables.cpp",
            "External/imgui/imgui_widgets.cpp",

            -- Backends:
            --   * imgui_impl_sdl3 — used by editor / launcher / runtime
            --     executables for input + window-event plumbing.
            --   * imgui_impl_wgpu — used by engine.dll via
            --     Gui/ImGuiImplWebGPU.cpp for in-engine ImGui draws on the
            --     shared wgpu::Device. Headers are reachable through
            --     Dependency.ImGui below (DawnLayout spliced in).
            "External/imgui/backends/imgui_impl_sdl3.h",
            "External/imgui/backends/imgui_impl_sdl3.cpp",
            "External/imgui/backends/imgui_impl_wgpu.h",
            "External/imgui/backends/imgui_impl_wgpu.cpp"
        }

        if _OPTIONS["with-imgui-demo"] then
            files { "External/imgui/imgui_demo.cpp" }
        end

        vpaths
        {
            ["Core/*"] =
            {
                "External/imgui/imconfig.h",
                "External/imgui/imgui.h",
                "External/imgui/imgui_internal.h",
                "External/imgui/imstb_rectpack.h",
                "External/imgui/imstb_textedit.h",
                "External/imgui/imstb_truetype.h",
                "External/imgui/imgui.cpp",
                "External/imgui/imgui_draw.cpp",
                "External/imgui/imgui_tables.cpp",
                "External/imgui/imgui_widgets.cpp"
            },
            ["Backends/*"] =
            {
                "External/imgui/backends/imgui_impl_sdl3.h",
                "External/imgui/backends/imgui_impl_sdl3.cpp",
                "External/imgui/backends/imgui_impl_wgpu.h",
                "External/imgui/backends/imgui_impl_wgpu.cpp"
            },
            ["Optional/*"] = { "External/imgui/imgui_demo.cpp" }
        }

        UseDependencySet(Dependency.ImGui)

        filter "system:windows"
            -- See Index-Engine/premake5.lua for the rationale on
            -- MultiProcessorCompile + /Zc:preprocessor.
            flags { "MultiProcessorCompile" }
            buildoptions { "/FS", "/Zc:preprocessor" }
            systemversion "latest"

        filter "configurations:Debug"
            runtime "Debug"
            symbols "On"
            defines { "_DEBUG" }

        filter "configurations:Release"
            runtime "Release"
            optimize "On"
            symbols "On"
            defines { "NDEBUG" }

        filter "configurations:Dist"
            runtime "Release"
            optimize "Full"
            symbols "Off"
            defines { "NDEBUG" }

        filter {}
end

-- ImGuizmo (translate/rotate/scale gizmos) — editor-only. Built as its own
-- StaticLib so it compiles WITHOUT the engine PCH/forceinclude the editor uses;
-- it only needs the imgui headers and resolves ImGui symbols at editor link time.
if IndexModules.Editor and not IndexVariantRoot then
    project "ImGuizmo"
        location (path.join(ROOT_DIR, "premake/generated/ImGuizmo"))
        kind "StaticLib"
        language "C++"
        cppdialect "C++20"
        staticruntime "off"

        targetdir (path.join(ROOT_DIR, "bin/" .. outputdir .. "/%{prj.name}"))
        objdir (path.join(ROOT_DIR, "bin-int/" .. outputdir .. "/%{prj.name}"))

        files
        {
            "External/ImGuizmo/src/ImGuizmo.h",
            "External/ImGuizmo/src/ImGuizmo.cpp"
        }

        includedirs
        {
            "External/imgui",
            "External/ImGuizmo/src"
        }

        filter "system:windows"
            flags { "MultiProcessorCompile" }
            buildoptions { "/FS", "/Zc:preprocessor" }
            systemversion "latest"

        filter "configurations:Debug"
            runtime "Debug"
            symbols "On"
            defines { "_DEBUG" }

        filter "configurations:Release"
            runtime "Release"
            optimize "On"
            symbols "On"
            defines { "NDEBUG" }

        filter "configurations:Dist"
            runtime "Release"
            optimize "Full"
            symbols "Off"
            defines { "NDEBUG" }

        filter {}
end

-- Dawn (WebGPU) and SDL3 are built out-of-band by scripts/dawn/SetupDawn.* and
-- scripts/sdl/SetupSDL.py; premake only links them (see dawn.lua / sdl3.lua).

if IndexModules.Physics and IndexModules.TwoD then
    include "premake/dependencies/box2d.lua"
    include "premake/dependencies/index_physics.lua"
end
if IndexModules.Physics and IndexModules.ThreeD then
    include "premake/dependencies/jolt.lua"
end

if IndexProfiler.Enabled then
    include "premake/dependencies/tracy.lua"
end

include "Index-Engine"

-- The solution builds the managed projects on Windows. gmake2 cannot build SDK-style .NET 10 projects,
-- so Linux / macOS run them through dotnet in one utility project the native consumers depend on.
-- A runtime variant reuses the full build's ScriptCore: the C# surface is module-independent.
if IndexVariantRoot then
    -- nothing managed
elseif os.target() == "windows" and not IndexCoreValidationRoot then
    include "Index-PackageTool"
    if IndexModules.Scripting then
        include "Index-ScriptCore"
        include "Index-Sandbox"
    end
elseif not IndexCoreValidationRoot and IndexModules.Scripting then
    group "Core"
    project "Index-Managed"
        location "premake/generated/Index-Managed"
        -- "Makefile", not "Utility": gmake2 never runs a Utility project's commands.
        kind "Makefile"
        targetdir ("bin/" .. outputdir .. "/%{prj.name}")
        objdir ("bin-int/" .. outputdir .. "/%{prj.name}")
        buildcommands {
            'python3 "' .. path.join(ROOT_DIR, "scripts/BuildManaged.py") .. '" --configuration %{cfg.buildcfg}'
                .. ' --output-dir "' .. path.join(ROOT_DIR, "bin/" .. outputdir) .. '"'
                .. ' --intermediate-dir "' .. path.join(ROOT_DIR, "bin-int/" .. outputdir) .. '"'
        }
    group ""
end

-- Shared StaticLib between the editor and launcher; must be defined
-- before either consumer is `include`d so each project's DependencySet
-- resolution can see it.
if IndexModules.Editor or IndexModules.FullCompatibility then
    include "Index-EditorRuntime"
end

if IndexModules.Editor and not IndexVariantRoot then
    include "Index-Editor"
    -- `index` command-line tool; compiles the editor's UI-free build sources directly.
    include "Index-CLI"
end

if IndexModules.FullCompatibility then
    if not IndexVariantRoot then
        include "Index-Launcher"
    end
    include "Index-Runtime"
end

group ""

-- Tests live in their own group so they're easy to spot in the IDE solution
-- explorer and trivial to disable by commenting out this block.
if not IndexIsolatedRoot then
    group "Tests"
        include "Tests/Index-Engine-Tests"
        include "Tests/Index-Editor-Tests"
    group ""
end

-- Load any index-package.lua manifests under packages/ and register their projects.
if not IndexIsolatedRoot then
    local IndexPackageLoader = dofile(path.join(ROOT_DIR, "premake/package-loader.lua"))
    IndexPackageLoader.LoadAll()
end
