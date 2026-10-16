group "Tools"
project "Funke-PackageTool"
    location "."
    kind "ConsoleApp"
    language "C#"
    dotnetframework "net10.0"

    targetdir ("../bin/" .. outputdir .. "/%{prj.name}")
    objdir ("../bin-int/" .. outputdir .. "/%{prj.name}")

    vsprops {
        AppendTargetFrameworkToOutputPath = "false",
        ImplicitUsings = "enable",
        Nullable = "enable",
    }

    files { "Program.cs" }

    filter "configurations:Debug"
        optimize "Off"
        symbols "On"

    filter "configurations:Release"
        optimize "On"
        symbols "On"

    filter "configurations:Dist"
        optimize "Full"
        symbols "Off"

    filter {}
