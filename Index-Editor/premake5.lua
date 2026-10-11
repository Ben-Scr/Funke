group "Runtime"
project "Index-Editor"
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
        files { "icon.rc" }
    filter {}

    -- Engine-level diagnostic overlays (StatsOverlay, LogOverlay, …).
    -- Same cross-binary share pattern as ProfilerPanel: the .cpp files use
    -- ImGui directly and are excluded from the engine DLL; we compile them
    -- into the editor binary so the Game View toolbar can drive them.
    files { path.join(ROOT_DIR, "Index-Engine/src/Diagnostics/**.cpp") }

    -- The ImGui WebGPU backend lives inside Index-Engine.dll so the
    -- editor and engine share one wgpu::Device. The editor's
    -- ImGuiContextLayer.cpp `#includes "Gui/ImGuiImplWebGPU.hpp"` via
    -- the engine include path and links the INDEX_API exports.

    UseDependencySet(Dependency.EditorRuntimeCommon)
    UseIndexProfilerDependencies(true)
    defines(GetIndexModuleDefines())
    ApplyIndexEditorModuleDefine()
    defines { "IDX_IMPORT_DLL" }
    includedirs { "src" }
    -- ImGuizmo translate gizmo (editor-only StaticLib, see root premake5.lua).
    includedirs { path.join(ROOT_DIR, "External/ImGuizmo/src") }
    dependson { "ImGuizmo" }
    links { "ImGuizmo" }
    -- The editor's Rebuild Engine flow rewrites IndexEntityBitsConfig.h
    -- to override INDEX_ENTITY_BITS without a premake regen. The EnTT
    -- patch picks it up via `#include "IndexEntityBitsConfig.h"`. See
    -- WriteIndexEntityBitsConfigHeader() in the root premake5.lua.
    includedirs { IndexEntityBitsConfigIncludeDir }

    -- Precompile src/EditorPch.hpp once per editor build instead of re-parsing
    -- it in every TU. EditorPch.cpp is a one-line stub that includes it; both
    -- files live in src/ so they're picked up by the "src/**.cpp" glob above.
    -- /FI EditorPch.hpp (forceincludes) auto-prepends the PCH header to TUs
    -- that don't already include it (e.g. the engine's Diagnostics .cpp files
    -- added below, and the editor .cpp files that include their own header
    -- first). Without this PCH setup ComponentInspectors.cpp exhausts the
    -- compiler heap with C1060 even with /Zm2000 + 64-bit hosted cl.exe.
    --
    -- This used to precompile Index-Engine's pch.hpp directly. That PCH is
    -- shared with the engine DLL, so it deliberately stops at the STL + Core/
    -- boundary and carries neither ImGui nor the ECS surface — the engine must
    -- not pull ImGui into every TU. The editor has the opposite profile (23 of
    -- its 32 TUs include <imgui.h>, 18 reach Scene.hpp and through it
    -- entt::registry), so it now layers its own PCH on top of the engine's.
    -- Measured over the transitive closure of all 32 editor TUs that takes a
    -- full editor build from ~1,056k lines parsed to ~422k (-60%). See
    -- src/EditorPch.hpp for the contents and the rule for adding to them.
    pchheader "EditorPch.hpp"
    pchsource "src/EditorPch.cpp"
    forceincludes { "EditorPch.hpp" }
    -- ScriptSystem loads Index-ScriptCore.dll at runtime. The packaged-layout
    -- branch (ScriptSystem.cpp:194) finds it next to the editor exe; the
    -- copy lets a zipped Editor folder work standalone. The managed C# project
    -- builds on Windows only (see root premake5.lua), so gate the dep + copy.
    if os.target() == "windows" then dependson { "Index-ScriptCore" } end
    postbuildcommands { CopyIndexAssets, CopyIndexEngineDll, CopyGlfwDll, CopyGladDll }
    if CopyDawnSharedLib ~= "" then postbuildcommands { CopyDawnSharedLib } end
    if os.target() == "windows" then postbuildcommands(CopyScriptCoreArtifacts) end
    ApplyIndexProfilerRuntime()

    filter "system:windows"
        -- See Index-Engine/premake5.lua for the rationale on
        -- MultiProcessorCompile + /Zc:preprocessor.
        --
        -- /Zm2000 (max compiler heap reservation) used to be required here:
        -- ComponentInspectors.cpp's Properties::Make<EnumType>/MakeWith<T>
        -- template forest hit `fatal error C1060: out of heap space` without
        -- it, even with MAGIC_ENUM_RANGE_MIN/MAX tightened and the 64-bit
        -- hosted cl.exe (Directory.Build.props: PreferredToolArchitecture).
        --
        -- It is gone because the actual heap hog was found and removed: a
        -- magic_enum::customize::enum_range<Index::Wrap> specialisation spanning
        -- 0x2901..0x8370, i.e. 23,152 constexpr instantiations to reflect four
        -- sparse GL constants. Graphics/Wrap.hpp now maps those names by hand,
        -- and <magic_enum/magic_enum.hpp> no longer reaches the PCH via
        -- Core/Base.hpp. Peak per-cl.exe memory roughly halved (1.96 -> 1.07 GB)
        -- and a full editor rebuild dropped to ~19s, so the default heap is
        -- ample. ComponentInspectors.cpp never used enum reflection at all --
        -- the old comment here blaming magic_enum for its cost was wrong; its
        -- weight is plain template instantiation, which /bigobj below covers.
        --
        -- Do not reintroduce /Zm: it is a per-cl.exe *reservation*, so it
        -- multiplies with MultiProcessorCompile. See Directory.Build.props for
        -- the CL_MPCount cap that bounds that product.
        flags { "MultiProcessorCompile" }
        buildoptions { "/utf-8", "/FS", "/Zc:preprocessor" }
        systemversion "latest"
        defines { "IDX_PLATFORM_WINDOWS" }
        links { "Ole32" }
        postbuildcommands {
            '{COPYFILE} "' .. path.join(ROOT_DIR, "External/dotnet/lib/nethost.dll") .. '" "%{cfg.targetdir}/nethost.dll"'
        }

    filter "system:linux"
        defines { "IDX_PLATFORM_LINUX" }
        linkoptions { "-Wl,-rpath,'$$ORIGIN'" }

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

    -- ComponentInspectors.cpp and EditorComponentRegistration.cpp are large
    -- per-component ImGui inspector TUs whose section count grows with every
    -- component they cover, so /bigobj keeps them clear of the 64K COFF
    -- section limit (C1128). They contain no Properties::Make* calls; that
    -- template forest lives only in Index-Engine's
    -- BuiltInComponentRegistration<Category>.cpp slices.
    filter { "files:**/ComponentInspectors.cpp", "system:windows" }
        buildoptions { "/bigobj" }

    filter { "files:**/EditorComponentRegistration.cpp", "system:windows" }
        buildoptions { "/bigobj" }

    filter {}
