-- C# scripting on platforms without the desktop .NET runtime (Android, iOS, Web).
--
-- A build component, not a game package: platform packages depend on it, compile Native/ into the
-- game binary and call FunkeScriptRuntimeMono_Register() before the engine starts. It downloads the
-- official Mono runtime packs from nuget.org (verified against the NuGet catalog hash).
return {
    name        = "Funke.ScriptRuntime.Mono",
    version     = "0.1.0",
    kind        = "build-component",
    description = "Mono-based .NET runtime backend for Android, iOS and Web builds.",
    builder     = "Builder/mono_runtime.py",
    protocol    = "1",
}
