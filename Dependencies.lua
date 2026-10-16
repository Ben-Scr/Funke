IncludeDir = {}
IncludeDir["ExternalRoot"] = "External"
IncludeDir["ImGui"] = "External/imgui"
IncludeDir["Spdlog"] = "External/spdlog/include"
IncludeDir["SDL3"] = "External/SDL/include"
IncludeDir["IndexPhysics"] = "External/Index-Physics/include"
IncludeDir["Box2D"] = "External/box2d/include"
IncludeDir["Jolt"] = "External/JoltPhysics"
IncludeDir["GLM"] = "External/glm"
IncludeDir["EnTT"] = "External/entt/src"
IncludeDir["STB"] = "External/stb"
IncludeDir["MagicEnum"] = "External/magic_enum/include"
IncludeDir["MiniAudio"] = "External/miniaudio"
IncludeDir["ConcurrentQueue"] = "External/concurrentqueue"
IncludeDir["RapidXml"] = "External/rapidxml"
IncludeDir["Glad"] = "External/glad/include"
IncludeDir["DotNet"] = "External/dotnet"
IncludeDir["FunkeEngine"] = "Funke-Engine/src"
IncludeDir["FunkeEngineLegacy"] = "Funke-Engine/src"
IncludeDir["FunkeEditorRuntime"] = "Funke-EditorRuntime/src"
IncludeDir["Tracy"] = "External/tracy/public"

local isWindowsTarget = os.target() == "windows"
local isLinuxTarget = os.target() == "linux"

local function AppendUnique(target, values)
    if not values then
        return
    end

    local seen = {}
    for _, value in ipairs(target) do
        seen[value] = true
    end

    for _, value in ipairs(values) do
        if not seen[value] then
            table.insert(target, value)
            seen[value] = true
        end
    end
end

-- Concatenate any number of list-style tables into a single new table,
-- dropping duplicates. Used by the per-RHI dep sets below to splice in
-- the active backend's include/lib/define lists without mutating the
-- inputs. Returns a fresh table — callers can keep mutating it freely.
function MergeListsForDep(...)
    local result = {}
    local seen = {}
    for i = 1, select("#", ...) do
        local list = select(i, ...)
        if list then
            for _, value in ipairs(list) do
                if not seen[value] then
                    table.insert(result, value)
                    seen[value] = true
                end
            end
        end
    end
    return result
end

function MergeDependencySets(...)
    local merged =
    {
        IncludeDirs = {},
        LibDirs = {},
        DependsOn = {},
        Links = {},
        Defines = {}
    }

    for index = 1, select("#", ...) do
        local dependency = select(index, ...)
        if dependency then
            AppendUnique(merged.IncludeDirs, dependency.IncludeDirs)
            AppendUnique(merged.LibDirs, dependency.LibDirs)
            AppendUnique(merged.DependsOn, dependency.DependsOn)
            AppendUnique(merged.Links, dependency.Links)
            AppendUnique(merged.Defines, dependency.Defines)
        end
    end

    return merged
end

local MergeDependencies = MergeDependencySets

Library = {}
Library["Box2D"] = "box2d.lib"
Library["FreeType"] = "freetype.lib"
Library["OpenGL"] = "%{cfg.system == 'windows' and 'opengl32.lib' or 'GL'}"
Library["GDI32"] = "gdi32.lib"
Library["NetHost"] = isWindowsTarget and "nethost.lib" or "nethost"

LibDir = {}
-- Set on every target: Windows links nethost.lib, Unix links libnethost.a
-- (both copied into External/dotnet/lib by scripts/Setup.py).
LibDir["DotNet"] = "External/dotnet/lib"

Dependency = {}
Dependency["ImGui"] =
{
    -- imgui_impl_sdl3 serves the consumer executables
    -- (editor / launcher / runtime). imgui_impl_wgpu serves engine.dll
    -- (Gui/ImGuiImpl.cpp wraps it). Dawn's include paths splice in so
    -- imgui_impl_wgpu.cpp can find <webgpu/webgpu.h>; the WGPU_SHARED_-
    -- LIBRARY=0 define carries through too so every TU that touches the
    -- WebGPU C ABI agrees on linkage.
    IncludeDirs = MergeListsForDep(
        { "%{IncludeDir.ImGui}", "%{IncludeDir.ImGui}/backends", "%{IncludeDir.SDL3}" },
        DawnLayout and DawnLayout.IncludeDirs or {}
    ),
    Defines = MergeListsForDep(
        {},
        DawnLayout and DawnLayout.Defines or {}
    ),
    BuildProject = true
}

Dependency["Spdlog"] =
{
    IncludeDirs = { "%{IncludeDir.Spdlog}" },
    HeaderOnly = true
}

Dependency["ExternalIncludes"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.SDL3}",
        "%{IncludeDir.Box2D}",
        "%{IncludeDir.GLM}",
        "%{IncludeDir.EnTT}",
        "%{IncludeDir.STB}",
        "%{IncludeDir.MagicEnum}",
        "%{IncludeDir.MiniAudio}",
        "%{IncludeDir.RapidXml}",
        "%{IncludeDir.IndexPhysics}"
    }
}

-- Minimal public core contract:
-- - Funke-Engine/src headers
-- - Core/Export.hpp feature metadata
-- - Funke.hpp lean umbrella
-- - header-only/public dependencies required by that core surface
Dependency["EngineCore"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.FunkeEngine}",
        "%{IncludeDir.FunkeEngineLegacy}",
        "%{IncludeDir.Spdlog}",
        "%{IncludeDir.GLM}",
        "%{IncludeDir.EnTT}",
        "%{IncludeDir.STB}",
        "%{IncludeDir.MagicEnum}",
        -- RapidXML (vendored single header at External/rapidxml) — consumed
        -- only by Packages/CsprojParser.cpp for .csproj/.props XML parsing.
        -- Kept global because per-file premake filters don't reliably emit
        -- AdditionalIncludeDirectories on vcxproj; it's a one-line cost for a
        -- header path that only one .cpp consumes.
        "%{IncludeDir.RapidXml}",
        -- moodycamel::ConcurrentQueue — header-only, BSD-2-Clause / Boost
        -- dual-licensed. Consumed only by Jobs/JobSystem.cpp as a lock-free
        -- MPMC replacement for the prior std::mutex + std::deque queue.
        -- Added globally for the same reason RapidXML is (per-file scoping
        -- doesn't emit on vcxproj reliably).
        "%{IncludeDir.ConcurrentQueue}",
        -- SDL3 is the platform layer (paths, dynamic libraries, dialogs,
        -- locale, threads), so even core-only builds link it.
        "%{IncludeDir.SDL3}"
    },

    LibDirs = MergeListsForDep(SDL3Layout and SDL3Layout.LibDirs or {}),

    DependsOn = {},

    Links = MergeListsForDep(SDL3Layout and SDL3Layout.Links or {})
}

-- Render-API dep set — WebGPU via Dawn. Dawn is built out-of-band by
-- scripts/dawn/SetupDawn.bat into a monolithic webgpu_dawn.lib;
-- premake/dependencies/dawn.lua exposes DawnLayout with the resulting
-- include + lib paths. No DependsOn entry for a premake project because
-- Dawn isn't built by premake — it's a vendored pre-built static lib.
Dependency["EngineCoreRender"] =
{
    IncludeDirs = MergeListsForDep(
        { "%{IncludeDir.SDL3}" },
        DawnLayout and DawnLayout.IncludeDirs or {}
    ),

    -- Dawn's webgpu_dawn.lib lives under either Debug\ or Release\, and
    -- mixing runtime libraries (/MDd Debug vs /MD Release) trips MSVC's
    -- "_ITERATOR_DEBUG_LEVEL" + "RuntimeLibrary" mismatch (LNK2038). So
    -- LibDirs MUST be set per-config, not as a single default. The
    -- per-config wiring lives in Funke-Engine/premake5.lua via explicit
    -- `filter "configurations:Debug" / Release / Dist` blocks that
    -- libdirs() each into the matching Dawn output folder.
    LibDirs = {},

    Defines = MergeListsForDep(
        { "FNK_RHI_WEBGPU" },
        DawnLayout and DawnLayout.Defines or {}
    ),

    DependsOn = {},

    Links = MergeListsForDep(
        DawnLayout and DawnLayout.Links or {}
    )
}

Dependency["EngineCoreAudio"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.MiniAudio}"
    },

    LibDirs = {},

    DependsOn = {},

    Links = {}
}

-- Physics is split by engine feature module so a runtime variant links only what it ships.
Dependency["EngineCorePhysics2D"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.Box2D}",
        "%{IncludeDir.IndexPhysics}"
    },

    LibDirs = {},

    DependsOn =
    {
        "Box2D",
        "Index-Physics"
    },

    Links =
    {
        "Box2D",
        "Index-Physics"
    }
}

Dependency["EngineCorePhysics3D"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.Jolt}"
    },

    LibDirs = {},

    DependsOn =
    {
        "Jolt"
    },

    Links =
    {
        "Jolt"
    }
}

Dependency["EngineCorePhysics"] = MergeDependencies(
    Dependency["EngineCorePhysics2D"],
    Dependency["EngineCorePhysics3D"]
)

Dependency["EngineCoreScripting"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.DotNet}"
    },

    LibDirs = {},

    DependsOn = {},

    Links =
    {
        "%{Library.NetHost}"
    }
}

Dependency["EngineCoreEditor"] =
{
    IncludeDirs =
    {
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.ImGui}/backends"
    },

    LibDirs = {},

    DependsOn =
    {
        "ImGui"
    },

    Links =
    {
        "ImGui"
    }
}

-- Tracy client — available to development configurations when FunkeProfiler.Enabled.
-- Engine.dll + editor.exe + runtime.exe attach the same Tracy client SharedLib so
-- there's exactly one client instance per process. Header `Tracy.hpp`
-- sees TRACY_IMPORTS on consumer side -> dllimport, matching the
-- TRACY_EXPORTS in the Tracy project itself.
if FunkeProfiler and FunkeProfiler.Enabled then
    Dependency["Profiler"] =
    {
        IncludeDirs = { "%{IncludeDir.Tracy}" },
        DependsOn   = { "Tracy" },
        Links       = { "Tracy" },
        -- TRACY_ON_DEMAND must match the Tracy lib build (premake/dependencies/tracy.lua).
        -- Mismatched on/off across consumer + lib produces ABI drift in the SourceLocationData
        -- struct, which is hashed by Tracy at runtime — wrong size = corrupted zones.
        Defines     = { "FUNKE_PROFILER_ENABLED", "TRACY_ENABLE", "TRACY_IMPORTS", "TRACY_ON_DEMAND" }
    }
else
    Dependency["Profiler"] = { IncludeDirs = {}, DependsOn = {}, Links = {}, Defines = {} }
end

-- Keep Linux system libraries at the end of the final engine link line. Both
-- Dawn's external static archive and .NET's static libnethost can reference
-- dl/pthread symbols, and static archive resolution is order-sensitive.
Dependency["EngineCoreLinuxSystem"] =
{
    IncludeDirs = {},
    LibDirs = {},
    DependsOn = {},
    Links = isLinuxTarget and { "pthread", "dl" } or {},
    Defines = {}
}

Dependency["EngineCoreAllModules"] = MergeDependencies(
    Dependency["EngineCore"],
    Dependency["EngineCoreRender"],
    Dependency["EngineCoreAudio"],
    Dependency["EngineCorePhysics"],
    Dependency["EngineCoreScripting"],
    Dependency["EngineCoreEditor"],
    Dependency["Profiler"],
    Dependency["EngineCoreLinuxSystem"]
)

-- Explicit legacy/full-module compatibility path for consumers that opt into FUNKE_ALL_MODULES.
Dependency["EngineCoreLegacy"] = Dependency["EngineCoreAllModules"]

-- NOTE: `EditorRuntimeCommon` is re-defined in the root `premake5.lua`
-- (after this file is loaded), where it picks up `Funke-EditorRuntime`
-- in its DependsOn/Links list. This earlier definition exists only for
-- compatibility with consumers that still resolve the dep set during
-- Dependencies.lua load order; the root file's definition supersedes
-- it at premake-run time.
Dependency["EditorRuntimeCommon"] = MergeDependencies(
    {
        IncludeDirs = { "%{IncludeDir.FunkeEditorRuntime}" },
        DependsOn = { "Funke-Engine", "Funke-EditorRuntime" },
        Links = { "Funke-Engine", "Funke-EditorRuntime" }
    },
    Dependency["EngineCoreAllModules"]
)

if LibDir["DotNet"] then
    table.insert(Dependency["EngineCoreScripting"].LibDirs, "%{LibDir.DotNet}")
    table.insert(Dependency["EngineCoreAllModules"].LibDirs, "%{LibDir.DotNet}")
    table.insert(Dependency["EditorRuntimeCommon"].LibDirs, "%{LibDir.DotNet}")
end
