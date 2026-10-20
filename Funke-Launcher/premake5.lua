group "Runtime"
project "Funke-Launcher"
    location "."
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

    UseDependencySet(Dependency.EditorRuntimeCommon)
    UseFunkeProfilerDependencies(true)
    defines(GetFunkeModuleDefines())
    ApplyFunkeEditorModuleDefine()
    defines { "FNK_IMPORT_DLL" }
    includedirs { "src" }
    -- See WriteFunkeEntityBitsConfigHeader() in the root premake5.lua.
    includedirs { FunkeEntityBitsConfigIncludeDir }

    -- The ImGui WebGPU backend lives inside Funke-Engine.dll so the
    -- launcher and engine share one wgpu::Device. The launcher just
    -- `#includes` the engine header which exports the API via
    -- FUNKE_API.

    -- CopyEngineSdk materializes a bundled EngineSDK/ next to the launcher
    -- exe so a zipped distribution works without the source tree. The SDK
    -- bundles Funke-ScriptCore.dll, so we depend on the C# project to
    -- ensure it exists before our postbuild runs. Funke-Managed builds the
    -- C# projects on Linux / macOS (see root premake5.lua).
    dependson { "Funke-Editor", "Funke-Runtime" }
    dependson(FunkeManagedDependencies({ "Funke-ScriptCore", "Funke-PackageTool" }))
    if os.target() ~= "windows" then dependson { "Funke-CLI" } end

    postbuildcommands { CopyFunkeAssets, CopyFunkeEngineDll, CopySDL3SharedLib, CopyGladDll, CopyEngineSdk }
    if CopyDawnSharedLib ~= "" then postbuildcommands { CopyDawnSharedLib } end
    ApplyFunkeProfilerRuntime()

    filter "system:windows"
        -- See Funke-Engine/premake5.lua for the rationale on
        -- MultiProcessorCompile + /Zc:preprocessor.
        FunkeMultiProcessorCompile()
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
