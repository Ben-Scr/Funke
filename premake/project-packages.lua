newoption {
    trigger = "index-project",
    value = "PATH",
    description = "Absolute path to the Index project whose package workspace should be generated."
}

local requestedProjectRoot = _OPTIONS["index-project"]
if not requestedProjectRoot or requestedProjectRoot == "" then
    error("[Index Packages] --index-project=<absolute-path> is required.", 0)
end

local scriptDir = path.getdirectory(os.realpath(_MAIN_SCRIPT))
local engineRoot = os.realpath(path.join(scriptDir, ".."))
local projectRoot = os.realpath(requestedProjectRoot)
local workspaceRoot = path.join(projectRoot, ".index/build/packages")

dofile(path.join(scriptDir, "fix_csharp_platforms.lua"))

workspace "IndexProjectPackages"
    location(workspaceRoot)
    architecture "x64"
    configurations { "Debug", "Release", "Dist" }

    filter { "system:windows", "language:C++" }
        toolset "v143"
        defines { "SPDLOG_WCHAR_FILENAMES" }
    filter { "system:linux", "language:C++" }
        pic "On"
        buildoptions { "-Wno-changes-meaning" }
    filter {}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
ROOT_DIR = engineRoot

IndexPackageProjectLocationRoot = path.join(workspaceRoot, "generated")
IndexPackageTargetRoot = path.join(workspaceRoot, "bin")
IndexPackageObjectRoot = path.join(workspaceRoot, "bin-int")
IndexEntityBitsConfigIncludeDir = path.join(engineRoot, "Index-Engine/src/Generated")

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
function GetIndexEntityBits()
    return entityBits
end

local includeDirs = {
    path.join(engineRoot, "Index-Engine/src"),
    path.join(engineRoot, "Index-Engine/src/Generated"),
    path.join(engineRoot, "External/spdlog/include"),
    path.join(engineRoot, "External/glm"),
    path.join(engineRoot, "External/entt/src"),
    path.join(engineRoot, "External/stb"),
    path.join(engineRoot, "External/magic_enum/include"),
    path.join(engineRoot, "External/rapidxml"),
    path.join(engineRoot, "External/glfw/include"),
    path.join(engineRoot, "External/glad/include"),
    path.join(engineRoot, "External/miniaudio"),
    path.join(engineRoot, "External/box2d/include"),
    path.join(engineRoot, "External/Index-Physics/include"),
    path.join(engineRoot, "External/dotnet"),
    path.join(engineRoot, "External/dawn/include"),
    path.join(engineRoot, "External/dawn/include/dawn"),
    path.join(engineRoot, "External/dawn/build/gen/include"),
}

local engineLibDirs = { path.join(engineRoot, "bin/" .. outputdir .. "/Index-Engine") }
for _, directory in ipairs(os.matchdirs(path.join(engineRoot, "bin/*/Index-Engine"))) do
    table.insert(engineLibDirs, directory)
end

local engineIncludeDirsForPremake = includeDirs
local engineBuildOptions = nil
local engineLibDirsForPremake = engineLibDirs
local engineLinkOptions = nil
if os.target() == "windows" then
    engineIncludeDirsForPremake = nil
    engineBuildOptions = {}
    for _, directory in ipairs(includeDirs) do
        table.insert(engineBuildOptions,
            '/I"' .. path.translate(directory, "\\") .. '"')
    end

    engineLibDirsForPremake = nil
    engineLinkOptions = {}
    for _, directory in ipairs(engineLibDirs) do
        table.insert(engineLinkOptions,
            '/LIBPATH:"' .. path.translate(directory, "\\") .. '"')
    end
end

IndexPackageUseDawnLibDirs = false

Dependency = {
    EditorRuntimeCommon = {
        IncludeDirs = engineIncludeDirsForPremake,
        BuildOptions = engineBuildOptions,
        LibDirs = engineLibDirsForPremake,
        LinkOptions = engineLinkOptions,
        Links = { "Index-Engine" },
        Defines = {
            "INDEX_ALL_MODULES=1",
            "GLFW_DLL",
            "IDX_RHI_WEBGPU",
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
end

local function FirstExistingPath(candidates)
    for _, candidate in ipairs(candidates) do
        if os.isfile(candidate) then return candidate end
    end
    return candidates[1]
end

IndexPackageScriptCoreAssemblies = {}
for _, configuration in ipairs({ "Debug", "Release", "Dist" }) do
    IndexPackageScriptCoreAssemblies[configuration] = FirstExistingPath({
        path.join(engineRoot, "bin/" .. configuration .. "-" .. os.target() .. "-x86_64/Index-ScriptCore/Index-ScriptCore.dll"),
        path.join(engineRoot, "Index-ScriptCore/Index-ScriptCore.dll"),
    })
end

local packageLoader = dofile(path.join(engineRoot, "premake/package-loader.lua"))
local packageCount = packageLoader.LoadProjectPackages(projectRoot)
if packageCount == 0 then
    group "Packages"
    project "IndexProjectPackages.Empty"
        location(path.join(workspaceRoot, "generated/IndexProjectPackages.Empty"))
        kind "Utility"
    group ""
end
