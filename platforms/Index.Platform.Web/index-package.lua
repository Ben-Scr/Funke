-- Web export (WebAssembly + WebGPU) for Index games.
--
-- Install this package to enable the Web tile in Build Profiles. Building needs the Emscripten SDK
-- (set EMSDK, or let `check` point at a local emsdk), CMake, Ninja and Python 3.10+. The game runs in
-- browsers with WebGPU (Chrome/Edge 113+, Safari 26+, Firefox 141+).
return {
    name         = "Index.Platform.Web",
    version      = "0.1.0",
    kind         = "platform",
    description  = "Build Index games for the browser (WebAssembly, WebGPU).",
    platform     = "Web",
    builder      = "Builder/build.py",
    hosts        = "windows,macos,linux",
    protocol     = "1",
    dependencies = { "Index.ScriptRuntime.Mono" },
}
