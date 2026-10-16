project "Funke-Engine-Tests"
    location "."
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    warnings "Extra"

    targetdir ("../../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../../bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "src/**.cpp",
        "src/**.hpp",
        "src/**.h"
    }

    -- Link the engine SharedLib so we can call into Funke:: functions directly
    -- (e.g. ParseInstalledPackagesFromXml).
    UseDependencySet(Dependency.EditorRuntimeCommon)
    UseFunkeProfilerDependencies(true)
    defines(GetFunkeModuleDefines())
    ApplyFunkeEditorModuleDefine()
    defines { "FNK_IMPORT_DLL" }

    -- doctest is a single-header lib at External/doctest/doctest/doctest.h.
    -- Include path "External/doctest" gives the conventional `#include <doctest/doctest.h>`.
    includedirs { path.join(ROOT_DIR, "External/doctest") }

    -- Same DLL-copying postbuild as Runtime so the test exe finds Funke-Engine.dll
    -- next to it at run time.
    postbuildcommands
    {
        CopyFunkeEngineDll,
        CopySDL3SharedLib,
        CopyGladDll
    }
    if CopyDawnSharedLib ~= "" then postbuildcommands { CopyDawnSharedLib } end
    ApplyFunkeProfilerRuntime()

    filter "system:windows"
        -- See Funke-Engine/premake5.lua for the rationale on
        -- MultiProcessorCompile + /Zc:preprocessor.
        flags { "MultiProcessorCompile" }
        buildoptions { "/utf-8", "/FS", "/Zc:preprocessor" }
        systemversion "latest"
        defines { "FNK_PLATFORM_WINDOWS" }
        postbuildcommands {
            '{COPYFILE} "' .. path.join(ROOT_DIR, "External/dotnet/lib/nethost.dll") .. '" "%{cfg.targetdir}/nethost.dll"'
        }

    filter "system:linux"
        defines { "FNK_PLATFORM_LINUX" }
        linkoptions { "-Wl,-rpath,'$$ORIGIN'" }

    filter "system:macosx"
        defines { "FNK_PLATFORM_MACOS" }
        linkoptions { "-Wl,-rpath,@executable_path" }

    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
        defines { "FNK_DEBUG", "_DEBUG" }

    filter "configurations:Release"
        runtime "Release"
        optimize "On"
        symbols "On"
        defines { "FNK_RELEASE", "NDEBUG" }

    filter "configurations:Dist"
        runtime "Release"
        optimize "Full"
        symbols "Off"
        defines { "FNK_DIST", "NDEBUG" }

    filter {}

    -- Per-config libdirs for webgpu_dawn.lib. Tests/ sits one level
    -- deeper than the top-level project folders so the rel-prefix is
    -- "../../" (two ups to reach the repo root).
    ApplyDawnLibDirs("../../")
