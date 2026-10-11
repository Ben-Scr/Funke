group "Core"
project "Index-ScriptCore"
    location "."
    kind "SharedLib"
    language "C#"
    dotnetframework "net10.0"
    clr "Unsafe"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    vsprops {
        AppendTargetFrameworkToOutputPath = "false",
        Nullable = "enable",
        AllowUnsafeBlocks = "true",
        EnableDynamicLoading = "true",
        TreatWarningsAsErrors = "true",
        -- Ships Index-ScriptCore.xml next to the DLL so game projects get API docs in IntelliSense.
        GenerateDocumentationFile = "true",
        NoWarn = "$(NoWarn);CS1591",
    }

    files {
        "Source/**.cs"
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        optimize "Off"
        symbols "On"
        defines { "IDX_DEBUG" }

    filter "configurations:Release"
        optimize "On"
        symbols "On"
        defines { "IDX_RELEASE" }

    filter "configurations:Dist"
        optimize "Full"
        symbols "On"
        defines { "IDX_DIST" }
