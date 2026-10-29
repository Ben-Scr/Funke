newoption {
    trigger = "funke-project",
    value = "PATH",
    description = "Absolute path to the Funke project whose package workspace should be generated."
}

local requestedProjectRoot = _OPTIONS["funke-project"]
if not requestedProjectRoot or requestedProjectRoot == "" then
    error("[Funke Packages] --funke-project=<absolute-path> is required.", 0)
end

local scriptDir = path.getdirectory(os.realpath(_MAIN_SCRIPT))
local engineRoot = os.realpath(path.join(scriptDir, ".."))
local projectRoot = os.realpath(requestedProjectRoot)
local workspaceRoot = path.join(projectRoot, ".funke/build/packages")

dofile(path.join(scriptDir, "compat.lua"))
dofile(path.join(scriptDir, "fix_csharp_platforms.lua"))
dofile(path.join(scriptDir, "architecture.lua"))

workspace "FunkeProjectPackages"
    location(workspaceRoot)
    FunkeApplyArchitecture()
    configurations { "Debug", "Release", "Dist" }

    filter { "system:windows", "language:C++" }
        toolset "v143"
        defines { "SPDLOG_WCHAR_FILENAMES" }
    filter { "system:linux", "language:C++" }
        pic "On"
        buildoptions { "-Wno-changes-meaning" }
    filter {}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-" .. FunkeArchitecture
ROOT_DIR = engineRoot

FunkePackageProjectLocationRoot = path.join(workspaceRoot, "generated")
FunkePackageTargetRoot = path.join(workspaceRoot, "bin")
FunkePackageObjectRoot = path.join(workspaceRoot, "bin-int")
FunkeEntityBitsConfigIncludeDir = path.join(engineRoot, "Funke-Engine/src/Generated")

local function ReadEntityBits()
    local projectFile = io.open(path.join(projectRoot, "project.json"), "rb")
    if not projectFile then return 20 end
    local jsonText = projectFile:read("*all")
    projectFile:close()
    if not jsonText then return 20 end

    jsonText = jsonText:gsub("//[^\n]*", ""):gsub("/%*.-%*/", "")
    local bits = tonumber(jsonText:match('"entityBits"%s*:%s*(%-?%d+)'))
    if bits == 16 or bits == 20 or bits == 22 or bits == 24 or bits == 28 then
        return bits
    end
    return 20
end

local entityBits = ReadEntityBits()
function GetFunkeEntityBits()
    return entityBits
end

local includeDirs = {
    path.join(engineRoot, "Funke-Engine/src"),
    path.join(engineRoot, "Funke-Engine/src/Generated"),
    path.join(engineRoot, "External/spdlog/include"),
    path.join(engineRoot, "External/glm"),
    path.join(engineRoot, "External/entt/src"),
    path.join(engineRoot, "External/stb"),
    path.join(engineRoot, "External/magic_enum/include"),
    path.join(engineRoot, "External/rapidxml"),
    path.join(engineRoot, "External/SDL/include"),
    path.join(engineRoot, "External/miniaudio"),
    path.join(engineRoot, "External/box2d/include"),
    path.join(engineRoot, "External/dotnet"),
    path.join(engineRoot, "External/dawn/include"),
    path.join(engineRoot, "External/dawn/include/dawn"),
    path.join(engineRoot, "External/dawn/build/gen/include"),
}

-- Each configuration links its own engine build first; the other builds under bin/ are fallbacks
-- (an SDK may ship only one configuration). Per configuration because premake drops duplicate
-- link options, which used to put bin/Debug-* ahead of the matching build and link a stale engine.
local engineConfigurations = { "Debug", "Release", "Dist" }
local foundEngineLibDirs = os.matchdirs(path.join(engineRoot, "bin/*/Funke-Engine"))

local function EngineLibDirsFor(configuration)
    local own = path.join(engineRoot, "bin/" .. configuration .. "-" .. os.target() .. "-" .. FunkeArchitecture .. "/Funke-Engine")
    local directories = { own }
    for _, directory in ipairs(foundEngineLibDirs) do
        if path.normalize(directory):lower() ~= path.normalize(own):lower() then
            table.insert(directories, directory)
        end
    end
    return directories
end

local engineIncludeDirsForPremake = includeDirs
local engineBuildOptions = nil
local engineLibDirsByConfiguration = {}
local engineLinkOptionsByConfiguration = {}
for _, configuration in ipairs(engineConfigurations) do
    local directories = EngineLibDirsFor(configuration)
    if os.target() == "windows" then
        local options = {}
        for _, directory in ipairs(directories) do
            table.insert(options, '/LIBPATH:"' .. path.translate(directory, "\\") .. '"')
        end
        engineLinkOptionsByConfiguration[configuration] = options
    else
        engineLibDirsByConfiguration[configuration] = directories
    end
end
if os.target() == "windows" then
    engineIncludeDirsForPremake = nil
    engineBuildOptions = {}
    for _, directory in ipairs(includeDirs) do
        table.insert(engineBuildOptions,
            '/I"' .. path.translate(directory, "\\") .. '"')
    end
end

FunkePackageUseDawnLibDirs = false

Dependency = {
    EditorRuntimeCommon = {
        IncludeDirs = engineIncludeDirsForPremake,
        BuildOptions = engineBuildOptions,
        LibDirsByConfiguration = engineLibDirsByConfiguration,
        LinkOptionsByConfiguration = engineLinkOptionsByConfiguration,
        Links = { "Funke-Engine" },
        Defines = {
            "FUNKE_ALL_MODULES=1",
            "FNK_RHI_WEBGPU",
            "WEBGPU_CPP_HAS_ENUM_CLASS_BITMASKS=1",
            "IMGUI_IMPL_WEBGPU_BACKEND_DAWN",
        },
    }
}

function UseDependencySet(dependency)
    if dependency.IncludeDirs then includedirs(dependency.IncludeDirs) end
    if dependency.BuildOptions then buildoptions(dependency.BuildOptions) end
    if dependency.LibDirs then libdirs(dependency.LibDirs) end
    if dependency.DependsOn then dependson(dependency.DependsOn) end
    if dependency.LinkOptions then linkoptions(dependency.LinkOptions) end
    if dependency.Links then links(dependency.Links) end
    if dependency.Defines then defines(dependency.Defines) end
    for _, configuration in ipairs(engineConfigurations) do
        local configLibDirs = dependency.LibDirsByConfiguration and dependency.LibDirsByConfiguration[configuration]
        local configLinkOptions = dependency.LinkOptionsByConfiguration and dependency.LinkOptionsByConfiguration[configuration]
        if configLibDirs or configLinkOptions then
            filter { "configurations:" .. configuration }
                if configLibDirs then libdirs(configLibDirs) end
                if configLinkOptions then linkoptions(configLinkOptions) end
        end
    end
    filter {}
end

local function FirstExistingPath(candidates)
    for _, candidate in ipairs(candidates) do
        if os.isfile(candidate) then return candidate end
    end
    return candidates[1]
end

FunkePackageScriptCoreAssemblies = {}
for _, configuration in ipairs({ "Debug", "Release", "Dist" }) do
    FunkePackageScriptCoreAssemblies[configuration] = FirstExistingPath({
        path.join(engineRoot, "bin/" .. configuration .. "-" .. os.target() .. "-" .. FunkeArchitecture .. "/Funke-ScriptCore/Funke-ScriptCore.dll"),
        path.join(engineRoot, "Funke-ScriptCore/Funke-ScriptCore.dll"),
    })
end

local packageLoader = dofile(path.join(engineRoot, "premake/package-loader.lua"))
local packageCount = packageLoader.LoadProjectPackages(projectRoot)
if packageCount == 0 then
    group "Packages"
    project "FunkeProjectPackages.Empty"
        location(path.join(workspaceRoot, "generated/FunkeProjectPackages.Empty"))
        kind "Utility"
    group ""
end
