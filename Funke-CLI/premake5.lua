group "Tools"
project "Funke-CLI"
    location "."
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "off"
    warnings "Extra"
    -- `funke create`, `funke build`, ... ; the project keeps the Funke-* name for the solution.
    targetname "funke"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "src/**.cpp",
        "src/**.hpp"
    }

    -- The editor is an executable, so the UI-free editor sources the CLI shares are compiled in
    -- directly (same pattern as Tests/Funke-Editor-Tests).
    files
    {
        path.join(ROOT_DIR, "Funke-Editor/src/Build/GameBuilder.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Build/DotNetRuntimeBundle.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Editor/BuildAssetPruning.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Editor/FunkePackageInstaller.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Editor/PlatformBuilder.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Editor/SpriteSlicing.cpp")
    }

    UseDependencySet(Dependency.EditorRuntimeCommon)
    UseFunkeProfilerDependencies(true)
    defines(GetFunkeModuleDefines())
    ApplyFunkeEditorModuleDefine()
    defines { "FNK_IMPORT_DLL" }
    includedirs { "src", path.join(ROOT_DIR, "Funke-Editor/src") }

    -- FunkeAssets: `create` copies them into new projects and builds stage them. ScriptCore: `create`
    -- seeds Packages/Funke-ScriptCore from the copy next to the exe.
    postbuildcommands { CopyFunkeAssets, CopyFunkeEngineDll, CopySDL3SharedLib }
    if CopyDawnSharedLib ~= "" then postbuildcommands { CopyDawnSharedLib } end
    dependson(FunkeManagedDependencies({ "Funke-ScriptCore" }))
    postbuildcommands(CopyScriptCoreArtifacts)
    ApplyFunkeProfilerRuntime()

    filter "system:windows"
        FunkeMultiProcessorCompile()
        buildoptions { "/utf-8", "/FS", "/Zc:preprocessor" }
        systemversion "latest"
        defines { "FNK_PLATFORM_WINDOWS" }
        files { path.join(ROOT_DIR, "premake/Utf8CodePage.manifest") }
        -- Funke-Engine.dll imports nethost even when no script ever runs.
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

    ApplyDawnLibDirs("../")
group ""
