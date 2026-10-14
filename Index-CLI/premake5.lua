group "Tools"
project "Index-CLI"
    location "."
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    warnings "Extra"
    -- `index create`, `index build`, ... ; the project keeps the Index-* name for the solution.
    targetname "index"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "src/**.cpp",
        "src/**.hpp"
    }

    -- The editor is an executable, so the UI-free editor sources the CLI shares are compiled in
    -- directly (same pattern as Tests/Index-Editor-Tests).
    files
    {
        path.join(ROOT_DIR, "Index-Editor/src/Build/GameBuilder.cpp"),
        path.join(ROOT_DIR, "Index-Editor/src/Editor/BuildAssetPruning.cpp"),
        path.join(ROOT_DIR, "Index-Editor/src/Editor/IndexPackageInstaller.cpp"),
        path.join(ROOT_DIR, "Index-Editor/src/Editor/PlatformBuilder.cpp"),
        path.join(ROOT_DIR, "Index-Editor/src/Editor/SpriteSlicing.cpp")
    }

    UseDependencySet(Dependency.EditorRuntimeCommon)
    UseIndexProfilerDependencies(true)
    defines(GetIndexModuleDefines())
    ApplyIndexEditorModuleDefine()
    defines { "IDX_IMPORT_DLL" }
    includedirs { "src", path.join(ROOT_DIR, "Index-Editor/src") }

    -- IndexAssets: `create` copies them into new projects and builds stage them. ScriptCore: `create`
    -- seeds Packages/Index-ScriptCore from the copy next to the exe.
    postbuildcommands { CopyIndexAssets, CopyIndexEngineDll, CopySDL3SharedLib, CopyGladDll }
    if CopyDawnSharedLib ~= "" then postbuildcommands { CopyDawnSharedLib } end
    dependson(IndexManagedDependencies({ "Index-ScriptCore" }))
    postbuildcommands(CopyScriptCoreArtifacts)
    ApplyIndexProfilerRuntime()

    filter "system:windows"
        flags { "MultiProcessorCompile" }
        buildoptions { "/utf-8", "/FS", "/Zc:preprocessor" }
        systemversion "latest"
        defines { "IDX_PLATFORM_WINDOWS" }
        files { path.join(ROOT_DIR, "premake/Utf8CodePage.manifest") }
        -- Index-Engine.dll imports nethost even when no script ever runs.
        postbuildcommands {
            '{COPYFILE} "' .. path.join(ROOT_DIR, "External/dotnet/lib/nethost.dll") .. '" "%{cfg.targetdir}/nethost.dll"'
        }

    filter "system:linux"
        defines { "IDX_PLATFORM_LINUX" }
        linkoptions { "-Wl,-rpath,'$$ORIGIN'" }

    filter "system:macosx"
        defines { "IDX_PLATFORM_MACOS" }
        linkoptions { "-Wl,-rpath,@executable_path" }

    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
        defines { "IDX_DEBUG", "_DEBUG" }

    filter "configurations:Release"
        runtime "Release"
        optimize "On"
        symbols "On"
        defines { "IDX_RELEASE", "NDEBUG" }

    filter "configurations:Dist"
        runtime "Release"
        optimize "Full"
        symbols "Off"
        defines { "IDX_DIST", "NDEBUG" }

    filter {}

    ApplyDawnLibDirs("../")
group ""
