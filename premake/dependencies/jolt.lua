-- Jolt Physics (3D rigid bodies). Built without any JPH_* feature defines so the
-- engine TUs that include Jolt headers need none either: every JPH_* option
-- changes inline code and object layout, and a lib/consumer mismatch is UB.
-- No GPU compute (hair), ObjectStream, debug renderer or profiler.
project "Jolt"
    location (FunkeProjectLocation("Jolt", path.join(ROOT_DIR, "premake/generated/Jolt")))
    kind "StaticLib"
    language "C++"
    cppdialect "C++17"
    staticruntime "off"
    warnings "Off"

    targetdir (path.join(ROOT_DIR, "bin/" .. outputdir .. "/%{prj.name}"))
    objdir (path.join(ROOT_DIR, "bin-int/" .. outputdir .. "/%{prj.name}"))

    files
    {
        path.join(ROOT_DIR, "External/JoltPhysics/Jolt/**.h"),
        path.join(ROOT_DIR, "External/JoltPhysics/Jolt/**.inl"),
        path.join(ROOT_DIR, "External/JoltPhysics/Jolt/**.cpp")
    }

    removefiles
    {
        path.join(ROOT_DIR, "External/JoltPhysics/Jolt/Compute/CPU/**"),
        path.join(ROOT_DIR, "External/JoltPhysics/Jolt/Compute/DX12/**"),
        path.join(ROOT_DIR, "External/JoltPhysics/Jolt/Compute/MTL/**"),
        path.join(ROOT_DIR, "External/JoltPhysics/Jolt/Compute/VK/**"),
        path.join(ROOT_DIR, "External/JoltPhysics/Jolt/Shaders/**"),
        -- SerializableObject.cpp stays: every shape/constraint settings class needs it.
        path.join(ROOT_DIR, "External/JoltPhysics/Jolt/ObjectStream/ObjectStream*.cpp"),
        path.join(ROOT_DIR, "External/JoltPhysics/Jolt/ObjectStream/TypeDeclarations.cpp")
    }

    includedirs
    {
        path.join(ROOT_DIR, "External/JoltPhysics")
    }

    filter "system:windows"
        flags { "MultiProcessorCompile" }
        buildoptions { "/FS", "/Zc:preprocessor" }
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
        defines { "_DEBUG" }

    filter "configurations:Release"
        runtime "Release"
        optimize "On"
        symbols "On"
        defines { "NDEBUG" }

    filter "configurations:Dist"
        runtime "Release"
        optimize "Full"
        symbols "Off"
        defines { "NDEBUG" }

    filter {}
