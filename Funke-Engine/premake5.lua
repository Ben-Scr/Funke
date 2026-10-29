group "Core"
project "Funke-Engine"
    location (FunkeProjectLocation("Funke-Engine", "."))
    kind "SharedLib"
    language "C++"
    cppdialect "C++20"
    cdialect "C17"
    staticruntime "off"
    warnings "Extra"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    -- FNK_BUILD_DLL flips FUNKE_API to __declspec(dllexport) inside the engine TUs.
    -- Consumers (Editor / Launcher / Runtime / engine_core packages) define FNK_IMPORT_DLL.
    defines { "FNK_BUILD_DLL" }

    pchheader "pch.hpp"
    pchsource "src/pch.cpp"

    files
    {
        "src/**.h",
        "src/**.hpp",
        "src/**.c",
        "src/**.cpp"
    }

    filter "system:windows"
        files { "version.rc" }
    filter {}

    local function RemoveFilesIfModuleDisabled(moduleEnabled, filePatterns)
        if not moduleEnabled then
            removefiles(filePatterns)
        end
    end

    local function RemoveFilesUnlessModulesEnabled(requiredModules, filePatterns)
        for _, moduleName in ipairs(requiredModules) do
            if not FunkeModules[moduleName] then
                removefiles(filePatterns)
                return
            end
        end
    end

    -- These integration units still aggregate multiple optional modules.
    RemoveFilesUnlessModulesEnabled({ "Render", "Audio", "Physics", "Scripting", "Editor" },
        {
            "src/Core/Application.*",
            "src/Graphics/RenderSettings.cpp",
            "src/Serialization/PrefabTemplateCache.*"
        }
    )

    RemoveFilesUnlessModulesEnabled({ "Render" },
        {
            "src/Core/Input.*",
            "src/Core/Window.*"
        }
    )

    RemoveFilesUnlessModulesEnabled({ "Physics" },
        {
            "src/Components/General/Transform2DComponent.cpp"
        }
    )

    RemoveFilesUnlessModulesEnabled({ "Render", "Audio", "Physics", "Scripting" },
        {
            -- Glob: covers the per-category slices as well as the orchestrator.
            "src/Scene/BuiltInComponentRegistration*.cpp",
            "src/Scene/ComponentRegistry.cpp",
            "src/Scene/Entity.cpp",
            "src/Scene/EntityHelper.cpp",
            "src/Scene/Scene.cpp",
            "src/Scene/SceneDefinition.cpp",
            "src/Scene/SceneExecutionContext.cpp",
            "src/Scene/SceneManager.cpp",
            "src/Serialization/SceneSerializer.cpp",
            "src/Serialization/SceneSerializerDeserialize.cpp"
        }
    )

    RemoveFilesIfModuleDisabled(FunkeModules.Render,
        {
            "src/Core/Platform/**",
            "src/Components/Graphics/**",
            "src/Graphics/**",
            "src/Gui/**",
            "src/Packages/PackageImGuiBridge.*",
            "src/Profiling/GpuTimer.*",
            "src/Scene/EntityPicker.*",
            "src/Systems/GizmosDebugSystem.*",
            "src/Systems/LocalizedTextSystem.*",
            "src/Systems/ParticleUpdateSystem.*",
            "src/Systems/UIEventSystem.*",
            "src/Systems/UIFocusSystem.*",
            "src/Systems/UILayoutSystem.*"
        }
    )

    -- ── Render-API backend ─────────────────────────────────────────
    -- WebGPU via Dawn. The engine has one graphics backend; the runtime
    -- D3D12 / Vulkan / Metal choice underneath is Dawn's call at adapter-
    -- request time (see Backend/WebGPUApi.cpp::RequestAdapterSync).
    --
    -- FNK_RHI_WEBGPU is defined for compatibility with TUs that still
    -- check the macro.
    if FunkeModules.Render then
        defines { "FNK_RHI_WEBGPU" }
    end

    RemoveFilesIfModuleDisabled(FunkeModules.Audio,
        {
            "src/Audio/**",
            "src/Components/Audio/**",
            "src/Systems/AudioUpdateSystem.*"
        }
    )

    RemoveFilesIfModuleDisabled(FunkeModules.Physics,
        {
            "src/Components/Physics/**",
            "src/Components/Physics3D/**",
            "src/Physics/**",
            "src/Physics3D/**",
            "src/Systems/TransformHierarchySystem.*"
        }
    )

    RemoveFilesIfModuleDisabled(FunkeModules.Scripting,
        {
            "src/Scripting/**"
        }
    )

    -- Engine feature modules (Funke.2D / Funke.3D): source lists in EngineModules.json.
    for _, featureModule in ipairs(FunkeFeatureModules) do
        RemoveFilesIfModuleDisabled(featureModule.Enabled, featureModule.sources)
    end

    -- Profiler is gated by the standalone --no-profiler premake flag, not
    -- by the module profile. When stripped, drop the entire Profiling/
    -- folder so no Tracy headers are pulled in and no symbols are emitted.
    RemoveFilesIfModuleDisabled(FunkeProfiler.Enabled,
        {
            "src/Profiling/**"
        }
    )

    -- Diagnostics/StatsOverlay.cpp uses ImGui directly, but the engine DLL
    -- doesn't link ImGui (it's an editor/runtime concern). The header
    -- (StatsOverlay.hpp) only forward-declares ImVec2 so it stays clean to
    -- include in engine code; the .cpp is excluded here and re-added by
    -- Funke-Editor's and Funke-Runtime's premake (same pattern as the
    -- ProfilerPanel cross-binary share).
    removefiles { "src/Diagnostics/**.cpp" }


    UseFunkeEngineModuleDependencies()
    defines(GetFunkeModuleDefines())
    ApplyFunkeEditorModuleDefine()
    -- The editor's Rebuild Engine flow rewrites FunkeEntityBitsConfig.h
    -- to override FUNKE_ENTITY_BITS without a premake regen. Every C++
    -- project that compiles entt-using code needs this folder on its
    -- include path so the EnTT patch's `#include "FunkeEntityBitsConfig.h"`
    -- resolves. See WriteFunkeEntityBitsConfigHeader() in root premake5.lua.
    includedirs { FunkeEntityBitsConfigIncludeDir }

    local function AddPrivateIncludes(filePatterns, includePaths)
        for _, pattern in ipairs(filePatterns) do
            filter("files:" .. pattern)
                includedirs(includePaths)
        end

        filter {}
    end

    local renderIncludes = {
        "../External/SDL/include",
    }

    -- Splice in Dawn's header paths so the WebGPU backend + every
    -- renderer can resolve <webgpu/webgpu_cpp.h>. Engine premake runs
    -- from Funke-Engine/ so the include paths (which are repo-root-
    -- relative in DawnLayout) need a "../" prefix to match the
    -- convention used by the other renderIncludes entries.
    if DawnLayout then
        for _, p in ipairs(DawnLayout.IncludeDirs) do
            table.insert(renderIncludes, "../" .. p)
        end
    end

    local audioIncludes =
    {
        "../External/miniaudio"
    }

    local physicsIncludes =
    {
        "../External/box2d/include"
    }

    local scriptingIncludes =
    {
        "../External/dotnet"
    }

    if FunkeModules.Render then
        AddPrivateIncludes(
            {
                "src/Core/Application.cpp",
                "src/Core/Application.hpp",
                "src/Core/Input.hpp",
                "src/Core/Input.cpp",
                "src/Core/Platform/**",
                "src/Core/Window.cpp",
                "src/Core/Window.hpp",
                "src/Graphics/**",
                "src/Gui/**",
                "src/Systems/GizmosDebugSystem.cpp"
            },
            renderIncludes
        )
    end

    if FunkeModules.Audio then
        AddPrivateIncludes(
            {
                "src/Audio/**",
                "src/Components/Audio/**",
                "src/Scripting/ScriptBindings.cpp",
                "src/Scripting/ScriptBindingsAssets.cpp",
                "src/Scripting/ScriptBindingsScene.cpp",
                "src/Serialization/SceneSerializer.cpp",
                "src/Serialization/SceneSerializerDeserialize.cpp",
                "src/Serialization/SceneSerializerShared.hpp"
            },
            audioIncludes
        )
    end

    if FunkeModules.Physics then
        AddPrivateIncludes(
            {
                "src/Core/Application.cpp",
                "src/Core/Application.hpp",
                "src/Components/Physics/**",
                "src/Physics/**",
                "src/Scene/Scene.cpp",
                "src/Scripting/ScriptBindings.cpp",
                "src/Scripting/ScriptBindingsScene.cpp",
                "src/Serialization/SceneSerializer.cpp",
                "src/Serialization/SceneSerializerDeserialize.cpp"
            },
            physicsIncludes
        )
    end

    if FunkeModules.Scripting then
        AddPrivateIncludes(
            {
                "src/Scene/BuiltInComponentRegistration*.cpp",
                "src/Scene/Scene.cpp",
                "src/Scene/SceneManager.cpp",
                "src/Scripting/**",
                "src/Serialization/SceneSerializer.cpp",
                "src/Serialization/SceneSerializerDeserialize.cpp"
            },
            scriptingIncludes
        )
    end

    filter "system:windows"
        -- MultiProcessorCompile emits <MultiProcessorCompilation>true</...>
        -- into the .vcxproj, so MSBuild + the VS IDE pick up /MP without
        -- needing the team's "-m" command-line wrapper. /FS keeps the
        -- concurrent PDB writes safe (C1041 without it).
        --
        -- /Zc:preprocessor selects MSVC's standards-conformant preprocessor.
        -- Required by some EnTT and magic_enum constexpr paths and required
        -- by Tracy v0.11+; settling on it engine-wide keeps every TU
        -- consistent.
        FunkeMultiProcessorCompile()
        buildoptions { "/utf-8", "/FS", "/Zc:preprocessor" }
        systemversion "latest"
        defines { "FNK_PLATFORM_WINDOWS" }
        -- dwmapi: Win32Titlebar uses DwmExtendFrameIntoClientArea for
        -- custom-titlebar shadows and DwmSetWindowAttribute for native
        -- titlebar dark mode/colors. user32 / gdi32 come through SDL3.
        -- dbghelp: CrashReporter writes minidumps via MiniDumpWriteDump.
        links { "dwmapi.lib", "dbghelp.lib" }

    filter "system:linux"
        pic "On"
        defines { "FNK_PLATFORM_LINUX" }
        linkoptions { "-Wl,-rpath,'$$ORIGIN'" }

    filter "system:macosx"
        defines { "FNK_PLATFORM_MACOS" }
        linkoptions { "-Wl,-rpath,@loader_path" }
        -- Dawn's Metal backend and SDL's Cocoa video backend.
        links { "Cocoa.framework", "Metal.framework", "QuartzCore.framework", "IOKit.framework", "IOSurface.framework" }

    filter {}

    if FunkeCoreValidationRoot then
        filter "system:linux"
            linkoptions { "-Wl,--no-undefined" }
        filter {}
    end

    -- (RapidXML include is provided globally via Dependency.EngineCore — the
    -- per-file scoping attempt didn't reliably propagate to vcxproj; see
    -- the commentary in Dependencies.lua next to %{IncludeDir.RapidXml}.)

    -- Built-in component registration is split one TU per category
    -- (BuiltInComponentRegistration<Category>.cpp); each Register<T> instantiates
    -- EnTT storage for its component and each Properties::Make* two std::function
    -- erasures. As one file that blew past the COFF section limit (C1128). The
    -- slices are far smaller, but /Z7 embeds debug info in the .obj, so /bigobj
    -- stays on the whole set as cheap insurance.
    filter { "files:**/BuiltInComponentRegistration*.cpp", "system:windows" }
        buildoptions { "/bigobj" }

    -- ScriptBindings.cpp hosts every Funke_<Component>_<Member> binding for the
    -- C#/native bridge. Each WIDGET_*_BINDING macro expands into a pair of free
    -- functions, and with every UI widget covered the TU's section count exceeds
    -- the COFF default limit (C1128). Same fix as BuiltInComponentRegistration.
    filter { "files:**/ScriptBindings.cpp", "system:windows" }
        buildoptions { "/bigobj" }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
        defines { "FNK_DEBUG", "_DEBUG", "FNK_TRACK_MEMORY" }

    filter "configurations:Release"
        runtime "Release"
        optimize "On"
        symbols "On"
        defines { "FNK_RELEASE", "NDEBUG" }

    filter { "system:windows", "configurations:Release or Dist" }
        buildoptions { "/guard:cf" }
        linkoptions { "/DYNAMICBASE", "/NXCOMPAT", "/HIGHENTROPYVA", "/guard:cf" }

    filter "configurations:Dist"
        runtime "Release"
        optimize "Full"
        symbols "On"
        -- LTCG lets the linker drop and fold engine code across TUs; this DLL ships in every game.
        linktimeoptimization "On"
        defines { "FNK_DIST", "NDEBUG" }

    -- Per-config libdirs for webgpu_dawn.lib. See ApplyDawnLibDirs in
    -- the root premake5.lua for the runtime-mismatch (LNK2038) rationale.
    ApplyDawnLibDirs("../")

    if CopyDawnSharedLib ~= "" then postbuildcommands { CopyDawnSharedLib } end
    -- gmake2 gives consumers a RUNPATH to this folder, and RUNPATH only covers their direct dependencies:
    -- the engine library resolves SDL3 through its own $ORIGIN, so SDL3 has to sit next to it as well.
    if os.target() ~= "windows" then postbuildcommands { CopySDL3SharedLib } end
