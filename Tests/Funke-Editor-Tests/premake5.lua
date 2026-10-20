project "Funke-Editor-Tests"
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

    -- Editor logic under test that lives in a .cpp (not header-only). The
    -- editor builds as an executable, not a lib, so its translation units
    -- can't be linked; instead we compile the specific, GPU/ImGui-free
    -- source files we exercise directly into the test binary.
    -- EditorCamera.cpp is pure glm math (no ImGui); its `#include "pch.hpp"`
    -- resolves through the engine include dir below.
    files
    {
        path.join(ROOT_DIR, "Funke-Editor/src/Build/GameBuilder.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Editor/BuildAssetPruning.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Editor/PlatformBuilder.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Editor/SpriteSlicing.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Gui/AssetSaveDialog.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Gui/EditorDockLayout.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Editor/ScriptWindowTypes.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Editor/EditorCamera.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Editor/EditorCamera3D.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Undo/Transform3DEditCommand.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Editor/FunkePackageInstaller.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Editor/ExternalEditor.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Editor/EditorRestart.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Scripting/StructFieldBytes.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Undo/UndoStack.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Undo/TransformEditCommand.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Undo/InspectorEditCommand.cpp"),
        path.join(ROOT_DIR, "Funke-Editor/src/Undo/StructureEditCommand.cpp")
    }

    -- Link the engine SharedLib (same set the editor uses) so calls into
    -- Funke:: resolve, then add the editor's own src dir so editor headers
    -- like "Editor/EditorCamera.hpp" and "Scripting/ScriptDiscovery.hpp"
    -- (header-only) include cleanly.
    UseDependencySet(Dependency.EditorRuntimeCommon)
    UseFunkeProfilerDependencies(true)
    defines(GetFunkeModuleDefines())
    ApplyFunkeEditorModuleDefine()
    defines { "FNK_IMPORT_DLL" }
    includedirs { path.join(ROOT_DIR, "Funke-Editor/src") }

    -- doctest is a single-header lib at External/doctest/doctest/doctest.h.
    includedirs { path.join(ROOT_DIR, "External/doctest") }

    -- Same DLL-copying postbuild as the engine tests so the exe finds
    -- Funke-Engine.dll (and friends) next to it at run time.
    postbuildcommands
    {
        CopyFunkeEngineDll,
        CopySDL3SharedLib,
        CopyGladDll
    }
    if CopyDawnSharedLib ~= "" then postbuildcommands { CopyDawnSharedLib } end
    ApplyFunkeProfilerRuntime()

    filter "system:windows"
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

    filter {}

    -- Per-config libdirs for webgpu_dawn.lib. Tests/ sits one level deeper
    -- than the top-level project folders so the rel-prefix is "../../".
    ApplyDawnLibDirs("../../")
