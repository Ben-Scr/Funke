-- Web export (WebAssembly + WebGPU) for Funke games.
--
-- Install this package to enable the Web tile in Build Profiles. Building needs the Emscripten SDK
-- (set EMSDK, or let `check` point at a local emsdk), CMake, Ninja and Python 3.10+. The game runs in
-- browsers with WebGPU (Chrome/Edge 113+, Safari 26+, Firefox 141+).
return {
    name         = "Funke.Platform.Web",
    version      = "0.1.0",
    kind         = "platform",
    description  = "Build Funke games for the browser (WebAssembly, WebGPU).",
    platform     = "Web",
    builder      = "Builder/build.py",
    hosts        = "windows,macos,linux",
    protocol     = "1",
    dependencies = { "Funke.ScriptRuntime.Mono" },
}
