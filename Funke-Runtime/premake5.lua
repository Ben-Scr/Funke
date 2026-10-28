project "Funke-Runtime"
    location (FunkeProjectLocation("Funke-Runtime", "."))
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    cdialect "C17"
    staticruntime "off"
    warnings "Extra"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "src/**.cpp",
        "src/**.h",
        "src/**.hpp"
    }

    -- icon.rc is a Windows resource (app icon); Linux has no .rc toolchain. Windows-only.
    filter "system:windows"
        -- UTF-8 process code page: the engine passes UTF-8 std::strings to std::filesystem and narrow Win32 APIs.
        files { "icon.rc", path.join(ROOT_DIR, "premake/Utf8CodePage.manifest") }
    filter {}

    -- When the profiler is enabled the runtime hosts the same ProfilerPanel
    -- the editor uses. We compile its .cpp into the runtime binary directly
    -- (rather than splitting it into a third project) and add the editor's
    -- src as an include dir so the panel header resolves. Stays out of the
    -- runtime entirely when --no-profiler strips the macro.
    -- The panel is built from the editor UI kit (EditorUI -> ImGuiUtils ->
    -- EditorIcons; EditorTheme is header-only), so those come along too.
    if FunkeProfiler.Enabled then
        files {
            path.join(ROOT_DIR, "Funke-Editor/src/Gui/ProfilerPanel.cpp"),
            path.join(ROOT_DIR, "Funke-Editor/src/Gui/EditorUI.cpp"),
            path.join(ROOT_DIR, "Funke-Editor/src/Gui/ImGuiUtils.cpp"),
            path.join(ROOT_DIR, "Funke-Editor/src/Gui/EditorIcons.cpp")
        }
        includedirs { path.join(ROOT_DIR, "Funke-Editor/src") }
    end

    -- Engine-level diagnostic overlays (StatsOverlay = F6, LogOverlay = F7,
    -- and any future additions). Glob picks up new files automatically.
    -- Same cross-binary share as ProfilerPanel: the .cpp files use ImGui
    -- and are excluded from the engine DLL; we compile them into the
    -- runtime exe so the F6/F7 toggle layers can drive them.
    files { path.join(ROOT_DIR, "Funke-Engine/src/Diagnostics/**.cpp") }

    -- The ImGui WebGPU backend lives inside Funke-Engine.dll so the
    -- runtime exe and engine share one wgpu::Device. RuntimeImGuiHost.cpp
    -- `#includes "Gui/ImGuiImplWebGPU.hpp"` from the engine include path
    -- and links the FUNKE_API exports.

    UseDependencySet(Dependency.EditorRuntimeCommon)
    UseFunkeProfilerDependencies(true)
    defines(GetFunkeModuleDefines())
    ApplyFunkeEditorModuleDefine()
    defines { "FNK_IMPORT_DLL" }
    -- See WriteFunkeEntityBitsConfigHeader() in the root premake5.lua.
    includedirs { FunkeEntityBitsConfigIncludeDir }

    -- ScriptSystem loads Funke-ScriptCore.dll at runtime. The packaged-layout
    -- branch (ScriptSystem.cpp:194) finds it next to the runtime exe; the
    -- copy lets a zipped Runtime folder work standalone. The managed C# project
    -- is built by the solution on Windows and by Funke-Managed elsewhere (see root premake5.lua).
    if not FunkeVariantRoot then
        dependson(FunkeManagedDependencies({ "Funke-ScriptCore" }))
    end
    postbuildcommands
    {
        CopyFunkeAssets,
        CopyFunkeEngineDll,
        CopySDL3SharedLib
    }
    if CopyDawnSharedLib ~= "" then postbuildcommands { CopyDawnSharedLib } end
    postbuildcommands(CopyScriptCoreArtifacts)
    ApplyFunkeProfilerRuntime()

    filter "system:windows"
        -- See Funke-Engine/premake5.lua for the rationale on
        -- MultiProcessorCompile + /Zc:preprocessor.
        FunkeMultiProcessorCompile()
        buildoptions { "/utf-8", "/FS", "/Zc:preprocessor" }
        systemversion "latest"
        links { "%{Library.GDI32}" }
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
        symbols "On"
        defines { "FNK_DIST", "NDEBUG" }

    filter { "system:windows", "configurations:Release or Dist" }
        kind "WindowedApp"
        defines { "FNK_WINDOWED_APP=1" }
        buildoptions { "/guard:cf" }
        linkoptions { "/DYNAMICBASE", "/NXCOMPAT", "/HIGHENTROPYVA", "/guard:cf" }

    filter {}

    -- Per-config libdirs for webgpu_dawn.lib (inherited via EngineCore-
    -- Render's Links). See ApplyDawnLibDirs in the root premake5.lua for
    -- the runtime-mismatch (LNK2038) rationale.
    ApplyDawnLibDirs("../")
