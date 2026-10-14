group "Runtime"
project "Index-Launcher"
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
    UseIndexProfilerDependencies(true)
    defines(GetIndexModuleDefines())
    ApplyIndexEditorModuleDefine()
    defines { "IDX_IMPORT_DLL" }
    includedirs { "src" }
    -- See WriteIndexEntityBitsConfigHeader() in the root premake5.lua.
    includedirs { IndexEntityBitsConfigIncludeDir }

    -- The ImGui WebGPU backend lives inside Index-Engine.dll so the
    -- launcher and engine share one wgpu::Device. The launcher just
    -- `#includes` the engine header which exports the API via
    -- INDEX_API.

    -- CopyEngineSdk materializes a bundled EngineSDK/ next to the launcher
    -- exe so a zipped distribution works without the source tree. The SDK
    -- bundles Index-ScriptCore.dll, so we depend on the C# project to
    -- ensure it exists before our postbuild runs. Index-Managed builds the
    -- C# projects on Linux / macOS (see root premake5.lua).
    dependson { "Index-Editor", "Index-Runtime" }
    dependson(IndexManagedDependencies({ "Index-ScriptCore", "Index-PackageTool" }))
    if os.target() ~= "windows" then dependson { "Index-CLI" } end

    postbuildcommands { CopyIndexAssets, CopyIndexEngineDll, CopySDL3SharedLib, CopyGladDll, CopyEngineSdk }
    if CopyDawnSharedLib ~= "" then postbuildcommands { CopyDawnSharedLib } end
    ApplyIndexProfilerRuntime()

    filter "system:windows"
        -- See Index-Engine/premake5.lua for the rationale on
        -- MultiProcessorCompile + /Zc:preprocessor.
        flags { "MultiProcessorCompile" }
        buildoptions { "/utf-8", "/FS", "/Zc:preprocessor" }
        systemversion "latest"
        defines { "IDX_PLATFORM_WINDOWS" }

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

    filter { "system:windows", "configurations:Release or Dist" }
        kind "WindowedApp"
        defines { "IDX_WINDOWED_APP=1" }
        buildoptions { "/guard:cf" }
        linkoptions { "/DYNAMICBASE", "/NXCOMPAT", "/HIGHENTROPYVA", "/guard:cf" }

    filter {}

    -- Per-config libdirs for webgpu_dawn.lib (inherited via EngineCore-
    -- Render's Links). See ApplyDawnLibDirs in the root premake5.lua for
    -- the runtime-mismatch (LNK2038) rationale.
    ApplyDawnLibDirs("../")
