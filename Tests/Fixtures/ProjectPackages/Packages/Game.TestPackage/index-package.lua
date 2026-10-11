return {
    name = "Game.TestPackage",
    version = "1.0.0",
    description = "Build fixture for project-local package exports.",
    layers = {
        native_standalone = {
            sources = { "Native/**.cpp" },
            includes = { "Native" },
        },
        csharp = {
            sources = { "CSharp/**.cs" },
            pinvoke_dll = "Pkg.Game.TestPackage.Native",
        },
    },
}
