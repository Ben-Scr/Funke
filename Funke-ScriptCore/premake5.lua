group "Core"
project "Funke-ScriptCore"
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
        -- Ships Funke-ScriptCore.xml next to the DLL so game projects get API docs in IntelliSense.
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
        defines { "FNK_DEBUG" }

    filter "configurations:Release"
        optimize "On"
        symbols "On"
        defines { "FNK_RELEASE" }

    filter "configurations:Dist"
        optimize "Full"
        symbols "On"
        defines { "FNK_DIST" }
