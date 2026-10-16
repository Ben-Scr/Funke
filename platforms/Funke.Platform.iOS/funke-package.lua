-- iOS export for Funke games.
--
-- Builds run on a Mac: Xcode 15+, CMake, Python 3.10+. The builder produces an Xcode project and,
-- with a development team set in the build options, a signed .app for devices.
return {
    name         = "Funke.Platform.iOS",
    version      = "0.1.0",
    kind         = "platform",
    description  = "Build Funke games for iPhone and iPad (arm64, Metal).",
    platform     = "iOS",
    builder      = "Builder/build.py",
    hosts        = "macos",
    protocol     = "1",
    dependencies = { "Funke.ScriptRuntime.Mono" },
}
